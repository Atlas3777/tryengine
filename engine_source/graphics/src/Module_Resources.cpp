#include <EASTL/hash_set.h>
#include <daScript/daScript.h>
#include <daScript/simulate/aot.h>

#include "engine/core/TryengineContext.hpp"
#include "engine/graphics/MaterialConfig.hpp"
#include "engine/graphics/RuntimeTypes.hpp"
#include "engine/graphics/UiSystem.hpp"
#include "engine/resources/ResourceManager.hpp"

using namespace tryengine::core;
using das::LineInfoArg;
using das::Module;
using das::ModuleLibrary;

MAKE_TYPE_FACTORY(TryMesh, tryengine::graphics::Mesh)
MAKE_TYPE_FACTORY(TryMat, tryengine::graphics::Material)

namespace {

// Общая преамбула биндингов ресурсов: менеджер живёт в движке, скрипт видит только guid.
tryengine::resources::ResourceManager* GetResourceManager(das::Context* ctx) {
    auto* try_ctx = static_cast<TryengineContext*>(ctx);
    auto* res_manager = try_ctx ? try_ctx->engine.TryGet<tryengine::resources::ResourceManager>() : nullptr;
    if (!res_manager)
        LogError("Resource Manager not found");
    return res_manager;
}

}  // namespace

// UI texture -> material for `Ui::ImageTexture`. The binding lives in this module because the
// TryMat annotation is declared here, and a texture material is a resource lookup, not a UI
// state: the UI owns the cache, the script only ever sees the material pointer.
tryengine::graphics::Material* UiTextureMaterial(uint64_t guid) {
    auto* ui = tryengine::graphics::UiSystem::ForScripts();
    return ui != nullptr ? ui->TextureMaterial(guid) : nullptr;
}

void RequestLoadMesh(uint64_t guid, das::Context* ctx) {
    if (auto* res_manager = GetResourceManager(ctx))
        res_manager->RequestLoad<tryengine::graphics::Mesh>(guid);
}

void RequestLoadMaterial(uint64_t guid, das::Context* ctx) {
    if (auto* res_manager = GetResourceManager(ctx))
        res_manager->RequestLoad<tryengine::graphics::Material>(guid);
}

bool ResourceIsReadyMesh(uint64_t guid, das::Context* ctx) {
    auto* res_manager = GetResourceManager(ctx);
    return res_manager ? res_manager->IsReady<tryengine::graphics::Mesh>(guid) : false;
}

bool ResourceIsReadyMaterial(uint64_t guid, das::Context* ctx) {
    auto* res_manager = GetResourceManager(ctx);
    return res_manager ? res_manager->IsReady<tryengine::graphics::Material>(guid) : false;
}

bool ResourceIsFailedMesh(uint64_t guid, das::Context* ctx) {
    auto* res_manager = GetResourceManager(ctx);
    return res_manager ? res_manager->IsFailed<tryengine::graphics::Mesh>(guid) : false;
}

bool ResourceIsFailedMaterial(uint64_t guid, das::Context* ctx) {
    auto* res_manager = GetResourceManager(ctx);
    return res_manager ? res_manager->IsFailed<tryengine::graphics::Material>(guid) : false;
}

tryengine::graphics::Mesh* GetMeshPointer(uint64_t guid, das::Context* ctx) {
    auto* res_manager = GetResourceManager(ctx);
    return res_manager ? res_manager->GetPointerExplicit<tryengine::graphics::Mesh>(guid) : nullptr;
}

// Материал по умолчанию из артефакта меша: 0, пока меш не загружен или у примитива нет
// материала. Гид меша адресует только геометрию, дефолт — данные артефакта.
uint64_t MeshDefaultMaterial(uint64_t guid, das::Context* ctx) {
    auto* mesh = GetMeshPointer(guid, ctx);
    return mesh != nullptr ? mesh->default_material : 0;
}

tryengine::graphics::Material* GetMaterialPointer(uint64_t guid, das::Context* ctx) {
    auto* res_manager = GetResourceManager(ctx);
    return res_manager ? res_manager->GetPointerExplicit<tryengine::graphics::Material>(guid) : nullptr;
}

// Схема параметров материала для редактора. Возвращаем `char*` из кучи контекста
// daslang: на стороне скрипта это обычный `string`, промежуточный файл не нужен.
// Шейдер грузится лениво: первый вызов только запускает загрузку и отдаёт null.
char* GetMaterialSchema(uint64_t shader_guid, das::Context* ctx, LineInfoArg* at) {
    auto* res_manager = GetResourceManager(ctx);
    if (!res_manager)
        return nullptr;

    auto* shader = res_manager->GetPointerExplicit<tryengine::graphics::Shader>(shader_guid);
    if (!shader) {
        static eastl::hash_set<uint64_t> requested_shaders;
        if (requested_shaders.insert(shader_guid).second)
            res_manager->RequestLoad<tryengine::graphics::Shader>(shader_guid);
        return nullptr;
    }

    const eastl::string schema = tryengine::graphics::BuildMaterialSchemaConfig(shader->reflection);
    return ctx->allocateString(schema.c_str(), schema.size(), at);
}

