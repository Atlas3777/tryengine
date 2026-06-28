#include <daScript/daScript.h>

#include "imgui.h"

void* GetEditorImGuiContext() {
    return static_cast<void*>(ImGui::GetCurrentContext());
}

class Module_TryEditor : public das::Module {
public:
    Module_TryEditor() : das::Module("tryeditor") {
        das::ModuleLibrary lib(this);

        // Регистрируем обновленную функцию
        das::addExtern<DAS_BIND_FUN(GetEditorImGuiContext)>(*this, lib, "get_editor_imgui_context", 
            das::SideEffects::none, "GetEditorImGuiContext");
        
        verifyAotReady();
    }
};

REGISTER_DYN_MODULE(Module_TryEditor, Module_TryEditor);
REGISTER_MODULE(Module_TryEditor);