options gen2
options relaxed_assign = false

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
        // Пока новый материал грузится, указателя может не быть — не отдаём полупустой draw call.
        if (mesh == null || material == null) {
            return
        }
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
    // Загрузка меша и материала разведена: материал не ждёт готовности меша.
    // _meshLoadGuid/_materialLoadGuid — runtime-only компоненты (имя с "_" не
    // сохраняется в сцену и не показывается в инспекторе). Они хранят guid,
    // для которого загрузка уже запрошена, чтобы не дёргать RequestLoad каждый кадр.
    query() $(eid : EntityId, let meshGuid : uint64) {
        let guid = meshGuid
        var has_mesh = false
        query(eid) $(let mesh : TryMesh?) {
            has_mesh = mesh != null
        }
        var load_guid = 0ul
        var has_load = false
        query(eid) $(let _meshLoadGuid : uint64) {
            load_guid = _meshLoadGuid
            has_load = true
        }

        if (guid == 0ul) {
            if (has_mesh || has_load) {
                update_entity(eid) @(eid, cmp) {
                    decs::remove(cmp, "mesh")
                    decs::remove(cmp, "_meshLoadGuid")
                }
            }
            return
        }

        // Старый указатель без маркера: считаем, что он соответствует meshGuid.
        if (has_mesh && !has_load) {
            update_entity(eid) @(eid, cmp) {
                cmp |> set("_meshLoadGuid", guid)
            }
            return
        }
        if (has_mesh && has_load && load_guid == guid) {
            return
        }

        let changed = has_load && load_guid != guid
        let requested = has_load && load_guid == guid
        let ready = ResourceIsReadyMesh(guid)
        var mesh_ptr : TryMesh? = null
        if (ready) {
            mesh_ptr = GetMeshPointer(guid)
        }
        update_entity(eid) @(eid, cmp) {
            if (changed) {
                decs::remove(cmp, "mesh")
            }
            cmp |> set("_meshLoadGuid", guid)
            if (ready) {
                cmp |> set("mesh", mesh_ptr)
            }
        }
        if (!ready && !requested) {
            RequestLoadMesh(guid)
        }
    }

    query() $(eid : EntityId, let materialRef : string) {
        let material_guid = ResolveAssetRef(materialRef)
        var has_material = false
        query(eid) $(let material : TryMat?) {
            has_material = material != null
        }
        var load_guid = 0ul
        var has_load = false
        query(eid) $(let _materialLoadGuid : uint64) {
            load_guid = _materialLoadGuid
            has_load = true
        }

        if (material_guid == 0ul) {
            // Пустая ссылка — материал снят. Непустая, но не резолвится (реестр ещё
            // сканируется или в поле вписан неизвестный путь): оставляем прошлый
            // материал и повторяем на следующем кадре, иначе объект навсегда исчезнет.
            if (length(materialRef) == 0) {
                if (has_material || has_load) {
                    update_entity(eid) @(eid, cmp) {
                        decs::remove(cmp, "material")
                        decs::remove(cmp, "_materialLoadGuid")
                    }
                }
            }
            return
        }

        // Старый указатель без маркера: считаем, что он соответствует materialRef.
        if (has_material && !has_load) {
            update_entity(eid) @(eid, cmp) {
                cmp |> set("_materialLoadGuid", material_guid)
            }
            return
        }
        if (has_material && has_load && load_guid == material_guid) {
            return
        }

        // Новый материал: старый указатель снимаем сразу, чтобы кадр не рисовал
        // прошлый материал; новый подставится, когда ResourceIsReadyMaterial.
        let changed = has_load && load_guid != material_guid
        let requested = has_load && load_guid == material_guid
        let ready = ResourceIsReadyMaterial(material_guid)
        var mat_ptr : TryMat? = null
        if (ready) {
            mat_ptr = GetMaterialPointer(material_guid)
        }
        update_entity(eid) @(eid, cmp) {
            if (changed) {
                decs::remove(cmp, "material")
            }
            cmp |> set("_materialLoadGuid", material_guid)
            if (ready) {
                cmp |> set("material", mat_ptr)
            }
        }
        if (!ready && !requested) {
            RequestLoadMaterial(material_guid)
        }
    }

    commit()
}
