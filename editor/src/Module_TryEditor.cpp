#include <daScript/daScript.h>
#include <imgui.h>

#include "editor/AssetPipeline.hpp"
#include "editor/EditorRender.hpp"
#include "editor/TryEditorContext.hpp"
#include "editor/gui/EditorGUI.hpp"
#include "editor/gui/UiFile.hpp"
#include "engine/core/ScriptSystem.hpp"
#include "engine/graphics/rg/RenderGraph.hpp"

using UiFolderVector = eastl::vector<tryeditor::UiFolder>;
using UiFileVector = eastl::vector<tryeditor::UiFile>;
using Uint32Vector = eastl::vector<uint32_t>;

MAKE_TYPE_FACTORY(UiFile, tryeditor::UiFile);
MAKE_TYPE_FACTORY(UiFolder, tryeditor::UiFolder);

MAKE_TYPE_FACTORY(UiFolderVector, UiFolderVector);
MAKE_TYPE_FACTORY(UiFileVector, UiFileVector);
MAKE_TYPE_FACTORY(Uint32Vector, Uint32Vector);

__forceinline UiFolderVector& GetEngineFolders(das::Context* ctx) {
    auto try_ctx = static_cast<tryeditor::TryEditorContext*>(ctx);
    return try_ctx->editor.GetAssetSourceDatabase().engine_folders_;
}

const UiFolderVector& GetGameFolders(das::Context* ctx) {
    auto try_ctx = static_cast<tryeditor::TryEditorContext*>(ctx);
    return try_ctx->editor.GetAssetSourceDatabase().game_folders_;
}

void* GetEditorImGuiContext() {
    return ImGui::GetCurrentContext();
}

void* GetImage(das::Context* ctx) {
    auto* try_ctx = static_cast<tryeditor::TryEditorContext*>(ctx);
    if (!try_ctx)
        return nullptr;

    auto* rg = try_ctx->engine.TryGet<tryengine::graphics::RenderGraph>();
    if (!rg) {
        LogError("Render Graph not Found");
        return nullptr;
    }

    // 1. Извлекаем RGResourceHandle из Blackboard RenderGraph
    tryeditor::RGResourceHandle viewport_handle = rg->GetBlackboard().Get(tryengine::graphics::RGTag_v<"SceneViewport">);

    // 2. Получаем чистый VkImageView из RenderGraph (без зависимости от ImGui)
    VkImageView view = rg->GetPhysicalImageView(viewport_handle);
    if (!view)
        return nullptr;

    // 3. Слой редактора (EditorRender) возвращает кэшированный VkDescriptorSet для ImGui
    VkDescriptorSet ds = try_ctx->engine.Get<tryeditor::EditorRender>().GetOrCreateImguiTexture(view);

    return ds;
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