// #include <daScript/daScript.h>
// #include <daScript/simulate/aot.h>
//
// #include "engine/core/ScriptSystem.hpp"
// #include "engine/graphics/RuntimeTypes.hpp"
// #include "engine/resources/AsyncFileManager.hpp"
// #include "engine/resources/ResourceHandle.hpp"
// #include "engine/resources/ResourceManager.hpp"
//
// MAKE_TYPE_FACTORY(MeshResource, tryengine::resources::ResourceHandle<tryengine::graphics::Mesh>)
// MAKE_TYPE_FACTORY(ShaderResource, tryengine::resources::ResourceHandle<tryengine::graphics::Shader>)
//
// __forceinline tryengine::resources::ResourceHandle<tryengine::graphics::Shader> GetShaderHandle(uint64_t guid, das::Context* ctx) {
//     const auto* try_ctx = static_cast<tryengine::core::TryengineContext*>(ctx);
//
//     auto res_manager = try_ctx->engine.TryGet<tryengine::resources::ResourceManager>();
//     if (res_manager) {
//         auto shader_res = res_manager->Get<tryengine::graphics::Shader>(guid);
//         if (!shader_res.has_value())
//             LogError("Mesh handle error: {}", shader_res.error().Message());
//         return *shader_res;
//     }
//     LogError("Не найден ResourceManager");
//
//     //TODO: Возвращать базовый шейдер
//     return tryengine::resources::ResourceHandle<tryengine::graphics::Shader>();
// }
//
// __forceinline tryengine::resources::ResourceHandle<tryengine::graphics::Mesh> GetMeshHandle(uint64_t guid, das::Context* ctx) {
//     const auto* try_ctx = static_cast<tryengine::core::TryengineContext*>(ctx);
//
//     auto res_manager = try_ctx->engine.TryGet<tryengine::resources::ResourceManager>();
//     if (res_manager) {
//         auto mesh_res = res_manager->Get<tryengine::graphics::Mesh>(guid);
//         if (!mesh_res.has_value())
//             LogError("Mesh handle error: {}", mesh_res.error().Message());
//         return *mesh_res;
//     }
//     LogError("Не найден ResourceManager");
//
//     //TODO: Возвращать базовый Mesh
//     return tryengine::resources::ResourceHandle<tryengine::graphics::Mesh>();
// }
//
// using namespace das;
//
// struct ResourceMeshAnnotation
//     : ManagedStructureAnnotation<tryengine::resources::ResourceHandle<tryengine::graphics::Mesh>, false> {
//     ResourceMeshAnnotation(ModuleLibrary& ml) : ManagedStructureAnnotation("MeshResource", ml) {
//         addProperty<DAS_BIND_MANAGED_PROP(IsReady)>("IsReady", "IsReady");
//     }
//
//     bool canCopy() const override { return true; }
//     bool canMove() const override { return true; }
// };
//
// struct ResourceShaderAnnotation
//     : ManagedStructureAnnotation<tryengine::resources::ResourceHandle<tryengine::graphics::Shader>, false> {
//     ResourceShaderAnnotation(ModuleLibrary& ml) : ManagedStructureAnnotation("ShaderResource", ml) {
//         addProperty<DAS_BIND_MANAGED_PROP(IsReady)>("IsReady", "IsReady");
//     }
//
//     bool canCopy() const override { return true; }
//     bool canMove() const override { return true; }
// };
//
// class Module_Resources : public Module {
// public:
//     Module_Resources() : Module("tryResources") {
//         ModuleLibrary lib(this);
//
//         addAnnotation(new ResourceShaderAnnotation(lib));
//         addAnnotation(new ResourceMeshAnnotation(lib));
//
//         das::addExtern<DAS_BIND_FUN(GetMeshHandle), SimNode_ExtFuncCallAndCopyOrMove>(
//             *this, lib, "GetMeshHandle", SideEffects::accessExternal, "GetMeshHandle");
//
//         das::addExtern<DAS_BIND_FUN(GetShaderHandle), SimNode_ExtFuncCallAndCopyOrMove>(
//             *this, lib, "GetShaderHandle", SideEffects::accessExternal, "GetShaderHandle");
//
//         verifyAotReady();
//     }
// };
//
// REGISTER_DYN_MODULE(Module_Resources, Module_Resources);
// REGISTER_MODULE(Module_Resources);