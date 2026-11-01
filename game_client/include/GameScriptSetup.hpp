#pragma once

#include <daScript/daScript.h>

#include "engine/core/Engine.hpp"
#include "engine/core/ScriptSystem.hpp"
#include "engine/core/TryengineContext.hpp"

// DECLARE_MODULE — на глобальном уровне
DECLARE_MODULE(Module_Renderer);
DECLARE_MODULE(Module_Resources);
DECLARE_MODULE(Module_Input);
DECLARE_MODULE(Module_AsyncFileManager);

// Функция регистрации ДОЛЖНА БЫТЬ в глобальном namespace ::
inline void RegisterGameScriptModules() {
    NEED_ALL_DEFAULT_MODULES;
    PULL_MODULE(Module_Renderer);
    PULL_MODULE(Module_Resources);
    PULL_MODULE(Module_Input);
    PULL_MODULE(Module_AsyncFileManager);
}

namespace trygame {

inline tryengine::core::ScriptSystemConfig GetGameScriptConfig(tryengine::core::Engine& engine) {
    return tryengine::core::ScriptSystemConfig{
        .register_modules = &::RegisterGameScriptModules,
        .create_context   = [&engine](uint32_t stack_size) -> das::Context* {
            return new tryengine::core::TryengineContext(engine, stack_size);
        }
    };

}
} // namespace trygame