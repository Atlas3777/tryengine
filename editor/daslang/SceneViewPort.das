options gen2
options relaxed_assign = false
options _allow_imgui_legacy = true

module SceneViewPort shared

require tryengine/Profiling
require tryengine/AsyncTaskManager
require tryengine/Prefab
require tryengine/Types
require daslib/decs
require daslib/decs_boost
require daslib/async_boost
require math
require imgui
require imgui/imgui_widgets_builtin
require imgui/imgui_containers_builtin

require TryEditor
require tryeditor/EditorInput
require tryeditor/FileSystemUtils
require tryeditor/Hierarchy
require tryeditor/Gizmo

var scene_viewport_size : float2 = float2(600, 600)
var scene_viewport_mouse : Mouse

var scene_game_viewport_size : float2 = float2(600, 600)

[cpp_layout]
struct Mouse{
    pos : float2
    pressed : bool
    is_down : bool
}

[export]
def GetEditorSceneViewportMouse : Mouse ? {
    // print("scene_viewport_mouse : {scene_viewport_mouse} \n")
    return unsafe(addr(scene_viewport_mouse))
}

def ResetMouse(){
    scene_viewport_mouse.is_down = false
    scene_viewport_mouse.pressed = false
    scene_viewport_mouse.pos = float2(0, 0)
}

// Screen point under the cursor -> world point on the Z=0 ground plane. The editor
// camera looks along its local +Y with +Z up (see Mover and GetEditorCamera), so the
// ray is built in that basis. When the camera looks above the horizon there is no
// ground hit; spawn a bit in front of the camera instead of failing the drop.
def private GetViewportDropPoint() : float3 {
    var origin = float3(0.0f, 0.0f, 0.0f)
    var direction = float3(0.0f, 1.0f, 0.0f)

    query() $(let _world : Transform, let camera : Camera; let editorCameraTag : bool) {
        let size = float2(max(scene_viewport_size.x, 1.0f), max(scene_viewport_size.y, 1.0f))
        let ndc_x = scene_viewport_mouse.pos.x / size.x * 2.0f - 1.0f
        let ndc_y = 1.0f - scene_viewport_mouse.pos.y / size.y * 2.0f
        let half_tan = tan(radians(camera.fov) * 0.5f)

        let forward = quat_mul_vec(_world.rotation, float3(0.0f, 1.0f, 0.0f))
        let up = quat_mul_vec(_world.rotation, float3(0.0f, 0.0f, 1.0f))
        let right = quat_mul_vec(_world.rotation, float3(1.0f, 0.0f, 0.0f))

        origin = _world.position
        direction = normalize(forward + right * (ndc_x * half_tan * size.x / size.y) + up * (ndc_y * half_tan))
    }

    if (abs(direction.z) > 1e-5f) {
        let t = -origin.z / direction.z
        if (t > 0.0f) {
            return origin + direction * t
        }
    }
    return origin + direction * 10.0f
}

[async] def private SpawnDroppedPrefab(path : string; position : float3) : void {
    let eid : EntityId = await(Prefab::SpawnPrefab(path, position))
    if (eid != INVALID_ENTITY_ID) {
        selected_eid = eid
    }
}

// .prefab files spawn as is. A model asset spawns from the editor artifact written by
// GltfImporter: runtime cannot spawn glTF, and the artifact is not packed.
def private HandleAssetDrop(item : AssetItem; position : float3) {
    let prefab_ext = asset_ext_key(".prefab")
    let glb_ext = asset_ext_key(".glb")
    let gltf_ext = asset_ext_key(".gltf")

    var path = ""
    if (item.ext == prefab_ext) {
        path = item.path
    } elif (item.ext == glb_ext || item.ext == gltf_ext) {
        path = GetModelPrefabArtifactPath(item.path, item.guid)
        if (empty(path)) {
            print("[VIEWPORT] model '{item.name}' has no editor prefab artifact; reimport the asset\n")
            return
        }
    } else {
        print("[VIEWPORT] cannot spawn '{item.name}': drop a .prefab or a glTF model\n")
        return
    }

    if (empty(path)) {
        return
    }
    StartTask(SpawnDroppedPrefab(path, position))
}


[export]
def GetEditorViewportSize : float2 {
    return scene_viewport_size;
    // return size / float2(6, 6);
}

[export]
def GetGameViewportSize : float2 {
    return scene_game_viewport_size;
    // return size / float2(6, 6);
}

[profile]
def DrawSceneViewPort(){
    SetSceneViewportHovered(false)

    window(SCENE_VIEW_WND, (text = "SceneView", closable = false, flags = ImGuiWindowFlags.None)) {
        let img = GetEditorCameraViewImage()
        scene_viewport_size = GetContentRegionAvail()
        ResetMouse()

        if (img != null) {
            image(SCENE_VIEW, (user_texture_id = ImTextureRef(intptr(img)),
                              size = scene_viewport_size,
                              uv0 = float2(0.0f, 0.0f),
                              uv1 = float2(1.0f, 1.0f),
                              tint_col = float4(1.0f, 1.0f, 1.0f, 1.0f),
                              border_col = float4(0.0f, 0.0f, 0.0f, 0.0f)))

            // 1. Получаем экранные координаты мыши и верхнего левого угла картинки
            let mouse_screen_pos = GetMousePos()
            let image_top_left = GetItemRectMin()

            // 2. Локальные координаты относительно вьюпорта (0, 0 в левом верхнем углу)
            scene_viewport_mouse.pos = mouse_screen_pos - image_top_left

            // 3. Обновляем состояние клика (только если мышь находится над вьюпортом)
            let is_hovered = IsItemHovered()
            SetSceneViewportHovered(is_hovered)
            scene_viewport_mouse.is_down = is_hovered && IsMouseDown(ImGuiMouseButton.Left)
            scene_viewport_mouse.pressed = is_hovered && IsMouseClicked(ImGuiMouseButton.Left)

            // The gizmo consumes its own clicks; PickingPass skips selection while a handle
            // drag is active. `down` is not tied to hover so a drag keeps running when the
            // cursor leaves the viewport.
            SetGizmoViewport(scene_viewport_size)
            UpdateGizmoInteraction(scene_viewport_mouse.pos, scene_viewport_size, is_hovered,
                                   IsMouseClicked(ImGuiMouseButton.Left), IsMouseDown(ImGuiMouseButton.Left))

            drag_drop_target(TARGET_DD) {
                let payload = AcceptDragDropPayload("Asset", ImGuiDragDropFlags.None)
                if (payload != null) {
                    var dropped : AssetItem
                    unsafe {
                        dropped = (reinterpret<AssetItem? const>(payload.Data))[0]
                    }
                    HandleAssetDrop(dropped, GetViewportDropPoint())
                }
            }
        }
    }
}

[profile]
def DrawGameSceneViewPort(){
    SetGameViewportFocused(false)

    window(SCENE_GAME_VIEW_WND, (text = "SceneGameView", closable = false, flags = ImGuiWindowFlags.None)) {
        SetGameViewportFocused(IsWindowFocused())

        let img = GetGameCameraViewImage()
        scene_game_viewport_size = GetContentRegionAvail()

        if (img != null) {
            image(SCENE_VIEW, (user_texture_id = ImTextureRef(intptr(img)),
                              size = scene_game_viewport_size,
                              uv0 = float2(0.0f, 0.0f),
                              uv1 = float2(1.0f, 1.0f),
                              tint_col = float4(1.0f, 1.0f, 1.0f, 1.0f),
                              border_col = float4(0.0f, 0.0f, 0.0f, 0.0f)))
        }
    }
}