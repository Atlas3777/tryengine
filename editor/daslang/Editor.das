options gen2

module Editor shared public

require tryeditor/Hierarchy
require tryeditor/Inspector
require tryeditor/AssetBrowser
require tryeditor/SceneViewPort

require imgui
require imgui/imgui_boost_runtime
require tryeditor

[export]
def draw_editor() {
    unsafe {
        let ctx_address = get_editor_imgui_context()
        let host_ctx = reinterpret<ImGuiContext?>(ctx_address)
        SetCurrentContext(host_ctx)
    }
    begin_frame()


    DrawHierarchy()
    DrawInspector()
    DrawAssetBrowser()
    DrawSceneViewPort()

    end_of_frame()
}
