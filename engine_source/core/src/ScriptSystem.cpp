#include "engine/core/ScriptSystem.hpp"

#include <cstring>
#include <daScript/ast/dyn_modules.h>

namespace tryengine::core {

ScriptSystem::ScriptSystem(ScriptSystemConfig config) : config_(eastl::move(config)) {
    TRY_ASSERT(config_.create_context, "Фабрика контекста должна быть задана!");
    TRY_ASSERT(config_.register_modules, "Регистрация модулей должна быть задана!");
    TRY_ASSERT(config_.create_file_access, "Фабрика FileAccess должна быть задана!");

    das::setDasRoot("./");

    // 1. Статическая регистрация C++ модулей (PULL_MODULE / NEED_ALL_DEFAULT_MODULES)
    config_.register_modules();

    // 2. Источник файлов (папки или пак) решает тот, кто собрал конфиг
    file_access_ = config_.create_file_access();
    TRY_ASSERT(file_access_, "FileAccess не создан (битый пак?)");

    // 3. Загрузка динамических модулей
    das::TextPrinter tout;
    das::vector<das::string> load_modules;
    das::require_dynamic_modules(file_access_, das::getDasRoot(), "", load_modules, tout);

    // 4. Глобальная инициализация модулей daScript
    das::Module::Initialize();
}

ScriptSystem::~ScriptSystem() {
    if (das_ctx_) {
        delete das_ctx_;
        das_ctx_ = nullptr;
    }
    // Программа должна пережить контекст, группа модулей — программу.
    program_ = nullptr;
    module_group_.reset();
    das::Module::Shutdown();
}

Result<ScriptSystem::CompiledScript> ScriptSystem::CompileMainScript(eastl::string_view path) {
    das::TextPrinter tout;
    CompiledScript result;
    result.module_group = std::make_unique<das::ModuleGroup>();

    if (config_.prepare_file_access)
        config_.prepare_file_access(*file_access_);

    const das::string script_path(path.data(), path.size());
    auto program = das::compileDaScript(script_path, file_access_, tout, *result.module_group, config_.policies);

    if (!program)
        return LogAndMakeError("Ошибка компиляции скрипта (program == nullptr): {}", path.data());

    if (program->failed()) {
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

    auto* new_ctx = config_.create_context(program->getContextStackSize());
    if (!new_ctx)
        return LogAndMakeError("Фабрика контекста вернула nullptr: {}", path.data());

    if (!program->simulate(*new_ctx, tout)) {
        delete new_ctx;
        return LogAndMakeError("Ошибка симуляции контекста! {}", tout.c_str());
    }

    result.program = eastl::move(program);
    result.context.reset(new_ctx);
    return result;
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
    auto compiled = CompileMainScript(path);
    if (!compiled.has_value())
        return std::unexpected(std::move(compiled.error()));

    // Предыдущее поколение (если LoadMainScript зовут повторно) живёт до установки нового.
    std::unique_ptr<das::Context> old_context(das_ctx_);
    das::ProgramPtr old_program = eastl::move(program_);
    std::unique_ptr<das::ModuleGroup> old_group = eastl::move(module_group_);

    das_ctx_ = compiled->context.release();
    program_ = eastl::move(compiled->program);
    module_group_ = eastl::move(compiled->module_group);
    main_script_path_.assign(path.data(), path.size());

    old_context.reset();
    old_program = nullptr;
    old_group.reset();

    LogInfo(LogCategory::Script, "Скрипт {} успешно скомпилирован", path.data());
    return {};
}

Result<void> ScriptSystem::ReloadMainScript() {
    if (!reload_extension_)
        return LogAndMakeError("Горячая перезагрузка не подключена");
    if (main_script_path_.empty())
        return LogAndMakeError("Главный скрипт ещё не загружен");
    if (!das_ctx_)
        return LogAndMakeError("Нет активного контекста скриптов");

    // Пока новый скрипт компилируется и симулируется, старый продолжает работать:
    // ошибка компиляции не должна ронять редактор.
    auto compiled = CompileMainScript(main_script_path_);
    if (!compiled.has_value()) {
        reload_extension_->OnReloadFinished(false, compiled.error().Message());
        return std::unexpected(std::move(compiled.error()));
    }

    // Снимок состояния делает старый контекст, пока он ещё цел.
    eastl::string hook_errors;
    InvokeReloadHooks(das_ctx_, "__before_reload_", hook_errors);

    std::unique_ptr<das::Context> old_context(das_ctx_);
    das::ProgramPtr old_program = eastl::move(program_);
    std::unique_ptr<das::ModuleGroup> old_group = eastl::move(module_group_);

    das_ctx_ = compiled->context.release();
    program_ = eastl::move(compiled->program);
    module_group_ = eastl::move(compiled->module_group);

    // Точки входа хоста повторяются на новом контексте: TimeInit/SetInputSource,
    // подписки редактора, начальная сцена. [after_reload] затем возвращает состояние.
    reload_extension_->ReinitializeContext(*this);
    das_ctx_->restart();

    InvokeReloadHooks(das_ctx_, "__after_reload_", hook_errors);

    // Старое поколение уходит последним: его контекст ссылается на программу и модули.
    old_context.reset();
    old_program = nullptr;
    old_group.reset();

    reload_extension_->OnReloadFinished(true, hook_errors);
    if (hook_errors.empty())
        LogInfo(LogCategory::Script, "Скрипты перезагружены: {}", main_script_path_);
    else
        LogError(LogCategory::Script, "Перезагрузка скриптов завершилась с ошибками: {}", hook_errors);
    return {};
}

}  // namespace tryengine::core
