#include "engine/core/ScriptSystem.hpp"

#include <chrono>
#include <cstring>
#include <daScript/ast/dyn_modules.h>
#include <daScript/misc/free_list.h>

#include "engine/core/LogHistory.hpp"
#include "engine/core/Profiler.hpp"

namespace tryengine::core {

namespace {

//! Уникальный номер поколения контекста. Template-кеш поиска функции сравнивает его с
//! номером, при котором нашёл SimFunction*, поэтому номер не должен повторяться и между
//! экземплярами ScriptSystem в одном процессе.
uint64_t NextScriptRevision() {
    static uint64_t next = 0;
    return ++next;
}

double ElapsedMs(const std::chrono::steady_clock::time_point from, const std::chrono::steady_clock::time_point to) {
    return std::chrono::duration<double, std::milli>(to - from).count();
}

//! daScript's environment is thread-local. Compiling on a pool worker must bind the
//! environment created on the main thread (its module registry is the one the generation is
//! built against), and the worker's reuse caches live only for the duration of the job.
struct DasWorkerScope {
    das::ReuseCacheGuard reuse_cache;
    das::daScriptEnvironment* previous;

    explicit DasWorkerScope(das::daScriptEnvironment* environment)
        : previous(das::daScriptEnvironment::exchangeBound(environment)) {}

    ~DasWorkerScope() {
        das::daScriptEnvironment::setBound(previous);
    }

