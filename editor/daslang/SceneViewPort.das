options gen2
options _allow_imgui_legacy = true

module SceneViewPort shared

require imgui
require imgui/imgui_widgets_builtin
require imgui/imgui_containers_builtin

require tryeditor/AssetBrowser
require tryeditor

var a : AssetItem
var scene_viewport_size : float2 = float2(600, 600)
var scene_viewport_mouse : Mouse

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


[export]
def GetEditorViewportSize : float2 {
    return scene_viewport_size;
    // return size / float2(6, 6);
}

def DrawSceneViewPort(){
    window(SCENE_VIEW_WND, (text = "SceneView", closable = false, flags = ImGuiWindowFlags.None)) {
        let img = get_image()
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
            scene_viewport_mouse.is_down = is_hovered && IsMouseDown(ImGuiMouseButton.Left)
            scene_viewport_mouse.pressed = is_hovered && IsMouseClicked(ImGuiMouseButton.Left)


            drag_drop_target(TARGET_DD) {
                let payload = AcceptDragDropPayload("Asset", ImGuiDragDropFlags.None)
                if (payload != null) {
                    unsafe {
                        a = (reinterpret<AssetItem? const>(payload.Data))[0]
                    }
                    print("Drag and Drop {a.name} \n")
                }
            }
        }
    }
}