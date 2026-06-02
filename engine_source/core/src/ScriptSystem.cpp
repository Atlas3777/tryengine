
#include "engine/core/ScriptSystem.hpp"

#include <daScript/daScript.h>
#include <daScript/simulate/aot.h>
#include <iostream>

inline void InitializeDaScriptModules() {
    NEED_ALL_DEFAULT_MODULES;
}

namespace tryengine::core {

ScriptSystem::ScriptSystem() {
    das::setDasRoot(DAS_ROOT_DIR);
    InitializeDaScriptModules();
    das::Module::Initialize();
}

ScriptSystem::~ScriptSystem() {
    fn_start = nullptr;
    fn_update = nullptr;

    if (das_ctx) {
        delete das_ctx;
        das_ctx = nullptr;
    }

    das::ModuleGroup dummyLibGroup;
    das::Module::Shutdown();
}

bool ScriptSystem::LoadMainScript(const std::string& path) {
    das::TextPrinter tout;
    das::ModuleGroup dummyLibGroup;
    auto fAccess = das::make_smart<das::FsFileAccess>();

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