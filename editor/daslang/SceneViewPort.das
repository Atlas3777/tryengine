options gen2
options _allow_imgui_legacy = true

module SceneViewPort shared

require imgui
require imgui/imgui_widgets_builtin
require imgui/imgui_containers_builtin

require tryeditor/AssetBrowser
require tryeditor

var a : AssetItem

def DrawSceneViewPort(){
    window(SCENE_VIEW_WND, (text = "SceneView", closable = false, flags = ImGuiWindowFlags.None)) {
        let img = get_image()
        if (img != null) {
            image(SCENE_VIEW, (user_texture_id = ImTextureRef(intptr(img)),
                              size = float2(200.0f, 200.0f),
                              uv0 = float2(0.0f, 0.0f),
                              uv1 = float2(1.0f, 1.0f),
                              tint_col = float4(1.0f, 1.0f, 1.0f, 1.0f),
                              border_col = float4(0.0f, 0.0f, 0.0f, 0.0f)))

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