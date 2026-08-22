#include "engine/core/ScriptSystem.hpp"

#include <daScript/ast/dyn_modules.h>
#include <daScript/simulate/aot.h>
#include <daScript/simulate/runtime_array.h>
#include <set>

#include "engine/core/Assert.hpp"
#include "engine/core/Engine.hpp"

DECLARE_MODULE(Module_Renderer);
DECLARE_MODULE(Module_TryEditor);
DECLARE_MODULE(Module_Resources);
DECLARE_MODULE(Module_Input);

inline void InitializeDaScriptModules() {
    NEED_ALL_DEFAULT_MODULES;
    NEED_MODULE(Module_Renderer);
    NEED_MODULE(Module_TryEditor);
    NEED_MODULE(Module_Resources);
    NEED_MODULE(Module_Input);
}

namespace tryengine::core {

class TrackingFileAccess : public das::FsFileAccess {
public:
    // Передаем скомпилированную программу проекта в базовый конструктор
    TrackingFileAccess(const das::string& pak, const das::smart_ptr<das::Program>& project_program)
        : FsFileAccess(pak, project_program) {}

    das::FileInfo* getNewFileInfo(const das::string& file_name) override {
        das::FileInfo* info = FsFileAccess::getNewFileInfo(file_name);
        if (info)
            tracked_files.insert(file_name.c_str());

        return info;
    }

