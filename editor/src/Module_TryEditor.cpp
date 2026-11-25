#include <cstring>
#include <daScript/daScript.h>
#include <fstream>
#include <imgui.h>
#include <thread>

#include "editor/AssetExt.hpp"
#include "editor/AssetPipeline.hpp"
#include "editor/BuildSystem.hpp"
#include "editor/ScriptReload.hpp"
#include "editor/TryEditorContext.hpp"
#include "editor/import/GltfUpAxis.hpp"
#include "editor/meta/AssetMetaHeader.hpp"
#include "editor/render/EditorRender.hpp"
#include "editor/render/ImGuiPass.hpp"
#include "engine/core/ScriptBuildSystem.hpp"
#include "engine/core/ScriptSystem.hpp"
#include "engine/core/strings/Hash.hpp"
#include "engine/graphics/RuntimeTypes.hpp"
#include "engine/graphics/loaders/MaterialLoader.hpp"
#include "engine/graphics/rg/RenderGraph.hpp"
#include "engine/resources/AssetTypes.hpp"
#include "engine/resources/AsyncFileManager.hpp"
#include "engine/resources/ResourceManager.hpp"

// Биндинг домена дерева — ДО using namespace das (иначе коллизии имён)
DAS_BASE_BIND_ENUM(tryeditor::StorageDomain, StorageDomain, Engine, Game, Content, Data, EngineShaders, GameShaders)

// Ось импорта glTF. Имя значения — это то, что пишется в `.meta` (`string(enum)` в daslang) и
// что показывает комбо панели Import Settings: форма перебирает значения через `each(t)`.
DAS_BASE_BIND_ENUM(tryeditor::GltfUpAxis, GltfUpAxis, Y, Z, X)

using UiFolderVector = eastl::vector<tryeditor::UiFolder>;
using UiFileVector = eastl::vector<tryeditor::UiFile>;
using Uint32Vector = eastl::vector<uint32_t>;

MAKE_TYPE_FACTORY(AssetItem, tryeditor::UiFile);
MAKE_TYPE_FACTORY(UiFolder, tryeditor::UiFolder);

MAKE_TYPE_FACTORY(UiFolderVector, UiFolderVector);
MAKE_TYPE_FACTORY(UiFileVector, UiFileVector);
MAKE_TYPE_FACTORY(Uint32Vector, Uint32Vector);

const UiFolderVector& GetFolders(tryeditor::StorageDomain domain, das::Context* ctx) {
    auto try_ctx = static_cast<tryeditor::TryEditorContext*>(ctx);
    return try_ctx->editor.GetAssetSourceDatabase().GetFolders(domain);
}

// Ревизия дерева: daslang кеширует по ней результаты обхода ("все .mat в data").
uint64_t GetTreeRevision(tryeditor::StorageDomain domain, das::Context* ctx) {
    auto try_ctx = static_cast<tryeditor::TryEditorContext*>(ctx);
    return try_ctx->editor.GetAssetSourceDatabase().GetTreeRevision(domain);
}

// Полная пересборка всех деревьев из диска. Пока файлового вотчера нет, это единственный
// способ увидеть новые файлы; вызывается из UI.
void RebuildAssetTrees(das::Context* ctx) {
    auto try_ctx = static_cast<tryeditor::TryEditorContext*>(ctx);
    try_ctx->editor.GetAssetSourceDatabase().RebuildTrees();
}

// Ключ расширения (8 байт) — тот же, что лежит в узле дерева: скрипт ищет файлы по типу,
// не трогая строки.
uint64_t AssetExtKey(const char* name) {
    return tryeditor::ExtKey(name ? eastl::string_view(name) : eastl::string_view());
}

namespace {

// Читает файл целиком; false — файла нет или он пуст. Мета-файлы маленькие, поэтому
// обычный ifstream: это редактор, а не игровой путь чтения контента.
bool ReadWholeFile(const eastl::string& path, eastl::vector<uint8_t>& out) {
    std::ifstream file(path.c_str(), std::ios::binary);
    if (!file.is_open())
        return false;
    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    file.seekg(0, std::ios::beg);
    if (size <= 0)
        return false;
    out.resize(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(out.data()), size);
    return file.good();
}

}  // namespace

struct SceneDTODas {
    const char* name;
    const char* path;
    das::Array meshes;
    das::Array mats;
};

eastl::vector<tryeditor::SceneDTO> scene_dtos;

