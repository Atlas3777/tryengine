#include <EASTL/hash_set.h>
#include <daScript/daScript.h>
#include <daScript/simulate/aot.h>

#include "engine/core/TryengineContext.hpp"
#include "engine/graphics/MaterialConfig.hpp"
#include "engine/graphics/RuntimeTypes.hpp"
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

tryengine::graphics::Mesh* GetMeshPointer(uint64_t guid, das::Context* ctx) {
    auto* res_manager = GetResourceManager(ctx);
    return res_manager ? res_manager->GetPointerExplicit<tryengine::graphics::Mesh>(guid) : nullptr;
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
        if (requested_shaders.insert(shader_guid).second) {
            res_manager->RequestLoad<tryengine::graphics::Shader>(shader_guid);
        }
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

        das::addExtern<DAS_BIND_FUN(ReleaseMaterialResource)>(
            *this, lib, "ReleaseMaterialResource", das::SideEffects::accessGlobal, "ReleaseMaterialResource");

        das::addExtern<DAS_BIND_FUN(ReleaseMeshResource)>(*this, lib, "ReleaseMeshResource",
                                                          das::SideEffects::accessGlobal, "ReleaseMeshResource");

        das::addExtern<DAS_BIND_FUN(GetMeshPointer)>(*this, lib, "GetMeshPointer", das::SideEffects::accessGlobal,
                                                     "GetMeshPointer");

        das::addExtern<DAS_BIND_FUN(GetMaterialPointer)>(*this, lib, "GetMaterialPointer",
                                                         das::SideEffects::accessGlobal, "GetMaterialPointer");

        das::addExtern<DAS_BIND_FUN(GetMaterialSchema)>(*this, lib, "GetMaterialSchema", das::SideEffects::accessGlobal,
                                                        "GetMaterialSchema");

        verifyAotReady();
    }
};

REGISTER_DYN_MODULE(Module_Resources, Module_Resources);
REGISTER_MODULE(Module_Resources);