    std::set<std::string> tracked_files;
};

ScriptSystem::ScriptSystem(Engine& enigne, const std::string& path) : engine_(enigne) {
    das::setDasRoot(DAS_ROOT_DIR);
    InitializeDaScriptModules();

    das::TextPrinter tout;
    das::vector<das::string> load_modules;
    das::ModuleGroup dummyLibGroup;

    // Компилируем проект для инициализации динамических модулей
    auto base_access = das::make_smart<das::FsFileAccess>();
    auto project_program = das::compileDaScript("project.das_project", base_access, tout, dummyLibGroup);

    das::smart_ptr<das::FsFileAccess> file_access;
    if (project_program && !project_program->failed())
        file_access = das::make_smart<das::FsFileAccess>("project.das_project", project_program);
    else {
        LogError(LogCategory::Script, "Не удалось загрузить project.das_project. Используется дефолтный доступ.");
        file_access = das::make_smart<das::FsFileAccess>();
    }

    das::require_dynamic_modules(file_access, das::getDasRoot(), "", load_modules, tout);

    das::Module::Initialize();

    if (!path.empty()) {
        LoadMainScript(path);
    }
}

ScriptSystem::~ScriptSystem() {
    fn_start = nullptr;
    fn_update = nullptr;

    if (das_ctx) {
        delete das_ctx;
        das_ctx = nullptr;
    }
    das::Module::Shutdown();
}

Result<void> ScriptSystem::LoadMainScript(const std::string& path) {
    main_script_path_ = path;
    return CompileAndLoad(path);
}

Result<void> ScriptSystem::CompileAndLoad(const std::string& path) {
    das::TextPrinter tout;
    das::ModuleGroup dummy_lib_group;
    das::CodeOfPolicies policies;

    policies.jit_enabled = true;
    policies.jit_jit_all_functions = true;

    // Шаг 1: Компилируем сам файл конфигурации проекта .das_project
    auto base_access = das::make_smart<das::FsFileAccess>();
    auto project_program = das::compileDaScript("project.das_project", base_access, tout, dummy_lib_group, policies);

    if (!project_program || project_program->failed())
        return LogAndMakeError("Ошибка компиляции файла проекта project.das_project :", tout.str());

    // Шаг 2: Передаем программу проекта в твой кастомный TrackingFileAccess
    const auto tracking_file_access = das::make_smart<TrackingFileAccess>("project.das_project", project_program);
    auto program = das::compileDaScript(path, tracking_file_access, tout, dummy_lib_group, policies);

    if (program->failed()) {
        for (const auto& error : program->errors)
            LogError(reportError(error.at, error.what, error.extra, error.fixme, error.cerr).c_str());

        return Error("Ошибка компиляции скрипта");
    }

    auto new_ctx = CreateContext(program->getContextStackSize());

    if (!program->simulate(*new_ctx, tout)) {
        delete new_ctx;
        return LogAndMakeError("Ошибка симуляции контекста!");
    }

    if (das_ctx)
        delete das_ctx;

    das_ctx = new_ctx;

    fn_start = das_ctx->findFunction("start");
    fn_update = das_ctx->findFunction("update");

    for (const auto& file_path : tracking_file_access->tracked_files) {
        std::error_code ec;
        auto last_write = std::filesystem::last_write_time(file_path, ec);
        if (!ec)
            file_watch_map_[file_path] = last_write;
    }

    LogInfo(LogCategory::Script, "Скрипт успешно скомпилирован");
    return {};
}

das::Context* ScriptSystem::CreateContext(uint32_t stack_size) {
    if (context_factory_) {
        return context_factory_(engine_, stack_size);
    }
    // Дефолтное создание для standalone-игры без редактора
    return new TryengineContext(engine_, stack_size);
}

void ScriptSystem::CheckForReload(float dt) {
    reload_timer_ += dt;
    if (reload_timer_ < 0.5f)
        return;
    reload_timer_ = 0.0f;

    bool need_reload = false;

    // Если первая компиляция упала и карта пуста — принудительно ставим на слежку главный файл
    if (file_watch_map_.empty() && std::filesystem::exists(main_script_path_)) {
        std::error_code ec;
        auto current_time = std::filesystem::last_write_time(main_script_path_, ec);
        if (!ec) {
            file_watch_map_[main_script_path_] = current_time;
            need_reload = true;
        }
    }

    // Проверяем изменения во всех зависимостях скрипта
    for (auto& [path, last_time] : file_watch_map_) {
        if (std::filesystem::exists(path)) {
            std::error_code ec;
            auto current_time = std::filesystem::last_write_time(path, ec);
            if (!ec && last_time != current_time) {
                last_time = current_time;  // Фиксируем изменение сразу, предотвращая бесконечные циклы
                need_reload = true;
            }
        }
    }

    if (!need_reload)
        return;

    LogTrace(LogCategory::Script, "Изменение обнаружено. Перезагрузка скриптов...");

    // Шаг 1: Сохраняем состояние @live переменных текущего контекста
    InvokeHook("__before_reload_live_vars");

    // Шаг 2: Пробуем скомпилировать
    if (CompileAndLoad(main_script_path_)) {
        // Успех: сбрасываем заморозку, восстанавливаем переменные в новый контекст и вызываем старт
        if (is_frozen_) {
            LogTrace(LogCategory::Script, " Ошибки исправлены! Размораживаем выполнение скрипта");
            is_frozen_ = false;
        }
        InvokeHook("__after_reload_live_vars");
        InvokeStart();
    } else {
        // Ошибка: обрабатываем в зависимости от выбранной политики
        if (error_policy_ == ReloadErrorPolicy::FreezeExecution) {
            is_frozen_ = true;
            LogError(LogCategory::Script, " Выполнение заблокировано (ПАУЗА) до исправления ошибок");
        } else {
            is_frozen_ = false;
            LogError(LogCategory::Script, " Продолжаем работу на старом контексте. Ждем исправления");
        }
    }
}

void ScriptSystem::InvokeHook(const std::string& hook_substring) {
    TRY_CHECK(das_ctx, "Нет контекста");

    for (int i = 0; i < das_ctx->getTotalFunctions(); ++i) {
        auto* fn = das_ctx->getFunction(i);
        if (fn && fn->name) {
            std::string name = fn->name;
            // Ищем подстроку, чтобы успешно находить хуки внутри пространств имен (например,
            // `live_vars::__before_reload_live_vars`)
            if (name.find(hook_substring) != std::string::npos) {
                LogTrace(LogCategory::Script, " Вызов хука: {}", name);
                // Хуки живут внутри Live Coding, падение здесь не должно ронять редактор —
                // используем безопасный вызов с логированием.
                InvokeSimFunctionSafe(fn, name);
            }
        }
    }
}

void ScriptSystem::InvokeStart() {
    TRY_CHECK(das_ctx, "Нет контекста");
    TRY_CHECK(fn_start, "Не найдена функция start");

    InvokeSimFunctionSafe(fn_start, "start");
}

void ScriptSystem::InvokeUpdate(float time) {
    if (is_frozen_)
        return;

    if (das_ctx && fn_update) {
        InvokeSimFunctionSafe(fn_update, "update",time);
    }
}

das::Context* ScriptSystem::GetContext() {
    return das_ctx;
}

}  // namespace tryengine::core