void ReleaseMeshResource(uint64_t guid, das::Context* ctx) {
    if (auto* res_manager = GetResourceManager(ctx))
        res_manager->Release<tryengine::graphics::Mesh>(guid);
}

void ReleaseMaterialResource(uint64_t guid, das::Context* ctx) {
    if (auto* res_manager = GetResourceManager(ctx))
        res_manager->Release<tryengine::graphics::Material>(guid);
}

// Авторская ссылка -> id. Возвращает 0 для битой ссылки; сцена и редактор считают это
// ошибкой, а не «ещё не загрузилось».
uint64_t ResolveAssetRef(const char* ref, das::Context* ctx) {
    if (!ref || !*ref)
        return 0;
    auto* res_manager = GetResourceManager(ctx);
    if (!res_manager)
        return 0;
    const auto id = res_manager->GetAssetRegistry().ResolveRef(ref);
    return id.has_value() ? *id : 0;
}

// id -> каноническая авторская ссылка (путь для авторитетного файла, guid: для остальных).
char* AssetRefOf(uint64_t id, das::Context* ctx, LineInfoArg* at) {
    auto* res_manager = GetResourceManager(ctx);
    if (!res_manager)
        return nullptr;
    const auto ref = res_manager->GetAssetRegistry().AssetRefOf(id);
    if (!ref.has_value())
        return nullptr;
    return ctx->allocateString(ref->c_str(), ref->size(), at);
}

bool IsImportedAsset(uint64_t id, das::Context* ctx) {
    auto* res_manager = GetResourceManager(ctx);
    return res_manager ? res_manager->GetAssetRegistry().IsImported(id) : false;
}

uint64_t AssetPathId(const char* path) {
    if (!path || !*path)
        return 0;
    return tryengine::core::PathId(path);
}

// Путь (или ссылка `guid:`) -> id. В паке content-файл зарегистрирован под PathId(путь),
// поэтому таблица путей не нужна: тот же путь даёт тот же id.
uint64_t ResolveAssetPath(const char* path, das::Context* ctx) {
    if (!path || !*path)
        return 0;
    auto* res_manager = GetResourceManager(ctx);
    if (!res_manager)
        return 0;
    const auto id = res_manager->GetAssetRegistry().ResolveRef(path);
    return id.has_value() ? *id : 0;
}

// Редактор: путь, которого ещё нет в реестре (файл из диалога), становится
// авторитетным ресурсом. В игре не вызывается — там всё уже в паке.
uint64_t RegisterAssetPath(const char* path, das::Context* ctx) {
    if (!path || !*path)
        return 0;
    auto* res_manager = GetResourceManager(ctx);
    if (!res_manager)
        return 0;
    auto& registry = res_manager->GetAssetRegistry();
    const auto resolved = registry.ResolvePath(path);
    return resolved.has_value() ? *resolved : registry.RegisterAuthoritative(path);
}

// ---------------------------------------------------------------------------
// Content text API
// ---------------------------------------------------------------------------
// Единственная точка чтения content-файлов для скриптов. В редакторе источник — loose
// файл, в игре — чанк пака; разницу скрывает AssetRegistry. Текст не кешируется:
// handle — это запрос, который живёт до TakeAssetText/CancelTextRequest, потому что
// документ всё равно разбирается скриптом один раз.

uint64_t RequestLoadText(uint64_t asset_id, das::Context* ctx) {
    auto* res_manager = GetResourceManager(ctx);
    return res_manager ? res_manager->GetTextRequests().Request(asset_id) : 0;
}

bool TextRequestIsReady(uint64_t handle, das::Context* ctx) {
    auto* res_manager = GetResourceManager(ctx);
    return res_manager ? res_manager->GetTextRequests().IsReady(handle) : false;
}

bool TextRequestIsFailed(uint64_t handle, das::Context* ctx) {
    auto* res_manager = GetResourceManager(ctx);
    return res_manager ? res_manager->GetTextRequests().IsFailed(handle) : false;
}

char* TakeAssetText(uint64_t handle, das::Context* ctx, LineInfoArg* at) {
    auto* res_manager = GetResourceManager(ctx);
    if (!res_manager)
        return nullptr;

    const eastl::string text = res_manager->GetTextRequests().Take(handle);
    return ctx->allocateString(text.c_str(), text.size(), at);
}

void CancelTextRequest(uint64_t handle, das::Context* ctx) {
    if (auto* res_manager = GetResourceManager(ctx))
        res_manager->GetTextRequests().Cancel(handle);
}

