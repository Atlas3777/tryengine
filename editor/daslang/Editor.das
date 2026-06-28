options gen2

module Editor shared public
require imgui
require imgui/imgui_widgets_builtin
require imgui/imgui_containers_builtin
require imgui/imgui_boost_runtime
require imgui/imgui_boost_v2
require tryeditor

[export]
def ren() {
    // 1. Синхронизируем контекст ImGui с хост-компонентом движка
    unsafe {
        let ctx_address = get_editor_imgui_context()
        let host_ctx = reinterpret<ImGuiContext?>(ctx_address)
        SetCurrentContext(host_ctx)
    }
    begin_frame()
    if (button(SAVE, (text = "Save"))) {
        // one-shot handler — runs once per click
        print("sfa")
    }
    // OR drive on accumulated count:
    text("saves: {SAVE.click_count}")

    end_of_frame()
}