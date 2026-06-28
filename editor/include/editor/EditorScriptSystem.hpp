#pragma once

#include <daScript/daScript.h>
#include <daScript/simulate/simulate.h>

namespace editor {
class EditorScriptSystem {
public:
    EditorScriptSystem(const das::Context& master_contex) { clone_context_ = new das::Context(master_contex, 0); }
    void Hierarchy() {}

private:
    das::Context* clone_context_;
};
}  // namespace editor
