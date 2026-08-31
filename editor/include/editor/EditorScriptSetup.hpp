#pragma once

#include <daScript/daScript.h>

#include "editor/Editor.hpp"
#include "editor/TryEditorContext.hpp"
#include "engine/core/Engine.hpp"
#include "engine/core/ScriptSystem.hpp"

// DECLARE_MODULE — на глобальном уровне
DECLARE_MODULE(Module_Renderer);
DECLARE_MODULE(Module_Resources);
DECLARE_MODULE(Module_Input);
DECLARE_MODULE(Module_TryEditor);
DECLARE_MODULE(Module_AsyncFileManager);

// Функция регистрации ДОЛЖНА БЫТЬ в глобальном namespace ::
// Чтобы NEED_ALL_DEFAULT_MODULES не искал символы внутри tryeditor::
inline void RegisterEditorScriptModules() {
    NEED_ALL_DEFAULT_MODULES;
    PULL_MODULE(Module_Renderer);
    PULL_MODULE(Module_Resources);
    PULL_MODULE(Module_Input);
    PULL_MODULE(Module_TryEditor);
    PULL_MODULE(Module_AsyncFileManager);
}

namespace tryeditor {

inline tryengine::core::ScriptSystemConfig GetEditorScriptConfig(tryengine::core::Engine& engine, tryeditor::Editor& editor) {
    return {
        .register_modules = &::RegisterEditorScriptModules,
        .create_context   = [&engine, &editor](uint32_t stack_size) -> das::Context* {
            return new tryeditor::TryEditorContext(engine, editor, stack_size);
        }
    };
}

}  // namespace tryeditor