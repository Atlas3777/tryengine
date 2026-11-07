options gen2

module Render shared public

require tryengine/Types
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

    query() <| $(var transform : Transform; var light : PointLight) {
        lights |> push(PointLightGPU(
            position_radius = float4(transform.position.x, transform.position.y, transform.position.z, light.radius),
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

[export]
def get_render_obj() : array<DrawCall>? {
    start_load()
    dcs.clear()

    query() <| $(eid : EntityId, transform : Transform, mesh : TryMesh?, material : TryMat?) {
        let id = unsafe(reinterpret<uint64>(eid))
        dcs.push(DrawCall(
            mesh_id = mesh,
            material_id = material,
            matrix = compose(transform.position, transform.rotation, transform.scale),
            entity_id = id
        ))
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
    query() <| $(let transform : Transform, let camera : Camera; let editorCameraTag : bool){
        editor_cameraC.position = transform.position

        // Вектор взгляда берем по +Y, вектор верха — по +Z
        let forward = quat_mul_vec(transform.rotation, float3(0.0f, 1.0f, 0.0f))
        let up      = quat_mul_vec(transform.rotation, float3(0.0f, 0.0f, 1.0f))
        let target  = transform.position + forward

        editor_cameraC.view = look_at_rh(transform.position, target, up)

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
    query() $[REQUIRE_NOT(editorCameraTag)] (eid : EntityId, let transform : Transform, let camera : Camera){
        game_cameraC.position = transform.position

        // Вектор взгляда берем по +Y, вектор верха — по +Z
        let forward = quat_mul_vec(transform.rotation, float3(0.0f, 1.0f, 0.0f))
        let up      = quat_mul_vec(transform.rotation, float3(0.0f, 0.0f, 1.0f))
        let target  = transform.position + forward

        game_cameraC.view = look_at_rh(transform.position, target, up)

        let aspect = viewport_size.x / viewport_size.y
        let fov_rad = radians(camera.fov)
        game_cameraC.proj = perspective_rh_0_to_1(fov_rad, aspect, camera.near_plane, camera.far_plane)
        // FIX: Инвертируем Y для Vulkan NDC (где +Y направлен вниз)
        game_cameraC.proj[1][1] *= -1.0f
    }

    return unsafe(addr(game_cameraC))
}


def start_load(){
    var counter : int
    query() $[REQUIRE_NOT(mesh, material)] (eid : EntityId, let transform : Transform, let meshGuid : uint64, let materialGuid : uint64){
        counter++
        if (ResourceIsReadyMesh(meshGuid) && ResourceIsReadyMaterial(materialGuid)){
            var mesh_ptr = GetMeshPointer(meshGuid)
            var mat_ptr = GetMaterialPointer(materialGuid)
            print("ура, загрузились, обновляемся.\n")
            print("кстати guid mat {materialGuid:d}, mesh - {meshGuid:d} \n")
            update_entity(eid) @(eid, cmp) {
                cmp |> set("mesh", mesh_ptr)
                cmp |> set("material", mat_ptr)
            }
        }
    }
    if (counter > 0){
        print("пытаемся загрузиться для {counter} объектов\n")
    }

    commit()
}