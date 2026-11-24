options gen2
options relaxed_assign = false

module Render shared public

require tryengine/Types
require tryengine/TransformSystem
require tryRenderer
require tryResources
require daslib/decs
require daslib/decs_boost
require daslib/math_boost

require math

struct PointLightGPU {
    position_radius : float4
    color_intensity : float4
}

var lights : array<PointLightGPU>

[export]
def get_lights() : array<PointLightGPU>? {
    lights.clear()

    query() <| $(var _world : WorldTransform; var light : PointLight) {
        lights |> push(PointLightGPU(
            position_radius = float4(_world.position.x, _world.position.y, _world.position.z, light.radius),
            color_intensity = float4(light.color.x, light.color.y, light.color.z, light.intensity)
        ))
    }

    return unsafe(addr(lights))
}
[cpp_layout]
struct DrawCall{
    matrix : float4x4
    mesh_id : TryMesh const?
    material_id : TryMat const?
    entity_id : uint64
    _pad : uint64
}

var dcs : array<DrawCall>

// LLVM JIT cannot lower reinterpret<EntityId, uint64>; pack the fields numerically, the same
// way Physics.das does. EntityId is { id : uint, generation : int } and the little-endian
// reinterpret it replaces puts id in the low half and generation in the high half.
def private EntityRaw(eid : EntityId) : uint64 {
    return uint64(eid.id) | (uint64(uint(eid.generation)) << 32)
}

def private PushDraw(eid : EntityId; _world : WorldTransform; mesh : TryMesh?; material : TryMat?; mesh_scale : float3) {
    if (mesh == null || material == null) {
        return
    }
    dcs.push(DrawCall(
        mesh_id = mesh,
        material_id = material,
        matrix = compose(_world.position, _world.rotation, _world.scale * mesh_scale),
        entity_id = EntityRaw(eid)
    ))
}

[export]
def get_render_obj() : array<DrawCall>? {
    dcs.clear()

    // Only ready pointers are drawn; loading is not this pass's job. `Model` writes the pair
    // when the entity is created, so a draw call here is never half-resolved. The authored
    // `model` component is required as well: it is what makes the entity an appearance, so
    // dropping it stops the draw even if the runtime pointers are still around.
    //
    // `meshScale` is split into its own query instead of a per-entity component lookup: the
    // common path (no component) must stay one stride scan, and the second query excludes
    // the scaled entities by name, so nothing is drawn twice.
    query() $(eid : EntityId; _world : WorldTransform; model : ModelRender; mesh : TryMesh?; material : TryMat?;
              meshScale : MeshScale) {
        PushDraw(eid, _world, mesh, material, meshScale.scale)
    }
    query() $ [REQUIRE_NOT(meshScale)] (eid : EntityId; _world : WorldTransform; model : ModelRender;
                                        mesh : TryMesh?; material : TryMat?) {
        PushDraw(eid, _world, mesh, material, float3(1.0f, 1.0f, 1.0f))
    }

    return unsafe(addr(dcs))
}

[cpp_layout]
struct CameraC {
    view : float4x4
    proj : float4x4
    position : float3
}

var editor_cameraC : CameraC

[export]
def GetEditorCamera(viewport_size : float2) : CameraC? {
    query() $[REQUIRE(editorCameraTag)] (let _world : WorldTransform, let camera : Camera) {
        editor_cameraC.position = _world.position

        // Вектор взгляда берем по +Y, вектор верха — по +Z
        let forward = quat_mul_vec(_world.rotation, float3(0.0f, 1.0f, 0.0f))
        let up      = quat_mul_vec(_world.rotation, float3(0.0f, 0.0f, 1.0f))
        let target  = _world.position + forward

        editor_cameraC.view = look_at_rh(_world.position, target, up)

        let aspect = viewport_size.x / viewport_size.y
        let fov_rad = radians(camera.fov)
        editor_cameraC.proj = perspective_rh_0_to_1(fov_rad, aspect, camera.near_plane, camera.far_plane)
        // FIX: Инвертируем Y для Vulkan NDC (где +Y направлен вниз)
        editor_cameraC.proj[1][1] *= -1.0f
    }

    return unsafe(addr(editor_cameraC))
}

var game_cameraC : CameraC


[export]
def GetGameCamera(viewport_size : float2) : CameraC? {
    query() $[REQUIRE_NOT(editorCameraTag)] (eid : EntityId, let _world : WorldTransform, let camera : Camera){
        game_cameraC.position = _world.position

        // Вектор взгляда берем по +Y, вектор верха — по +Z
        let forward = quat_mul_vec(_world.rotation, float3(0.0f, 1.0f, 0.0f))
        let up      = quat_mul_vec(_world.rotation, float3(0.0f, 0.0f, 1.0f))
        let target  = _world.position + forward

        game_cameraC.view = look_at_rh(_world.position, target, up)

        let aspect = viewport_size.x / viewport_size.y
        let fov_rad = radians(camera.fov)
        game_cameraC.proj = perspective_rh_0_to_1(fov_rad, aspect, camera.near_plane, camera.far_plane)
        // FIX: Инвертируем Y для Vulkan NDC (где +Y направлен вниз)
        game_cameraC.proj[1][1] *= -1.0f
    }

    return unsafe(addr(game_cameraC))
}
