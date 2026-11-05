#pragma once

#include <EASTL/functional.h>
#include <EASTL/span.h>
#include <EASTL/vector.h>
#include <daScript/daScript.h>

#include "engine/core/Engine.hpp"
#include "engine/core/GameScriptFileAccess.hpp"
#include "engine/core/ScriptBuildSystem.hpp"
#include "engine/core/ScriptPolicies.hpp"
#include "engine/core/ScriptSystem.hpp"
#include "engine/core/TryengineContext.hpp"

// DECLARE_MODULE — на глобальном уровне
DECLARE_MODULE(Module_Renderer);
DECLARE_MODULE(Module_Resources);
DECLARE_MODULE(Module_Input);
DECLARE_MODULE(Module_AsyncFileManager);

// Функция регистрации ДОЛЖНА БЫТЬ в глобальном namespace ::
inline void RegisterGameScriptModules() {
    das::setDasRoot("./");

    NEED_ALL_DEFAULT_MODULES;
    PULL_MODULE(Module_Renderer);
    PULL_MODULE(Module_Resources);
    PULL_MODULE(Module_Input);
    PULL_MODULE(Module_AsyncFileManager);
}

// Фабрика контекста игры. Используется и в игре, и в редакторе (для ScriptBuildSystem).
inline eastl::function<das::Context*(uint32_t)> MakeGameContextFactory(tryengine::core::Engine& engine) {
    return [&engine](uint32_t stack_size) -> das::Context* {
        return new tryengine::core::TryengineContext(engine, stack_size);
    };
}

// Конфиг игры: скрипты читаются из пака, байты которого уже загружены.
inline tryengine::core::ScriptSystemConfig GetGameScriptConfig(tryengine::core::Engine& engine,
                                                               eastl::vector<uint8_t> pak_bytes) {
    return tryengine::core::ScriptSystemConfig{
        .register_modules = &::RegisterGameScriptModules,
        .create_file_access = [bytes = eastl::move(pak_bytes)]() -> das::smart_ptr<das::FileAccess> {
            auto pak = tryengine::core::DeserializeScriptPak(eastl::span(bytes.data(), bytes.size()));
            if (!pak.has_value()) {
                LogError("DeserializeScriptPak Falied");
                return das::smart_ptr<das::FileAccess>();
            }
            return das::make_smart<tryengine::core::PackedFileAccess>(eastl::move(*pak));
        },
        // .prepare_file_access = [](das::FileAccess& file_access) {
        //     file_access.addExtraModule("just_in_time", "just_in_time");
        // },
        .create_context = MakeGameContextFactory(engine),
        .policies = tryengine::core::CreatePakPolicies(),
    };
}

// Конфиг сборщика пака (для редактора).
inline tryengine::core::ScriptBuildConfig GetGameScriptBuildConfig(tryengine::core::Engine& engine) {
    return tryengine::core::ScriptBuildConfig{
        .project_file = "project.das_project",
        .create_context = MakeGameContextFactory(engine),
    };
}