void CreateBuild(const char* build_path, das::Array* scenes_ptr, das::Context* ctx) {
    auto try_ctx = static_cast<tryeditor::TryEditorContext*>(ctx);
    auto& build_system = try_ctx->engine.Get<tryeditor::BuildSystem>();

    scene_dtos.clear();

    LogInfo("CreateBuild CPP");

    // Bundle assets (UI font, UI material, screen textures) are discovered by BuildSystem
    // from the `.bundle` documents themselves: the bundle is the only list of what a screen
    // needs, so there is nothing to fill in here.
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
        dto.path = scene.path;
        dto.scene_guid = tryengine::core::PathId(dto.path.c_str());

        scene_dtos.emplace_back(eastl::move(dto));
    }
    for (auto& scene : scene_dtos) {
        for (auto& mesh : scene.meshes)
            LogInfo("MESH {}", mesh);
        for (auto& mat : scene.mats)
            LogInfo("MAT {}", mat);
    }

    tryengine::async::RunAndForget(tryengine::async::ThreadPool(), build_system.Build(scene_dtos, build_path));
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

//! Разрешает Play только когда текущее поколение скриптов работает и последняя
//! компиляция не закончилась ошибкой. После неудачной перезагрузки старый контекст жив
//! (редактор продолжает работать), но запускать на нём игру нельзя.
bool ScriptsHealthy(das::Context* ctx) {
    auto* try_ctx = static_cast<tryeditor::TryEditorContext*>(ctx);
    if (!try_ctx)
        return false;
    auto* scripts = try_ctx->engine.TryGet<tryengine::core::ScriptSystem>();
    return scripts != nullptr && scripts->IsReady() && !scripts->HasCompileError();
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

// Регистрирует .mat-файл как авторитетный ресурс: у материала нет .meta/GUID, его id —
// PathId(путь). Файл уже зарегистрирован сканером деревьев; ResolvePath — обычный путь,
// RegisterAuthoritative остаётся запасным для файла, появившегося после сканирования.
uint64_t RegisterMaterialFile(const char* path, das::Context* ctx) {
    auto* try_ctx = static_cast<tryeditor::TryEditorContext*>(ctx);
    if (!try_ctx || !path || !*path)
        return 0;
    auto* res_manager = try_ctx->engine.TryGet<tryengine::resources::ResourceManager>();
    if (!res_manager) {
        LogError("Resource Manager not found");
        return 0;
    }

    auto& registry = res_manager->GetAssetRegistry();
    const auto resolved = registry.ResolvePath(path);
    const uint64_t id = resolved.has_value() ? *resolved : registry.RegisterAuthoritative(path);
    res_manager->RequestLoad<tryengine::graphics::Material>(id);
    return id;
}

// Перечитывает .mat после сохранения: старый ресурс уходит в отставку, по тому же
// id создаётся новый.
uint64_t ReloadMaterialFile(const char* path, das::Context* ctx) {
    auto* try_ctx = static_cast<tryeditor::TryEditorContext*>(ctx);
    if (!try_ctx || !path || !*path)
        return 0;
    auto* res_manager = try_ctx->engine.TryGet<tryengine::resources::ResourceManager>();
    if (!res_manager) {
        LogError("Resource Manager not found");
        return 0;
    }

    auto& registry = res_manager->GetAssetRegistry();
    const auto resolved = registry.ResolvePath(path);
    const uint64_t id = resolved.has_value() ? *resolved : registry.RegisterAuthoritative(path);
    res_manager->Invalidate<tryengine::graphics::Material>(id);
    res_manager->RequestLoad<tryengine::graphics::Material>(id);
    return id;
}

// Атрибуты меша, которые требует материал: читает .mat, находит шейдер и отдаёт маску из
// его рефлексии. UINT32_MAX — шейдер ещё не загружен (запрос поставлен), решать «совместим
// ли» пока нельзя; редактор такие материалы не прячет.
uint32_t GetMaterialFileAttributes(const char* mat_path, das::Context* ctx) {
    if (!mat_path || !*mat_path || !ctx)
        return 0xFFFFFFFFu;

    auto* try_ctx = static_cast<tryeditor::TryEditorContext*>(ctx);
    auto* res_manager = try_ctx ? try_ctx->engine.TryGet<tryengine::resources::ResourceManager>() : nullptr;
    if (!res_manager)
        return 0xFFFFFFFFu;

    eastl::vector<uint8_t> bytes;
    if (!ReadWholeFile(mat_path, bytes))
        return 0xFFFFFFFFu;

    const auto config =
        tryengine::graphics::ParseMaterialConfig(eastl::span<const uint8_t>(bytes.data(), bytes.size()));
    if (!config.has_value())
        return 0xFFFFFFFFu;

    auto* shader = res_manager->GetPointerExplicit<tryengine::graphics::Shader>(config->shader);
    if (!shader) {
        // Шейдер нужен только для фильтрации; просим его асинхронно и в этом кадре считаем
        // совместимость неизвестной. IsLoading не даёт повторным кадрам растить refcount.
        if (!res_manager->IsLoading<tryengine::graphics::Shader>(config->shader) &&
            !res_manager->IsFailed<tryengine::graphics::Shader>(config->shader)) {
            res_manager->RequestLoad<tryengine::graphics::Shader>(config->shader);
        }
        return 0xFFFFFFFFu;
    }
    return shader->reflection.required_attributes;
}

// Путь loose-файла по его id. Для артефактов (glTF-суб-ассеты) путь ведёт в artifacts,
// у авторитетных файлов — в game/data; по пути редактор решает, можно ли материал править.
char* GetAssetFilePath(uint64_t guid, das::Context* ctx, das::LineInfoArg* at) {
    auto* try_ctx = static_cast<tryeditor::TryEditorContext*>(ctx);
    if (!try_ctx)
        return nullptr;
    auto* res_manager = try_ctx->engine.TryGet<tryengine::resources::ResourceManager>();
    if (!res_manager)
        return nullptr;

    const auto location = res_manager->GetAssetRegistry().GetAssetLocation(guid);
    if (!location.has_value())
        return nullptr;
    const auto* loose = eastl::get_if<tryengine::resources::LooseFileLocation>(&*location);
    if (!loose)
        return nullptr;
    return ctx->allocateString(loose->path.c_str(), static_cast<uint32_t>(loose->path.size()), at);
}

// Содержимое .meta как текст: панель Import Settings даёт его править. nullptr — меты нет
// (content/data) или файл не читается.
char* ReadAssetMeta(const char* asset_path, das::Context* ctx, das::LineInfoArg* at) {
    if (!asset_path || !*asset_path || !ctx)
        return nullptr;

    eastl::string meta_path(asset_path);
    meta_path.append(".meta");

    eastl::vector<uint8_t> bytes;
    if (!ReadWholeFile(meta_path, bytes))
        return nullptr;
    return ctx->allocateString(reinterpret_cast<const char*>(bytes.data()), static_cast<uint32_t>(bytes.size()), at);
}

// Запись отредактированной меты. Возвращает текст ошибки (пустая строка — успех).
// Идентичность ассета — guid и importer_type — менять нельзя: на guid ссылаются материалы,
// сцены и пак. Настройки проверяются кодеком самого импортёра, иначе ошибка в .meta
// привела бы к fallback на default-мету с новым guid и потере ссылок.
char* SaveAssetMeta(const char* asset_path, const char* text, das::Context* ctx, das::LineInfoArg* at) {
    if (!ctx)
        return nullptr;
    auto fail = [&](const char* message) {
        return ctx->allocateString(message, static_cast<uint32_t>(std::strlen(message)), at);
    };
    if (!asset_path || !*asset_path || !text || !ctx)
        return fail("invalid path or text");

    eastl::string meta_path(asset_path);
    meta_path.append(".meta");

    eastl::vector<uint8_t> old_bytes;
    if (!ReadWholeFile(meta_path, old_bytes))
        return fail("no .meta file for this asset");

    const auto old_header = tryeditor::ParseMetaHeader(eastl::span<const uint8_t>(old_bytes.data(), old_bytes.size()));
    if (!old_header.has_value()) {
        old_header.error().Discard();
        return fail("current .meta does not parse");
    }

    const eastl::span<const uint8_t> new_bytes(reinterpret_cast<const uint8_t*>(text), std::strlen(text));
    const auto new_header = tryeditor::ParseMetaHeader(new_bytes);
    if (!new_header.has_value()) {
        new_header.error().Discard();
        return fail("edited .meta is not valid ConfigFormat with a header");
    }

    if (new_header->guid != old_header->guid)
        return fail("guid is the asset identity and cannot be changed");
    if (new_header->importer_type != old_header->importer_type)
        return fail("importer_type is the asset identity and cannot be changed");
    if (new_header->sub_assets != old_header->sub_assets)
        return fail("sub_assets are generated by the importer and cannot be edited by hand");

    auto* try_ctx = static_cast<tryeditor::TryEditorContext*>(ctx);
    if (!try_ctx)
        return fail("editor context required");
    auto* importer =
        try_ctx->editor.GetAssetSourceDatabase().GetImportSystem().GetImporterByName(new_header->importer_type);
    if (!importer)
        return fail("no importer registered for this asset type");
    if (!importer->ValidateMeta(new_bytes))
        return fail("settings do not match the importer schema");

    std::ofstream file(meta_path.c_str(), std::ios::binary | std::ios::trunc);
    if (!file.is_open())
        return fail("cannot open .meta for writing");
    file.write(text, static_cast<std::streamsize>(std::strlen(text)));
    if (!file.good())
        return fail("cannot write .meta");

    return fail("");
}

// Явный переимпорт одного ассета из редактора. Асинхронный: панель не ждёт, а событие
// приходит через AssetPipeline::ReimportGeneration().
bool RequestReimport(const char* asset_path, das::Context* ctx) {
    auto* try_ctx = static_cast<tryeditor::TryEditorContext*>(ctx);
    if (!try_ctx || !asset_path || !*asset_path)
        return false;
    auto& file_manager = try_ctx->engine.Get<tryengine::resources::AsyncFileManager>();
    return try_ctx->editor.GetAssetSourceDatabase().RequestReimport(eastl::string_view(asset_path), file_manager);
}

uint64_t AssetReimportGeneration(das::Context* ctx) {
    auto* try_ctx = static_cast<tryeditor::TryEditorContext*>(ctx);
    return try_ctx ? try_ctx->editor.GetAssetSourceDatabase().ReimportGeneration() : 0ul;
}

// Авторский путь импортируемого ассета (для показа человеку). У авторитетных файлов он
// совпадает с GetAssetFilePath; у импортируемых — это исходник, а не artifacts/runtime/...
char* GetAssetSourcePath(uint64_t guid, das::Context* ctx, das::LineInfoArg* at) {
    auto* try_ctx = static_cast<tryeditor::TryEditorContext*>(ctx);
    if (!try_ctx || guid == 0)
        return nullptr;
    const eastl::string path = try_ctx->editor.GetAssetSourceDatabase().SourcePathOf(guid);
    if (path.empty())
        return nullptr;
    return ctx->allocateString(path.c_str(), path.size(), at);
}

// Дескриптор ImGui для превью текстуры-ассета. 0 — текстура ещё не загружена (запрос
// поставлен). Превью нужно панели материала: guid в поле textures — не то, что человек
// может проверить глазами.
void* GetTexturePreview(uint64_t texture_guid, das::Context* ctx) {
    auto* try_ctx = static_cast<tryeditor::TryEditorContext*>(ctx);
    if (!try_ctx || texture_guid == 0)
        return 0;
    auto* res_manager = try_ctx->engine.TryGet<tryengine::resources::ResourceManager>();
    if (!res_manager)
        return 0;

    auto* texture = res_manager->GetPointerExplicit<tryengine::graphics::TextureSampler>(texture_guid);
    if (!texture) {
        if (!res_manager->IsLoading<tryengine::graphics::TextureSampler>(texture_guid) &&
            !res_manager->IsFailed<tryengine::graphics::TextureSampler>(texture_guid)) {
            res_manager->RequestLoad<tryengine::graphics::TextureSampler>(texture_guid);
        }
        return 0;
    }

    auto* render = try_ctx->engine.TryGet<tryeditor::EditorRender>();
    if (!render)
        return 0;
    auto& rg = try_ctx->engine.Get<tryengine::graphics::RenderGraph>();
    auto* pass = rg.Get<tryeditor::ImGuiPass>(tryeditor::EditorRender::kEditorScope);
    if (!pass)
        return 0;
    return reinterpret_cast<void*>(pass->GetAssetTexture(texture_guid, texture->view, texture->sampler));
}

// Путь editor-артефакта с плоской композицией модели. Импортёр пишет его в формате
// .prefab рядом с исходным glTF-ассетом; игра этот артефакт не видит.
char* GetModelPrefabArtifactPath(const char* asset_path, uint64_t guid, das::Context* ctx, das::LineInfoArg* at) {
    if (!ctx || !asset_path || !*asset_path || guid == 0)
        return nullptr;

    const eastl::string_view path(asset_path);
    for (const tryeditor::StorageRoot& root : tryeditor::kStorageRoots) {
        if (!tryeditor::IsAssetDomain(root.domain))
            continue;

        const eastl::string_view dir(root.dir);
        if (path.size() <= dir.size())
            continue;
        if (path.substr(0, dir.size()) != dir || path[dir.size()] != '/')
            continue;

        const eastl::string artifact_path = tryengine::fmt::format(
            "{}/editor/{}/{}", root.artifacts_dir, guid, static_cast<uint32_t>(tryengine::EditorArtifactGuid::Prefab));
        return ctx->allocateString(artifact_path.c_str(), static_cast<uint32_t>(artifact_path.size()), at);
    }
    return nullptr;
}

// Live-правка материала: текст материала применяется к уже загруженному Material без
// пересоздания GPU-ресурсов, поэтому изменения видны в том же кадре.
bool ApplyMaterialConfigText(uint64_t material_guid, const char* text, das::Context* ctx) {
    auto* try_ctx = static_cast<tryeditor::TryEditorContext*>(ctx);
    if (!try_ctx || !material_guid || !text)
        return false;
    auto* res_manager = try_ctx->engine.TryGet<tryengine::resources::ResourceManager>();
    if (!res_manager)
        return false;

    const auto* bytes = reinterpret_cast<const uint8_t*>(text);
    const auto config = tryengine::graphics::ParseMaterialConfig(eastl::span<const uint8_t>(bytes, std::strlen(text)));
    if (!config.has_value()) {
        LogWarn("ApplyMaterialConfigText: {}", config.error());
        return false;
    }

    return tryengine::graphics::ApplyMaterialConfigLive(*res_manager, material_guid, *config);
}

void* GetEditorImGuiContext() {
    return ImGui::GetCurrentContext();
}

// EditorUpdate идёт до того, как контекст ImGui кадра станет текущим; обращение к IO в этот
// момент разыменовало бы null. Горячие клавиши спрашивают это, чтобы не срабатывать в вводе.
// Именно WantTextInput, а не WantCaptureKeyboard: при включённой навигации ImGui второй
// выставлен почти всегда и глушил бы все хоткеи.
bool IsEditorTextInputActive() {
    if (ImGui::GetCurrentContext() == nullptr)
        return false;
    return ImGui::GetIO().WantTextInput;
}

//! Переключатель VSync из панели: сам по себе он не пересоздаёт swapchain, а просит
//! EditorRender сделать это в начале следующего кадра.
bool EditorIsVSync(das::Context* ctx) {
    auto* try_ctx = static_cast<tryeditor::TryEditorContext*>(ctx);
    if (!try_ctx)
        return false;
    return try_ctx->engine.Get<tryeditor::EditorRender>().IsVSync();
}

void EditorSetVSync(bool enabled, das::Context* ctx) {
    auto* try_ctx = static_cast<tryeditor::TryEditorContext*>(ctx);
    if (!try_ctx)
        return;
    try_ctx->engine.Get<tryeditor::EditorRender>().RequestVSync(enabled);
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
        addField<DAS_BIND_MANAGED_FIELD(ext)>("ext", "ext");
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

        addEnumeration(new EnumerationStorageDomain());
        addEnumeration(new EnumerationGltfUpAxis());

        addAnnotation(new UiFileAnnotation(lib));

        addVectorAnnotation<UiFileVector>(this, lib, "UiFileVector");
        addVectorAnnotation<Uint32Vector>(this, lib, "Uint32Vector");

        addAnnotation(new UiFolderAnnotation(lib));
        addVectorAnnotation<UiFolderVector>(this, lib, "UiFolderVector");

        das::addExtern<DAS_BIND_FUN(GetEditorCameraViewImage)>(*this, lib, "GetEditorCameraViewImage",
                                                               das::SideEffects::none, "GetEditorCameraViewImage");

        das::addExtern<DAS_BIND_FUN(GetGameCameraViewImage)>(*this, lib, "GetGameCameraViewImage",
                                                             das::SideEffects::none, "GetGameCameraViewImage");

        das::addExtern<DAS_BIND_FUN(GetEditorImGuiContext)>(*this, lib, "GetEditorImGuiContext", das::SideEffects::none,
                                                            "GetEditorImGuiContext");
        das::addExtern<DAS_BIND_FUN(IsEditorTextInputActive)>(
            *this, lib, "IsEditorTextInputActive", das::SideEffects::accessGlobal, "IsEditorTextInputActive");
        das::addExtern<DAS_BIND_FUN(EditorIsVSync)>(*this, lib, "EditorIsVSync", das::SideEffects::accessGlobal,
                                                    "EditorIsVSync");
        das::addExtern<DAS_BIND_FUN(EditorSetVSync)>(*this, lib, "EditorSetVSync", das::SideEffects::accessGlobal,
                                                     "EditorSetVSync");

        das::addExtern<DAS_BIND_FUN(RegisterMaterialFile)>(*this, lib, "RegisterMaterialFile",
                                                           das::SideEffects::accessGlobal, "RegisterMaterialFile");

        das::addExtern<DAS_BIND_FUN(ReloadMaterialFile)>(*this, lib, "ReloadMaterialFile",
                                                         das::SideEffects::accessGlobal, "ReloadMaterialFile");

        das::addExtern<DAS_BIND_FUN(GetAssetFilePath)>(*this, lib, "GetAssetFilePath", das::SideEffects::accessGlobal,
                                                       "GetAssetFilePath");

        das::addExtern<DAS_BIND_FUN(GetMaterialFileAttributes)>(
            *this, lib, "GetMaterialFileAttributes", das::SideEffects::accessGlobal, "GetMaterialFileAttributes");

        das::addExtern<DAS_BIND_FUN(GetAssetSourcePath)>(*this, lib, "GetAssetSourcePath",
                                                         das::SideEffects::accessGlobal, "GetAssetSourcePath");

        das::addExtern<DAS_BIND_FUN(GetTexturePreview)>(*this, lib, "GetTexturePreview", das::SideEffects::accessGlobal,
                                                        "GetTexturePreview");

        das::addExtern<DAS_BIND_FUN(GetModelPrefabArtifactPath)>(
            *this, lib, "GetModelPrefabArtifactPath", das::SideEffects::accessGlobal, "GetModelPrefabArtifactPath");

        das::addExtern<DAS_BIND_FUN(ApplyMaterialConfigText)>(
            *this, lib, "ApplyMaterialConfigText", das::SideEffects::accessGlobal, "ApplyMaterialConfigText");

        das::addExtern<DAS_BIND_FUN(GetFolders)>(*this, lib, "get_folders", das::SideEffects::none, "GetFolders");

        das::addExtern<DAS_BIND_FUN(GetTreeRevision)>(*this, lib, "get_tree_revision", das::SideEffects::accessGlobal,
                                                      "GetTreeRevision");

        das::addExtern<DAS_BIND_FUN(RebuildAssetTrees)>(*this, lib, "rebuild_asset_trees",
                                                        das::SideEffects::accessGlobal, "RebuildAssetTrees");

        das::addExtern<DAS_BIND_FUN(AssetExtKey)>(*this, lib, "asset_ext_key", das::SideEffects::none, "AssetExtKey");

        das::addExtern<DAS_BIND_FUN(ReadAssetMeta)>(*this, lib, "ReadAssetMeta", das::SideEffects::accessGlobal,
                                                    "ReadAssetMeta");

        das::addExtern<DAS_BIND_FUN(SaveAssetMeta)>(*this, lib, "SaveAssetMeta", das::SideEffects::accessGlobal,
                                                    "SaveAssetMeta");

        das::addExtern<DAS_BIND_FUN(RequestReimport)>(*this, lib, "RequestReimport", das::SideEffects::accessGlobal,
                                                      "RequestReimport");

        das::addExtern<DAS_BIND_FUN(AssetReimportGeneration)>(
            *this, lib, "AssetReimportGeneration", das::SideEffects::accessGlobal, "AssetReimportGeneration");

        das::addExtern<DAS_BIND_FUN(CreateBuild)>(*this, lib, "CreateBuild", das::SideEffects::accessGlobal,
                                                  "CreateBuild");

        das::addExtern<DAS_BIND_FUN(BuildScriptPak)>(*this, lib, "BuildScriptPak", das::SideEffects::accessGlobal,
                                                     "BuildScriptPak");

        das::addExtern<DAS_BIND_FUN(TestCompileScriptPak)>(*this, lib, "TestCompileScriptPak",
                                                           das::SideEffects::accessGlobal, "TestCompileScriptPak");

        das::addExtern<DAS_BIND_FUN(ScriptsHealthy)>(*this, lib, "ScriptsHealthy", das::SideEffects::accessGlobal,
                                                     "ScriptsHealthy");

        tryeditor::RegisterScriptReloadBindings(*this, lib);

        verifyAotReady();
    }
};

REGISTER_DYN_MODULE(Module_TryEditor, Module_TryEditor);
REGISTER_MODULE(Module_TryEditor);