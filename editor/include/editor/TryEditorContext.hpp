#pragma once

#include "Editor.hpp"
#include "engine/core/ScriptSystem.hpp"

namespace tryeditor {

class TryEditorContext : public tryengine::core::TryengineContext {
public:
    Editor& editor;

    TryEditorContext(tryengine::core::Engine& eng, Editor& ed, uint32_t stackSize = 16 * 1024, bool ph = false)
        : TryengineContext(eng, stackSize, ph), editor(ed) {}
};

} // namespace tryeditor