#include "engine/core/ScriptSystem.hpp"

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
    das::Module::Shutdown();
}

Result<void> ScriptSystem::LoadMainScript(eastl::string_view path) {
    das::TextPrinter tout;
    das::ModuleGroup dummy_lib_group;

    if (config_.prepare_file_access)
        config_.prepare_file_access(*file_access_);

    const auto program = das::compileDaScript(path.data(), file_access_, tout, dummy_lib_group, config_.policies);

    if (!program)
        return LogAndMakeError("Ошибка компиляции скрипта (program == nullptr): {}", path.data());

    if (program->failed()) {
        for (const auto& error : program->errors)
            LogError(das::reportError(error.at, error.what, error.extra, error.fixme, error.cerr).c_str());

        return LogAndMakeError("Ошибка компиляции скрипта: {}", path.data());
    }

    for (auto* mod : program->thisModuleGroup->getModules())
        LogInfo(LogCategory::Script, "LOADED module: {} [{}]", mod->name.c_str(), mod->fileName.c_str());

    auto* new_ctx = config_.create_context(program->getContextStackSize());

    if (!program->simulate(*new_ctx, tout)) {
        delete new_ctx;
        return LogAndMakeError("Ошибка симуляции контекста! {}", tout.c_str());
    }

    for (auto* mod : program->thisModuleGroup->getModules())
        LogInfo(LogCategory::Script, "222 LOADED module: {} [{}]", mod->name.c_str(), mod->fileName.c_str());

    if (das_ctx_)
        delete das_ctx_;

    das_ctx_ = new_ctx;

    LogInfo(LogCategory::Script, "Скрипт {} успешно скомпилирован", path.data());
    return {};
}

}  // namespace tryengine::core