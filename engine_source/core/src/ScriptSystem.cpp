#include "engine/core/ScriptSystem.hpp"

#include <daScript/ast/dyn_modules.h>

namespace tryengine::core {

das::CodeOfPolicies ScriptSystem::CreateHeavyJitPolicies() const {
    das::CodeOfPolicies policies;
    policies.jit_enabled = true;
    policies.jit_dll_mode = true;
    policies.jit_jit_all_functions = true;
    policies.jit_size_level = 0;
    policies.jit_opt_level = 3;
    // policies.log_compile_time = true;
    // policies.log_total_compile_time = true;
    // policies.log_module_compile_time = true;
    return policies;
}

ScriptSystem::ScriptSystem(ScriptSystemConfig config) : config_(eastl::move(config)) {
    LogInfo("START FIRST");
    TRY_ASSERT(config_.create_context, "Фабрика контекста должна быть задана!");
    TRY_ASSERT(config_.register_modules, "Регистрация модулей должна быть задана!");

    das::setDasRoot(DAS_ROOT_DIR);

    // 1. Статическая регистрация C++ модулей (PULL_MODULE / NEED_ALL_DEFAULT_MODULES)
    config_.register_modules();

    das::TextPrinter tout;
    das::ModuleGroup dummy_lib_group;
    policies_ = CreateHeavyJitPolicies();

    // 2. Инициализация FsFileAccess и считывание project.das_project
    if (!config_.project_file.empty()) {
        auto base_access = das::make_smart<das::FsFileAccess>();
        auto project_program =
            das::compileDaScript(config_.project_file.c_str(), base_access, tout, dummy_lib_group, policies_);

        if (project_program && !project_program->failed()) {
            file_access_ = das::make_smart<das::FsFileAccess>(config_.project_file.c_str(), project_program);
        } else {
            LogWarn(LogCategory::Script, "Не удалось загрузить '{}'. Используется стандартный FsFileAccess.",
                    config_.project_file.c_str());
            file_access_ = das::make_smart<das::FsFileAccess>();
        }
    } else {
        file_access_ = das::make_smart<das::FsFileAccess>();
    }

    // 3. Загрузка динамических модулей
    das::vector<das::string> load_modules;
    das::require_dynamic_modules(file_access_, das::getDasRoot(), "", load_modules, tout);

    // 4. Глобальная инициализация модулей daScript
    das::Module::Initialize();

    LogInfo("END FIRST");
}

ScriptSystem::~ScriptSystem() {
    if (das_ctx_) {
        delete das_ctx_;
        das_ctx_ = nullptr;
    }
    das::Module::Shutdown();
}

Result<void> ScriptSystem::LoadMainScript(eastl::string_view path) {
    LogInfo("TWO START");
    das::TextPrinter tout;
    das::ModuleGroup dummy_lib_group;

    // Подключаем JIT-модуль к FileAccess
    file_access_->addExtraModule("just_in_time", das::getDasRoot() + "/daslib/just_in_time.das");

    const auto program = das::compileDaScript(path.data(), file_access_, tout, dummy_lib_group, policies_);

    if (!program || program->failed()) {
        for (const auto& error : program->errors)
            LogError(reportError(error.at, error.what, error.extra, error.fixme, error.cerr).c_str());

        return LogAndMakeError("Ошибка компиляции скрипта: {}", path.data());
    }

    auto* new_ctx = config_.create_context(program->getContextStackSize());

    if (!program->simulate(*new_ctx, tout)) {
        delete new_ctx;
        return LogAndMakeError("Ошибка симуляции контекста! {}", tout.c_str());
    }

    if (das_ctx_)
        delete das_ctx_;

    das_ctx_ = new_ctx;

    LogInfo(LogCategory::Script, "Скрипт {} успешно скомпилирован", path.data());
    return {};
}

}  // namespace tryengine::core