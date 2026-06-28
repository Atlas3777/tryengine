#include <daScript/daScript.h>
#include <imgui.h>

// Возвращаем void* вместо uint64_t для более удобного каста в скрипте
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

// --- ИСПРАВЛЕНИЕ: Макросы должны быть строго в глобальной области видимости ---
// REGISTER_DYN_MODULE нужен для корректной работы DLL/Dyn рантайма
REGISTER_DYN_MODULE(Module_TryEditor, Module_TryEditor);
REGISTER_MODULE(Module_TryEditor);