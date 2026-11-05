#pragma once

#include <daScript/daScript.h>

#include "editor/Editor.hpp"
#include "editor/TryEditorContext.hpp"
#include "engine/core/Engine.hpp"
#include "engine/core/FsFileAccessFactory.hpp"
#include "engine/core/ScriptPolicies.hpp"
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
    das::setDasRoot(DAS_ROOT_DIR);

    NEED_ALL_DEFAULT_MODULES;
    PULL_MODULE(Module_Renderer);
    PULL_MODULE(Module_Resources);
    PULL_MODULE(Module_Input);
    PULL_MODULE(Module_TryEditor);
    PULL_MODULE(Module_AsyncFileManager);
}

namespace tryeditor {

inline tryengine::core::ScriptSystemConfig GetEditorScriptConfig(tryengine::core::Engine& engine,
                                                                 tryeditor::Editor& editor) {
    return tryengine::core::ScriptSystemConfig{
        .register_modules = &::RegisterEditorScriptModules,
        .create_file_access =
            []() -> das::smart_ptr<das::FileAccess> {
                return tryengine::core::MakeFsFileAccess("project.das_project");
        },
        .create_context =
            [&engine, &editor](uint32_t stack_size) -> das::Context* {
                return new TryEditorContext(engine, editor, stack_size);
        },
        .policies = tryengine::core::CreateHeavyJitPolicies(),
    };
}


}  // namespace tryeditor