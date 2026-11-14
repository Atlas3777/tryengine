#include <daScript/daScript.h>
#include <imgui.h>
#include <thread>

#include "editor/AssetPipeline.hpp"
#include "editor/BuildSystem.hpp"
#include "editor/TryEditorContext.hpp"
#include "editor/render/EditorRender.hpp"
#include "editor/render/ImGuiPass.hpp"
#include "engine/core/ScriptBuildSystem.hpp"
#include "engine/graphics/rg/RenderGraph.hpp"
#include "engine/resources/AsyncFileManager.hpp"

using UiFolderVector = eastl::vector<tryeditor::UiFolder>;
using UiFileVector = eastl::vector<tryeditor::UiFile>;
using Uint32Vector = eastl::vector<uint32_t>;

MAKE_TYPE_FACTORY(AssetItem, tryeditor::UiFile);
MAKE_TYPE_FACTORY(UiFolder, tryeditor::UiFolder);

MAKE_TYPE_FACTORY(UiFolderVector, UiFolderVector);
MAKE_TYPE_FACTORY(UiFileVector, UiFileVector);
MAKE_TYPE_FACTORY(Uint32Vector, Uint32Vector);

const UiFolderVector& GetEngineFolders(das::Context* ctx) {
    auto try_ctx = static_cast<tryeditor::TryEditorContext*>(ctx);
    return try_ctx->editor.GetAssetSourceDatabase().GetFolders(tryeditor::AssetPipeline::StorageDomain::Engine);
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

    tryengine::async::RunAndForget(tryengine::async::ThreadPool(),
                                   build_system.Build(settings, scene_dtos, build_path));
}

namespace {

void WaitForFileHandle(tryengine::resources::AsyncFileManager& file_manager, tryengine::resources::FileHandle& handle) {
    file_manager.Submit();
    while (!handle.IsReady() && !handle.IsFailed()) {
        file_manager.Pull();
        file_manager.Submit();
        std::this_thread::yield();
    }
}

}  // namespace

bool BuildScriptPak(const char* entry_path, const char* output_path, das::Context* ctx) {
    auto* try_ctx = static_cast<tryeditor::TryEditorContext*>(ctx);
    if (!try_ctx || !entry_path || !output_path) {
        LogError("BuildScriptPak: некорректный контекст или путь");
        return false;
    }

    auto* build_system = try_ctx->engine.TryGet<tryengine::core::ScriptBuildSystem>();
    if (!build_system) {
        LogError("BuildScriptPak: ScriptBuildSystem не зарегистрирован");
        return false;
    }

    auto bytes = build_system->BuildPak(entry_path);
    if (!bytes.has_value()) {
        LogError("BuildScriptPak: не удалось собрать '{}'", entry_path);
        return false;
    }

    auto& file_manager = try_ctx->engine.Get<tryengine::resources::AsyncFileManager>();
    auto handle =
        file_manager.WriteChunkAsyncCopy(output_path, eastl::span<const uint8_t>(bytes->data(), bytes->size()));
    if (!handle.IsValid()) {
        LogError("BuildScriptPak: не удалось начать запись '{}'", output_path);
        return false;
    }

    WaitForFileHandle(file_manager, handle);
    if (handle.IsFailed()) {
        LogError("BuildScriptPak: ошибка записи '{}'", output_path);
        return false;
    }

    LogInfo(LogCategory::Script, "script_pak собран: {}", output_path);
    return true;
}

bool TestCompileScriptPak(const das::TArray<uint8_t>& data, das::Context* ctx) {
    auto* try_ctx = static_cast<tryeditor::TryEditorContext*>(ctx);
    if (!try_ctx) {
        LogError("TestCompileScriptPak: некорректный контекст");
        return false;
    }

    auto* build_system = try_ctx->engine.TryGet<tryengine::core::ScriptBuildSystem>();
    if (!build_system) {
        LogError("TestCompileScriptPak: ScriptBuildSystem не зарегистрирован");
        return false;
    }

    const auto* bytes = reinterpret_cast<const uint8_t*>(data.data);
    const auto result = build_system->TestCompile(eastl::span<const uint8_t>(bytes, data.size));
    if (!result.has_value()) {
        LogError("TestCompileScriptPak: тест-компиляция не прошла");
        return false;
    }
    return true;
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
    UiFileAnnotation(ModuleLibrary& ml) : ManagedStructureAnnotation("AssetItem", ml) {
        addField<DAS_BIND_MANAGED_FIELD(guid)>("guid", "guid");
        addField<DAS_BIND_MANAGED_FIELD(name)>("name", "name");
        addField<DAS_BIND_MANAGED_FIELD(path)>("path", "path");
    }
};

struct UiFolderAnnotation : ManagedStructureAnnotation<tryeditor::UiFolder, false> {
    UiFolderAnnotation(ModuleLibrary& ml) : ManagedStructureAnnotation("UiFolder", ml) {
        addField<DAS_BIND_MANAGED_FIELD(subfolders)>("subfolders", "subfolders");
        addField<DAS_BIND_MANAGED_FIELD(files)>("files", "files");
        addField<DAS_BIND_MANAGED_FIELD(name)>("name", "name");
        addField<DAS_BIND_MANAGED_FIELD(path)>("path", "path");
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
        addVectorAnnotation<UiFolderVector>(this, lib, "UiFolderVector");

        das::addExtern<DAS_BIND_FUN(GetEditorCameraViewImage)>(*this, lib, "GetEditorCameraViewImage",
                                                               das::SideEffects::none, "GetEditorCameraViewImage");

        das::addExtern<DAS_BIND_FUN(GetGameCameraViewImage)>(*this, lib, "GetGameCameraViewImage",
                                                             das::SideEffects::none, "GetGameCameraViewImage");

        das::addExtern<DAS_BIND_FUN(GetEditorImGuiContext)>(*this, lib, "GetEditorImGuiContext",
                                                            das::SideEffects::none, "GetEditorImGuiContext");

        das::addExtern<DAS_BIND_FUN(GetEngineFolders)>(*this, lib, "get_engine_folders", das::SideEffects::none,
                                                       "GetEngineFolders");

        das::addExtern<DAS_BIND_FUN(GetGameFolders)>(*this, lib, "get_game_folders", das::SideEffects::none,
                                                     "GetGameFolders");

        das::addExtern<DAS_BIND_FUN(CreateBuild)>(*this, lib, "CreateBuild", das::SideEffects::accessGlobal,
                                                  "CreateBuild");

        das::addExtern<DAS_BIND_FUN(BuildScriptPak)>(*this, lib, "BuildScriptPak", das::SideEffects::accessGlobal,
                                                     "BuildScriptPak");

        das::addExtern<DAS_BIND_FUN(TestCompileScriptPak)>(*this, lib, "TestCompileScriptPak",
                                                           das::SideEffects::accessGlobal, "TestCompileScriptPak");

        verifyAotReady();
    }
};

REGISTER_DYN_MODULE(Module_TryEditor, Module_TryEditor);
REGISTER_MODULE(Module_TryEditor);