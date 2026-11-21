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

// Per-entity load bookkeeping. `LoadMarker` is a plain struct without [DecsSerializable],
// so the component has no writer: scene/prefab save skip it and the Inspector hides it.
struct LoadMarker {
    guid : uint64 = 0ul
}

let public MESH_LOAD_FIELD : string = "meshLoad"
let public MATERIAL_LOAD_FIELD : string = "materialLoad"

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

[export]
def get_render_obj() : array<DrawCall>? {
    start_load()
    dcs.clear()

    query() <| $(eid : EntityId, _world : WorldTransform, mesh : TryMesh?, material : TryMat?) {
        // Пока новый материал грузится, указателя может не быть — не отдаём полупустой draw call.
        if (mesh == null || material == null) {
            return
        }
        let id = unsafe(reinterpret<uint64>(eid))
        dcs.push(DrawCall(
            mesh_id = mesh,
            material_id = material,
            matrix = compose(_world.position, _world.rotation, _world.scale),
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


def start_load(){
    // Загрузка меша и материала разведена: материал не ждёт готовности меша.
    // meshLoad/materialLoad — runtime-only: LoadMarker не имеет writer'а, поэтому
    // сцена/префаб их не сохраняют, а инспектор не рисует. Они хранят guid, для
    // которого загрузка уже запрошена, чтобы не дёргать RequestLoad каждый кадр.
    query() $(eid : EntityId, let meshGuid : uint64) {
        let guid = meshGuid
        var has_mesh = false
        query(eid) $(let mesh : TryMesh?) {
            has_mesh = mesh != null
        }
        var load_guid = 0ul
        var has_load = false
        query(eid) $(let meshLoad : LoadMarker) {
            load_guid = meshLoad.guid
            has_load = true
        }

        if (guid == 0ul) {
            if (has_mesh || has_load) {
                update_entity(eid) @(eid, cmp) {
                    decs::remove(cmp, "mesh")
                    decs::remove(cmp, MESH_LOAD_FIELD)
                }
            }
            return
        }

        if (has_mesh && has_load && load_guid == guid) {
            return
        }

        // A pointer is only ever set together with its marker. A pointer without one is
        // stale, so it is dropped instead of trusted.
        let changed = has_mesh && (!has_load || load_guid != guid)
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
            cmp |> set(MESH_LOAD_FIELD, LoadMarker(guid = guid))
            if (ready) {
                cmp |> set("mesh", mesh_ptr)
            }
        }
        if (!ready && !requested) {
            RequestLoadMesh(guid)
        }
    }

    query() $(eid : EntityId, let materialRef : MaterialRef) {
        let material_guid = ResolveAssetRef(materialRef.path)
        var has_material = false
        query(eid) $(let material : TryMat?) {
            has_material = material != null
        }
        var load_guid = 0ul
        var has_load = false
        query(eid) $(let materialLoad : LoadMarker) {
            load_guid = materialLoad.guid
            has_load = true
        }

        if (material_guid == 0ul) {
            // Пустая ссылка — материал снят. Непустая, но не резолвится (реестр ещё
            // сканируется или в поле вписан неизвестный путь): оставляем прошлый
            // материал и повторяем на следующем кадре, иначе объект навсегда исчезнет.
            if (length(materialRef.path) == 0) {
                if (has_material || has_load) {
                    update_entity(eid) @(eid, cmp) {
                        decs::remove(cmp, "material")
                        decs::remove(cmp, MATERIAL_LOAD_FIELD)
                    }
                }
            }
            return
        }

        if (has_material && has_load && load_guid == material_guid) {
            return
        }

        // A pointer without a marker is stale by definition. When materialRef changes, the
        // old pointer is dropped right away so the frame does not draw the previous material;
        // the new one is installed once ResourceIsReadyMaterial returns true.
        let changed = has_material && (!has_load || load_guid != material_guid)
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
            cmp |> set(MATERIAL_LOAD_FIELD, LoadMarker(guid = material_guid))
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
