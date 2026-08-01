#include <imgui.h>
#include <daScript/daScript.h>

#include "editor/AssetSourceDatabase.hpp"
#include "editor/gui/EditorGUI.hpp"
#include "editor/gui/SceneViewportPanel.hpp"
#include "editor/gui/UiFile.hpp"

using UiFolderVector = eastl::vector<tryeditor::UiFolder>;
using UiFileVector = eastl::vector<tryeditor::UiFile>;
using Uint32Vector = eastl::vector<uint32_t>;

MAKE_TYPE_FACTORY(UiFile, tryeditor::UiFile);
MAKE_TYPE_FACTORY(UiFolder, tryeditor::UiFolder);

MAKE_TYPE_FACTORY(UiFolderVector, UiFolderVector);
MAKE_TYPE_FACTORY(UiFileVector, UiFileVector);
MAKE_TYPE_FACTORY(Uint32Vector, Uint32Vector);

UiFolderVector& GetEngineFolders() {
    return tryeditor::AssetSourceDatabase::GetEngineFolders();
}

const UiFolderVector& GetGameFolders() {
    return tryeditor::AssetSourceDatabase::GetGameFolders();
}

void* GetEditorImGuiContext() {
    return ImGui::GetCurrentContext();
}

void* GetImage() {
    auto* viewport_panel = tryeditor::EditorGUI::GetSceneViewportPanel();
    if (!viewport_panel) return nullptr;

    auto* render_target = viewport_panel->GetTarget();
    if (!render_target) return nullptr;

    return render_target->GetColor();
}

using namespace das;

struct UiFileAnnotation : ManagedStructureAnnotation<tryeditor::UiFile, false> {
    UiFileAnnotation(ModuleLibrary& ml) : ManagedStructureAnnotation("UiFile", ml) {
        addField<DAS_BIND_MANAGED_FIELD(guid)>("guid", "guid");
        addField<DAS_BIND_MANAGED_FIELD(asset_idx)>("asset_idx", "asset_idx");
        addProperty<DAS_BIND_MANAGED_PROP(GetName)>("name", "GetName");
    }
};

struct UiFolderAnnotation : ManagedStructureAnnotation<tryeditor::UiFolder, false> {
    UiFolderAnnotation(ModuleLibrary& ml) : ManagedStructureAnnotation("UiFolder", ml) {
        addField<DAS_BIND_MANAGED_FIELD(subfolders)>("subfolders", "subfolders");
        addField<DAS_BIND_MANAGED_FIELD(files)>("files", "files");
        addProperty<DAS_BIND_MANAGED_PROP(GetName)>("name", "GetName");
    }
};

class Module_TryEditor : public das::Module {
public:
    Module_TryEditor() : das::Module("tryeditor") {
        das::ModuleLibrary lib(this);

        addAnnotation(new UiFileAnnotation(lib));
        addVectorAnnotation<UiFileVector>(this, lib, "UiFileVector");
        addVectorAnnotation<Uint32Vector>(this, lib, "Uint32Vector");

        addAnnotation(new UiFolderAnnotation(lib));
        addVectorAnnotation<UiFolderVector>(this, lib, "UiFolderVector");

        das::addExtern<DAS_BIND_FUN(GetImage)>(*this, lib, "get_image",
                                               das::SideEffects::none, "GetImage");

        das::addExtern<DAS_BIND_FUN(GetEditorImGuiContext)>(*this, lib, "get_editor_imgui_context",
                                                            das::SideEffects::none, "GetEditorImGuiContext");

        // Биндим раздельные функции для Engine и Game ресурсов
        das::addExtern<DAS_BIND_FUN(GetEngineFolders)>(*this, lib, "get_engine_folders",
                                                       das::SideEffects::none, "GetEngineFolders");

        das::addExtern<DAS_BIND_FUN(GetGameFolders)>(*this, lib, "get_game_folders",
                                                     das::SideEffects::none, "GetGameFolders");

        verifyAotReady();
    }
};

REGISTER_DYN_MODULE(Module_TryEditor, Module_TryEditor);
REGISTER_MODULE(Module_TryEditor);