class Module_Resources : public das::Module {
public:
    Module_Resources() : Module("tryResources") {
        ModuleLibrary lib(this);
        lib.addBuiltInModule();

        addAnnotation(new das::DummyTypeAnnotation("TryMesh", "tryengine::graphics::Mesh",
                                                   sizeof(tryengine::graphics::Mesh),
                                                   alignof(tryengine::graphics::Mesh)));

        addAnnotation(new das::DummyTypeAnnotation("TryMat", "tryengine::graphics::Material",
                                                   sizeof(tryengine::graphics::Material),
                                                   alignof(tryengine::graphics::Material)));

        das::addExtern<DAS_BIND_FUN(RequestLoadMaterial)>(*this, lib, "RequestLoadMaterial",
                                                          das::SideEffects::accessGlobal, "RequestLoadMaterial");

        das::addExtern<DAS_BIND_FUN(RequestLoadMesh)>(*this, lib, "RequestLoadMesh", das::SideEffects::accessGlobal,
                                                      "RequestLoadMesh");

        das::addExtern<DAS_BIND_FUN(ResourceIsReadyMaterial)>(
            *this, lib, "ResourceIsReadyMaterial", das::SideEffects::accessGlobal, "ResourceIsReadyMaterial");

        das::addExtern<DAS_BIND_FUN(ResourceIsReadyMesh)>(*this, lib, "ResourceIsReadyMesh",
                                                          das::SideEffects::accessGlobal, "ResourceIsReadyMesh");

        das::addExtern<DAS_BIND_FUN(ResourceIsFailedMaterial)>(
            *this, lib, "ResourceIsFailedMaterial", das::SideEffects::accessGlobal, "ResourceIsFailedMaterial");

        das::addExtern<DAS_BIND_FUN(ResourceIsFailedMesh)>(*this, lib, "ResourceIsFailedMesh",
                                                           das::SideEffects::accessGlobal, "ResourceIsFailedMesh");

        das::addExtern<DAS_BIND_FUN(ReleaseMaterialResource)>(
            *this, lib, "ReleaseMaterialResource", das::SideEffects::accessGlobal, "ReleaseMaterialResource");

        das::addExtern<DAS_BIND_FUN(ReleaseMeshResource)>(*this, lib, "ReleaseMeshResource",
                                                          das::SideEffects::accessGlobal, "ReleaseMeshResource");

        das::addExtern<DAS_BIND_FUN(GetMeshPointer)>(*this, lib, "GetMeshPointer", das::SideEffects::accessGlobal,
                                                     "GetMeshPointer");

        das::addExtern<DAS_BIND_FUN(MeshDefaultMaterial)>(*this, lib, "MeshDefaultMaterial",
                                                          das::SideEffects::accessGlobal, "MeshDefaultMaterial");
        das::addExtern<DAS_BIND_FUN(GetMaterialPointer)>(*this, lib, "GetMaterialPointer",
                                                         das::SideEffects::accessGlobal, "GetMaterialPointer");

        das::addExtern<DAS_BIND_FUN(GetMaterialSchema)>(*this, lib, "GetMaterialSchema", das::SideEffects::accessGlobal,
                                                        "GetMaterialSchema");

        das::addExtern<DAS_BIND_FUN(UiTextureMaterial)>(*this, lib, "UiTextureMaterial", das::SideEffects::accessGlobal,
                                                        "UiTextureMaterial");

        das::addExtern<DAS_BIND_FUN(ResolveAssetRef)>(*this, lib, "ResolveAssetRef", das::SideEffects::accessGlobal,
                                                      "ResolveAssetRef");

        das::addExtern<DAS_BIND_FUN(AssetRefOf)>(*this, lib, "AssetRefOf", das::SideEffects::accessGlobal,
                                                 "AssetRefOf");

        das::addExtern<DAS_BIND_FUN(IsImportedAsset)>(*this, lib, "IsImportedAsset", das::SideEffects::accessGlobal,
                                                      "IsImportedAsset");

        das::addExtern<DAS_BIND_FUN(AssetPathId)>(*this, lib, "AssetPathId", das::SideEffects::none, "AssetPathId");

        das::addExtern<DAS_BIND_FUN(ResolveAssetPath)>(*this, lib, "ResolveAssetPath", das::SideEffects::accessGlobal,
                                                       "ResolveAssetPath");

        das::addExtern<DAS_BIND_FUN(RegisterAssetPath)>(*this, lib, "RegisterAssetPath", das::SideEffects::accessGlobal,
                                                        "RegisterAssetPath");

        das::addExtern<DAS_BIND_FUN(RequestLoadText)>(*this, lib, "RequestLoadText", das::SideEffects::accessGlobal,
                                                      "RequestLoadText");

        das::addExtern<DAS_BIND_FUN(TextRequestIsReady)>(*this, lib, "TextRequestIsReady",
                                                         das::SideEffects::accessGlobal, "TextRequestIsReady");

        das::addExtern<DAS_BIND_FUN(TextRequestIsFailed)>(*this, lib, "TextRequestIsFailed",
                                                          das::SideEffects::accessGlobal, "TextRequestIsFailed");

        das::addExtern<DAS_BIND_FUN(TakeAssetText)>(*this, lib, "TakeAssetText", das::SideEffects::accessGlobal,
                                                    "TakeAssetText");

        das::addExtern<DAS_BIND_FUN(CancelTextRequest)>(*this, lib, "CancelTextRequest", das::SideEffects::accessGlobal,
                                                        "CancelTextRequest");

        verifyAotReady();
    }
};

REGISTER_DYN_MODULE(Module_Resources, Module_Resources);
REGISTER_MODULE(Module_Resources);
