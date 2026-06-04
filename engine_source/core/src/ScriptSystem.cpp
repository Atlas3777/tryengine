#include "engine/core/ScriptSystem.hpp"

#include <daScript/daScript.h>
#include <daScript/simulate/aot.h>
#include <daScript/ast/dyn_modules.h>
#include <iostream>

inline void InitializeDaScriptModules() {
    NEED_ALL_DEFAULT_MODULES;
}

namespace tryengine::core {

ScriptSystem::ScriptSystem() {
    das::setDasRoot(DAS_ROOT_DIR);
    InitializeDaScriptModules();

    das::TextPrinter tout;
    das::vector<das::string> load_modules;
    auto fAccess = das::make_smart<das::FsFileAccess>();

    das::require_dynamic_modules(fAccess, das::getDasRoot(), "./", load_modules, tout);

    das::Module::Initialize();
}

ScriptSystem::~ScriptSystem() {
    fn_start = nullptr;
    fn_update = nullptr;

    if (das_ctx) {
        delete das_ctx;
        das_ctx = nullptr;
    }

    // Очищаем кастомные скрипты эдитора/систем
    custom_scripts_.clear();

    das::ModuleGroup dummyLibGroup;
    das::Module::Shutdown();
}

bool ScriptSystem::LoadMainScript(const std::string& path) {
    das::TextPrinter tout;
    das::ModuleGroup dummyLibGroup;

    // Инициализируем FsFileAccess с указанием нашего .das_project файла
    auto fAccess = das::make_smart<das::FsFileAccess>("project.das_project", das::make_smart<das::FsFileAccess>());

    auto program = das::compileDaScript(path, fAccess, tout, dummyLibGroup);
    if (!program->failed()) {
        das_ctx = new das::Context(program->getContextStackSize());
        if (program->simulate(*das_ctx, tout)) {
            fn_start = das_ctx->findFunction("start");
            fn_update = das_ctx->findFunction("update");
            return true;
        }
    }

    std::cerr << "daScript compilation failed:\n" << tout.str() << std::endl;
    return false;
}

bool ScriptSystem::LoadCustomScript(const std::string& id, const std::string& path) {
    das::TextPrinter tout;
    das::ModuleGroup dummyLibGroup;

    // Делаем то же самое для кастомных скриптов (например, editor.das)
    auto fAccess = das::make_smart<das::FsFileAccess>("project.das_project", das::make_smart<das::FsFileAccess>());

    auto program = das::compileDaScript(path, fAccess, tout, dummyLibGroup);
    if (!program->failed()) {
        auto ctx = new das::Context(program->getContextStackSize());
        if (program->simulate(*ctx, tout)) {

            auto instance = std::make_unique<ScriptInstance>();
            instance->context = ctx;

            custom_scripts_[id] = std::move(instance);
            return true;
        }
    }
    if (!program || program->failed()) {
        std::cout << tout.str() << "\n";

        // ИСПРАВЛЕННЫЙ БЛОК:
        if (program) {
            for (const auto& error : program->errors) {
                std::cerr << reportError(error.at, error.what, error.extra, error.fixme, error.cerr) << "\n";
            }
        }
    }
    return false;
}

// Реализация простого вызова функции
void ScriptSystem::InvokeCustomFunction(const std::string& id, const std::string& function_name) {
    auto it = custom_scripts_.find(id);
    if (it != custom_scripts_.end() && it->second->context) {
        auto* ctx = it->second->context;
        if (auto* fn = ctx->findFunction(function_name.c_str())) {
            das::Func func(fn);
            das::das_invoke_function<void>::invoke(ctx, nullptr, func);
        }
    }
}

void ScriptSystem::InvokeStart() {
    if (das_ctx && fn_start) {
        das::Func start_func(fn_start);
        das::das_invoke_function<void>::invoke(das_ctx, nullptr, start_func);
    }
}

void ScriptSystem::InvokeUpdate(float dt) {
    if (das_ctx && fn_update) {
        das::Func update_func(fn_update);
        das::das_invoke_function<void>::invoke(das_ctx, nullptr, update_func, dt);
    }
}

das::Context* ScriptSystem::GetContext() {
    return das_ctx;
}

}  // namespace tryengine::core