    DasWorkerScope(const DasWorkerScope&) = delete;
    DasWorkerScope& operator=(const DasWorkerScope&) = delete;
};

}  // namespace

//! Фоновая компиляция живёт в shared_ptr: поток пула может закончить работу, когда хост уже
//! закрывает окно; деструктор ScriptSystem дожидается флага done, а не висячего `this`.
struct ScriptSystem::AsyncCompileState {
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    bool hot_reload = false;
    eastl::string path;
    CompileTiming timing;
    std::optional<Result<CompiledScript>> result;
    std::chrono::steady_clock::time_point requested_at;
};

ScriptSystem::ScriptSystem(ScriptSystemConfig config) : config_(eastl::move(config)) {
    TRY_ASSERT(config_.create_context, "Фабрика контекста должна быть задана!");
    TRY_ASSERT(config_.register_modules, "Регистрация модулей должна быть задана!");
    TRY_ASSERT(config_.create_file_access, "Фабрика FileAccess должна быть задана!");

    das::setDasRoot("./");

    // 1. Статическая регистрация C++ модулей (PULL_MODULE / NEED_ALL_DEFAULT_MODULES)
    config_.register_modules();

    // 2. Источник файлов (папки или пак) решает тот, кто собрал конфиг. Он нужен только
    // для require_dynamic_modules: каждое поколение компилируется через свой FileAccess,
    // иначе кеш прочитанных файлов вернул бы старую версию скрипта.
    file_access_ = config_.create_file_access();
    TRY_ASSERT(file_access_, "FileAccess не создан (битый пак?)");

    // 3. Загрузка динамических модулей. dasLiveHost приложение регистрирует статически
    // (DAS::LiveHost): иначе в процессе оказались бы две копии live-хоста с разными
    // хранилищами и флагами.
    das::TextPrinter tout;
    das::vector<das::string> load_modules;
    das::vector<das::string> disabled_modules;
    disabled_modules.push_back("dasLiveHost");
    das::require_dynamic_modules(file_access_, das::getDasRoot(), "", load_modules, disabled_modules, tout);

    // 4. Глобальная инициализация модулей daScript
    das::Module::Initialize();
}

ScriptSystem::~ScriptSystem() {
    // Фоновая компиляция не должна пережить систему: она читает config_ и пишет в своё
    // состояние, но закрываться посреди неё процесс не имеет права.
    if (async_) {
        LogInfo(LogCategory::Script, "Ожидание фоновой компиляции скриптов перед выходом");
        auto state = async_;
        std::unique_lock lock(state->mutex);
        state->cv.wait(lock, [&state] { return state->done; });
        // Host exited before PollAsync consumed the result: the error still has to be marked
        // checked, otherwise its destructor asserts in Debug builds.
        if (state->result.has_value() && !state->result->has_value())
            state->result->error().Discard();
        async_.reset();
    }

    if (das_ctx_) {
        delete das_ctx_;
        das_ctx_ = nullptr;
    }
    // Программа должна пережить контекст, группа модулей — программу.
    program_ = nullptr;
    module_group_.reset();
    das::Module::Shutdown();
}

Result<ScriptSystem::CompiledScript> ScriptSystem::CompileMainScript(eastl::string_view path, bool use_module_cache,
                                                                     das::smart_ptr<das::FileAccess> file_access,
                                                                     CompileTiming& timing) const {
    das::TextPrinter tout;
    CompiledScript result;
    result.module_group = std::make_unique<das::ModuleGroup>();

    if (!file_access)
        return LogAndMakeError("FileAccess не создан (битый пак?)");

    if (config_.prepare_file_access)
        config_.prepare_file_access(*file_access);

    const das::string script_path(path.data(), path.size());

    // Перезагрузка собирает модули проекта заново: без ignore_shared_modules
    // compileDaScript переиспользует promoted-модули прошлой сборки, и правка не видна.
    // AST-кеш отдаёт неизменённые модули без повторного парсинга.
    das::CodeOfPolicies policies = config_.policies;
    if (use_module_cache) {
        result.module_cache = std::make_unique<das::ModuleFileCache>();
        const das::string cache_path =
            das::ModuleFileCache::defaultPath(script_path, "tryengine-editor", "project.das_project");
        result.module_cache->install(cache_path, cache_path, true);
        policies.module_cache = result.module_cache->requested;
        policies.ignore_shared_modules = true;
    }

    auto finish_module_cache = [&result]() {
        if (result.module_cache)
            result.module_cache->finish();
    };

    const auto compile_begin = std::chrono::steady_clock::now();
    das::ProgramPtr program;
    {
        program = das::compileDaScript(script_path, file_access, tout, *result.module_group, policies);
    }
    const auto compile_end = std::chrono::steady_clock::now();
    timing.compile_ms = ElapsedMs(compile_begin, compile_end);

    if (!program) {
        finish_module_cache();
        return LogAndMakeError("Ошибка компиляции скрипта (program == nullptr): {}", path.data());
    }

    if (program->failed()) {
        finish_module_cache();
        eastl::string error_text;
        for (const auto& error : program->errors) {
            const auto report = das::reportError(error.at, error.what, error.extra, error.fixme, error.cerr);
            LogError(LogCategory::Script, "{}", report.c_str());
            if (!error_text.empty())
                error_text += "\n";
            error_text.append(report.c_str(), report.size());
        }
        if (error_text.empty())
            error_text = "Неизвестная ошибка компиляции";
        return Error(ErrorCode::Unknown, eastl::move(error_text));
    }

    // Поздние require (llvm_macro из just_in_time) идут уже на симуляции и требуют
    // shared-модуль; на время разбора мы его намеренно игнорировали.
    program->policies.ignore_shared_modules = false;

    // Кеш закрывается до симуляции: он описывает фазу разбора. Модули, которые симуляция
    // дочитывает через require_module_now, живут в promoted-списке и переживают перезагрузку.
    finish_module_cache();

    const auto simulate_begin = std::chrono::steady_clock::now();
    auto* new_ctx = config_.create_context(program->getContextStackSize());
    if (!new_ctx)
        return LogAndMakeError("Фабрика контекста вернула nullptr: {}", path.data());

    if (!program->simulate(*new_ctx, tout)) {
        delete new_ctx;
        finish_module_cache();
        return LogAndMakeError("Ошибка симуляции контекста! {}", tout.c_str());
    }
    timing.simulate_ms = ElapsedMs(simulate_begin, std::chrono::steady_clock::now());

    result.program = eastl::move(program);
    result.context.reset(new_ctx);
    return result;
}

void ScriptSystem::StartCompile(bool hot_reload, bool fresh_file_access) {
    TRY_ASSERT(!async_, "Фоновая компиляция уже идёт");
    TRY_ASSERT(!compiling_, "Фоновая компиляция уже идёт");

    auto state = std::make_shared<AsyncCompileState>();
    state->hot_reload = hot_reload;
    state->path = main_script_path_;
    state->requested_at = std::chrono::steady_clock::now();

    async_ = state;
    compiling_ = true;
    status_ = ScriptStatus::Compiling;

    // use_module_cache тот же, что у синхронного пути: у редактора он есть всегда (у него
    // подключено reload-расширение), у игры его нет.
    const bool use_module_cache = reload_extension_ != nullptr;
    // Первая загрузка переиспользует FileAccess из конструктора (он уже прочитал
    // динамические модули; у игры его фабрика одноразовая — пак отдаётся по move).
    // Перезагрузке и повторной первой загрузке нужен свежий доступ, иначе кеш текста
    // вернул бы старую (сломанную) версию скрипта.
    auto file_access = (das_ctx_ || fresh_file_access) ? config_.create_file_access() : file_access_;
    // daScriptEnvironment is TLS: the worker cannot see the module registry the main thread
    // initialized. Capture it here and bind it inside the job; without this the compiler
    // crashes in AstSerializer before the first module is parsed.
    auto* environment = das::daScriptEnvironment::getBound();
    const bool background = config_.post_background && environment != nullptr;
    if (config_.post_background && environment == nullptr)
        LogError(LogCategory::Script, "daScriptEnvironment не привязан к главному потоку; компиляция пойдёт на нём");

    eastl::function<void()> work = [this, state, use_module_cache, file_access, environment, background]() {
        auto compile = [&]() {
            auto result = CompileMainScript(state->path, use_module_cache, file_access, state->timing);
            state->timing.wall_ms = ElapsedMs(state->requested_at, std::chrono::steady_clock::now());
            {
                std::lock_guard lock(state->mutex);
                state->result.emplace(eastl::move(result));
                state->done = true;
            }
            state->cv.notify_all();
        };

        if (background) {
            DasWorkerScope scope(environment);
            compile();
        } else {
            compile();
        }
    };

    LogInfo(LogCategory::Script, "Скрипт {}: {}", state->path.c_str(), background ? "компиляция в фоне" : "компиляция");
    if (background) {
        config_.post_background(eastl::move(work));
    } else {
        work();
        // Синхронный хост не обязан знать про PollAsync: без фонового исполнителя работа
        // уже сделана, и установка поколения — часть того же вызова.
        PollAsync();
    }
}

void ScriptSystem::LoadMainScriptAsync(eastl::string_view path) {
    if (async_ || compiling_)
        return LogError(LogCategory::Script, "Загрузка скрипта уже идёт");
    if (das_ctx_)
        return LogError(LogCategory::Script, "Главный скрипт уже загружен");
    if (status_ == ScriptStatus::Failed)
        return LogError(LogCategory::Script, "Движок заперт после ошибки загрузки скриптов");

    main_script_path_.assign(path.data(), path.size());
    StartCompile(/*hot_reload=*/false, /*fresh_file_access=*/false);
}

void ScriptSystem::ReloadMainScriptAsync() {
    if (!reload_extension_)
        return LogError(LogCategory::Script, "Горячая перезагрузка не подключена");
    if (main_script_path_.empty() || !das_ctx_)
        return LogError(LogCategory::Script, "Главный скрипт ещё не загружен");
    if (async_ || compiling_)
        return LogError(LogCategory::Script, "Горячая перезагрузка уже идёт");

    // Снимок состояния делает старый контекст, пока он ещё цел. Хуки идут до компиляции,
    // чтобы ошибка разбора нового поколения не мешала сохранению состояния.
    eastl::string hook_errors;
    {
        TRY_PROFILE_SCOPE("Script.Reload.BeforeHooks");
        InvokeReloadHooks(das_ctx_, "__before_reload_", hook_errors);
    }
    pending_hook_errors_ = eastl::move(hook_errors);

    StartCompile(/*hot_reload=*/true, /*fresh_file_access=*/false);
}

bool ScriptSystem::RetryMainScriptAsync() {
    if (status_ != ScriptStatus::Failed || async_ || compiling_)
        return false;

    LogInfo(LogCategory::Script, "Скрипт {}: повторная компиляция", main_script_path_.c_str());
    StartCompile(/*hot_reload=*/false, /*fresh_file_access=*/true);
    return true;
}

void ScriptSystem::PollAsync() {
    if (!async_)
        return;

    {
        std::lock_guard lock(async_->mutex);
        if (!async_->done)
            return;
    }

    auto state = eastl::move(async_);
    async_.reset();
    compiling_ = false;

    auto result = eastl::move(*state->result);
    if (!result.has_value()) {
        eastl::string message(result.error().Message().data(), result.error().Message().size());
        FailCompile(state->hot_reload, message, state->timing);
        return;
    }

    // A successful compile after a failed one is the moment the stale error stops being
    // true. Drop the ring so the Log window does not keep showing it; the file on disk is
    // the run's history and is not touched. Normal reloads keep their history.
    const bool recovering = compile_error_;
    if (recovering)
        LogHistory::instance().clear();

    {
        TRY_PROFILE_SCOPE("Script.Install");
        if (state->hot_reload)
            InstallReload(eastl::move(*result), state->timing);
        else
            InstallFirstLoad(eastl::move(*result), state->path, state->timing);
    }
    status_ = ScriptStatus::Ready;
    compile_error_ = false;
    last_error_.clear();
    last_compile_ms_ = state->timing.wall_ms;
}

void ScriptSystem::FailCompile(bool hot_reload, eastl::string_view message, const CompileTiming& timing) {
    last_error_.assign(message.data(), message.size());
    compile_error_ = true;
    last_compile_ms_ = timing.wall_ms;
    // Снимок, снятый [before_reload] перед неудачной попыткой, к следующей уже не относится.
    pending_hook_errors_.clear();

    if (hot_reload) {
        // Старое поколение остаётся рабочим: ошибка перезагрузки не имеет права отнимать
        // у редактора уже работающий движок. Play при этом запрещён до успешной попытки.
        status_ = ScriptStatus::Ready;
        if (reload_extension_)
            reload_extension_->OnReloadFinished(false, last_error_);
        LogError(LogCategory::Script, "Перезагрузка скриптов не удалась за {:.1f} мс: {}", timing.wall_ms, last_error_);
        return;
    }

    // Первая загрузка провалилась: контекста нет, движок заперт до перезапуска процесса.
    status_ = ScriptStatus::Failed;
    LogError(LogCategory::Script, "Загрузка скриптов не удалась за {:.1f} мс: {}", timing.wall_ms, last_error_);
}

void ScriptSystem::InstallFirstLoad(CompiledScript&& compiled, eastl::string_view path, const CompileTiming& timing) {
    // Предыдущее поколение (если LoadMainScript зовут повторно) живёт до установки нового.
    // Кеш объявлен первым: он переживает контекст, программу и группу модулей.
    std::unique_ptr<das::ModuleFileCache> old_cache = eastl::move(module_cache_);
    std::unique_ptr<das::Context> old_context(das_ctx_);
    das::ProgramPtr old_program = eastl::move(program_);
    std::unique_ptr<das::ModuleGroup> old_group = eastl::move(module_group_);

    das_ctx_ = compiled.context.release();
    revision_ = NextScriptRevision();
    module_cache_ = eastl::move(compiled.module_cache);
    program_ = eastl::move(compiled.program);
    module_group_ = eastl::move(compiled.module_group);
    main_script_path_.assign(path.data(), path.size());

    old_context.reset();
    old_program = nullptr;
    old_group.reset();
    old_cache.reset();

    LogInfo(LogCategory::Script, "Скрипт {} загружен за {:.1f} мс (компиляция {:.1f}, симуляция {:.1f})", path.data(),
            timing.wall_ms, timing.compile_ms, timing.simulate_ms);
}

void ScriptSystem::InstallReload(CompiledScript&& compiled, const CompileTiming& timing) {
    std::unique_ptr<das::ModuleFileCache> old_cache = eastl::move(module_cache_);
    std::unique_ptr<das::Context> old_context(das_ctx_);
    das::ProgramPtr old_program = eastl::move(program_);
    std::unique_ptr<das::ModuleGroup> old_group = eastl::move(module_group_);

    das_ctx_ = compiled.context.release();
    // Новая ревизия до ReinitializeContext: точки входа хоста уже обязаны искать функции
    // в новом контексте, а не брать кеш прошлого поколения.
    revision_ = NextScriptRevision();
    module_cache_ = eastl::move(compiled.module_cache);
    program_ = eastl::move(compiled.program);
    module_group_ = eastl::move(compiled.module_group);

    // Точки входа хоста повторяются на новом контексте: TimeInit/SetInputSource,
    // подписки редактора, начальная сцена. [after_reload] затем возвращает состояние.
    if (reload_extension_) {
        TRY_PROFILE_SCOPE("Script.Reload.Reinit");
        reload_extension_->ReinitializeContext(*this);
    }
    das_ctx_->restart();

    eastl::string hook_errors = eastl::move(pending_hook_errors_);
    pending_hook_errors_.clear();
    {
        TRY_PROFILE_SCOPE("Script.Reload.AfterHooks");
        InvokeReloadHooks(das_ctx_, "__after_reload_", hook_errors);
    }

    // Старое поколение уходит последним: его контекст ссылается на программу и модули,
    // а AST-кеш — на FileInfo, которые эти модули ещё читают.
    old_context.reset();
    old_program = nullptr;
    old_group.reset();
    old_cache.reset();

    if (reload_extension_)
        reload_extension_->OnReloadFinished(true, hook_errors);

    LogInfo(LogCategory::Script, "Скрипты перезагружены за {:.1f} мс (компиляция {:.1f}, симуляция {:.1f})",
            timing.wall_ms, timing.compile_ms, timing.simulate_ms);

    if (!hook_errors.empty())
        LogError(LogCategory::Script, "Перезагрузка скриптов завершилась с ошибками: {}", hook_errors);
}

bool ScriptSystem::InvokeReloadHooks(das::Context* ctx, const char* prefix, eastl::string& errors) {
    if (!ctx || !prefix)
        return true;

    const size_t prefix_len = std::strlen(prefix);
    bool ok = true;

    // Контекст должен быть чистым: предыдущая ошибка скрипта или исключение в
    // реинициализации не должны превращать снимок состояния в повторное исключение.
    ctx->restart();

    const int32_t total = ctx->getTotalFunctions();
    for (int32_t i = 0; i < total; ++i) {
        auto* function = ctx->getFunction(i);
        if (!function || !function->name || !function->debugInfo || function->debugInfo->count != 0)
            continue;
        if (std::strncmp(function->name, prefix, prefix_len) != 0)
            continue;

        ctx->evalWithCatch(function, nullptr);
        if (const char* ex = ctx->getException()) {
            LogError(LogCategory::Script, "Хук перезагрузки '{}' упал: {}", function->name, ex);
            if (!errors.empty())
                errors += "\n";
            errors += function->name;
            errors += ": ";
            errors += ex;
            ctx->clearException();
            ok = false;
        }
    }
    return ok;
}

Result<void> ScriptSystem::LoadMainScript(eastl::string_view path) {
    if (async_ || compiling_)
        return LogAndMakeError("Загрузка скрипта уже идёт");

    CompileTiming timing;
    const auto begin = std::chrono::steady_clock::now();
    auto compiled = CompileMainScript(path, /*use_module_cache=*/reload_extension_ != nullptr, file_access_, timing);
    if (!compiled.has_value()) {
        timing.wall_ms = ElapsedMs(begin, std::chrono::steady_clock::now());
        eastl::string message(compiled.error().Message().data(), compiled.error().Message().size());
        FailCompile(/*hot_reload=*/false, message, timing);
        return std::unexpected(std::move(compiled.error()));
    }

    timing.wall_ms = ElapsedMs(begin, std::chrono::steady_clock::now());
    if (compile_error_)
        LogHistory::instance().clear();
    InstallFirstLoad(eastl::move(*compiled), path, timing);
    status_ = ScriptStatus::Ready;
    compile_error_ = false;
    last_error_.clear();
    last_compile_ms_ = timing.wall_ms;
    return {};
}

Result<void> ScriptSystem::ReloadMainScript() {
    if (!reload_extension_)
        return LogAndMakeError("Горячая перезагрузка не подключена");
    if (main_script_path_.empty() || !das_ctx_)
        return LogAndMakeError("Главный скрипт ещё не загружен");
    if (async_ || compiling_)
        return LogAndMakeError("Горячая перезагрузка уже идёт");

    eastl::string hook_errors;
    {
        TRY_PROFILE_SCOPE("Script.Reload.BeforeHooks");
        InvokeReloadHooks(das_ctx_, "__before_reload_", hook_errors);
    }
    pending_hook_errors_ = eastl::move(hook_errors);

    CompileTiming timing;
    const auto begin = std::chrono::steady_clock::now();
    auto compiled =
        CompileMainScript(main_script_path_, /*use_module_cache=*/true, config_.create_file_access(), timing);
    timing.wall_ms = ElapsedMs(begin, std::chrono::steady_clock::now());
    if (!compiled.has_value()) {
        eastl::string message(compiled.error().Message().data(), compiled.error().Message().size());
        FailCompile(/*hot_reload=*/true, message, timing);
        return std::unexpected(std::move(compiled.error()));
    }

    if (compile_error_)
        LogHistory::instance().clear();
    InstallReload(eastl::move(*compiled), timing);
    status_ = ScriptStatus::Ready;
    compile_error_ = false;
    last_error_.clear();
    last_compile_ms_ = timing.wall_ms;
    return {};
}

}  // namespace tryengine::core
