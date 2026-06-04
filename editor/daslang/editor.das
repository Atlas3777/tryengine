options gen2

require imgui
require imgui/imgui_boost_v2
require imgui/imgui_widgets_builtin
require imgui/imgui_containers_builtin
require imgui/imgui_visual_aids

// Подключаем наш новый C++ модуль
require tryeditor

[export]
def ren() {
    // Синхронизируем контексты перед вызовом любых UI методов!
  unsafe {
          let ctx_address = get_editor_imgui_context()
          let host_ctx = reinterpret<ImGuiContext?>(ctx_address)
          SetCurrentContext(host_ctx)
      }

    // Родной код Dear ImGui & Boost теперь будет рисовать в контекст движка
    SetNextWindowSize(ImVec2(560.0, 240.0), ImGuiCond.Always)
    window(BASICS_WIN, (text = "Boost basics", closable = false,
                        flags = ImGuiWindowFlags.None)) {
        // Slider — VOLUME state lives in a generated daslang global.
        VOLUME.bounds = (0.0f, 1.0f)
        slider_float(VOLUME, (text = "Volume"))
        text("volume = {VOLUME.value}")

        spacing(BB_SP_1)

        // Button — BUMP_BTN.click_count is updated on every click.
        if (button(BUMP_BTN, (text = "Bump"))) {
            print("bump clicked\n")
        }
        text("bumps = {BUMP_BTN.click_count}")
    }
}