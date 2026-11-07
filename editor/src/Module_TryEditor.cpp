#include <daScript/daScript.h>
#include <imgui.h>

#include "editor/AssetPipeline.hpp"
#include "editor/BuildSystem.hpp"
#include "editor/TryEditorContext.hpp"
#include "editor/render/EditorRender.hpp"
#include "editor/render/ImGuiPass.hpp"
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

MAKE_TYPE_FACTORY(UiFileTest, tryeditor::UiFileTest);

const UiFolderVector& GetEngineFolders(das::Context* ctx) {
    auto try_ctx = static_cast<tryeditor::TryEditorContext*>(ctx);
    return try_ctx->editor.GetAssetSourceDatabase().GetFolders(tryeditor::AssetPipeline::StorageDomain::Engine);
}

tryeditor::UiFileTest CreateUiFileTest() {
    return tryeditor::UiFileTest(3);
}

const UiFolderVector& GetGameFolders(das::Context* ctx) {
    auto try_ctx = static_cast<tryeditor::TryEditorContext*>(ctx);
    return try_ctx->editor.GetAssetSourceDatabase().GetFolders(tryeditor::AssetPipeline::StorageDomain::Game);
}

struct SceneDTODas {
    const char* name;
    das::Array meshes;
    das::Array mats;
};

eastl::vector<tryeditor::SceneDTO> scene_dtos;

void CreateBuild(const char* build_path, das::Array* scenes_ptr, das::Context* ctx) {
    auto try_ctx = static_cast<tryeditor::TryEditorContext*>(ctx);
    auto& build_system = try_ctx->engine.Get<tryeditor::BuildSystem>();

    scene_dtos.clear();

    LogInfo("CreateBuild CPP");

    tryeditor::BuildSettings settings{};

    auto array = *scenes_ptr;

    SceneDTODas* scene_dto = reinterpret_cast<SceneDTODas*>(array.data);
    eastl::span<SceneDTODas> scenes = eastl::span(scene_dto, scenes_ptr->size);
    for (const auto& scene : scenes) {
        tryeditor::SceneDTO dto;
        const uint64_t* mesh_ptr = reinterpret_cast<const uint64_t*>(scene.meshes.data);
        dto.meshes = eastl::vector<uint64_t>(mesh_ptr, mesh_ptr + scene.meshes.size);

        const uint64_t* mat_ptr = reinterpret_cast<const uint64_t*>(scene.mats.data);
        dto.mats = eastl::vector<uint64_t>(mat_ptr, mat_ptr + scene.mats.size);
        dto.name = scene.name;

        scene_dtos.emplace_back(eastl::move(dto));
    }
    for (auto& scene : scene_dtos) {
        for (auto& mesh : scene.meshes)
            LogInfo("MESH {}", mesh);
        for (auto& mat : scene.mats)
            LogInfo("MAT {}", mat);
    }

    tryengine::async::RunAndForget(tryengine::async::ThreadPool(), build_system.Build(settings, scene_dtos, build_path));
}

void* GetEditorImGuiContext() {
    return ImGui::GetCurrentContext();
}

void* GetEditorCameraViewImage(das::Context* ctx) {
    auto* try_ctx = static_cast<tryeditor::TryEditorContext*>(ctx);
    if (!try_ctx)
        return nullptr;

    auto& rg = try_ctx->engine.Get<tryengine::graphics::RenderGraph>();
    return rg.Get<tryeditor::ImGuiPass>(tryeditor::EditorRender::kEditorScope)->GetEditorViewportTexture();
}

void* GetGameCameraViewImage(das::Context* ctx) {
    auto* try_ctx = static_cast<tryeditor::TryEditorContext*>(ctx);
    if (!try_ctx)
        return nullptr;

    auto& rg = try_ctx->engine.Get<tryengine::graphics::RenderGraph>();
    return rg.Get<tryeditor::ImGuiPass>(tryeditor::EditorRender::kEditorScope)->GetGameViewportTexture();
}

using namespace das;

struct UiFileAnnotation : ManagedStructureAnnotation<tryeditor::UiFile, false> {
    UiFileAnnotation(ModuleLibrary& ml) : ManagedStructureAnnotation("UiFile", ml) {
        addField<DAS_BIND_MANAGED_FIELD(guid)>("guid", "guid");
        addProperty<DAS_BIND_MANAGED_PROP(GetName)>("name", "GetName");
        addProperty<DAS_BIND_MANAGED_PROP(GetPath)>("path", "GetPath");
    }
};

struct UiFolderAnnotation : ManagedStructureAnnotation<tryeditor::UiFolder, false> {
    UiFolderAnnotation(ModuleLibrary& ml) : ManagedStructureAnnotation("UiFolder", ml) {
        addField<DAS_BIND_MANAGED_FIELD(subfolders)>("subfolders", "subfolders");
        addField<DAS_BIND_MANAGED_FIELD(files)>("files", "files");
        addProperty<DAS_BIND_MANAGED_PROP(GetName)>("name", "GetName");
        addProperty<DAS_BIND_MANAGED_PROP(GetPath)>("path", "GetPath");
    }
};

struct UiFileTestAnnotation : ManagedStructureAnnotation<tryeditor::UiFileTest, false> {
    UiFileTestAnnotation(ModuleLibrary& ml) : ManagedStructureAnnotation("UiFileTest", ml) {
        addField<DAS_BIND_MANAGED_FIELD(guid)>("guid", "guid");
    }
};

class Module_TryEditor : public das::Module {
public:
    Module_TryEditor() : das::Module("TryEditor") {
        das::ModuleLibrary lib(this);

        addAnnotation(new UiFileAnnotation(lib));

        addVectorAnnotation<UiFileVector>(this, lib, "UiFileVector");
        addVectorAnnotation<Uint32Vector>(this, lib, "Uint32Vector");

        addAnnotation(new UiFolderAnnotation(lib));
        addAnnotation(new UiFileTestAnnotation(lib));
        addVectorAnnotation<UiFolderVector>(this, lib, "UiFolderVector");

        das::addExtern<DAS_BIND_FUN(GetEditorCameraViewImage)>(*this, lib, "GetEditorCameraViewImage",
                                                               das::SideEffects::none, "GetEditorCameraViewImage");

        das::addExtern<DAS_BIND_FUN(GetGameCameraViewImage)>(*this, lib, "GetGameCameraViewImage",
                                                       das::SideEffects::none, "GetGameCameraViewImage");

        das::addExtern<DAS_BIND_FUN(GetEditorImGuiContext)>(*this, lib, "get_editor_imgui_context",
                                                            das::SideEffects::none, "GetEditorImGuiContext");

        das::addExtern<DAS_BIND_FUN(GetEngineFolders)>(*this, lib, "get_engine_folders", das::SideEffects::none,
                                                       "GetEngineFolders");

        das::addExtern<DAS_BIND_FUN(GetGameFolders)>(*this, lib, "get_game_folders", das::SideEffects::none,
                                                     "GetGameFolders");

        das::addExtern<DAS_BIND_FUN(CreateUiFileTest), das::SimNode_ExtFuncCallAndCopyOrMove>(
            *this, lib, "CreateUiFileTest", das::SideEffects::none, "CreateUiFileTest");

        das::addExtern<DAS_BIND_FUN(CreateBuild)>(*this, lib, "CreateBuild", das::SideEffects::accessGlobal,
                                                  "CreateBuild");

        verifyAotReady();
    }
};

REGISTER_DYN_MODULE(Module_TryEditor, Module_TryEditor);
REGISTER_MODULE(Module_TryEditor);