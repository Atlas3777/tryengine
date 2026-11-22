options gen2
options relaxed_assign = false
options no_unused_block_arguments = false

module UiSmoke shared public

//! Headless test of the retained UI: no window, no GPU, injected mouse. It exercises the
//! parts that can be wrong without looking at pixels — id addressing, hit-testing, the
//! press/click state machine, sound events, `var` write-back and the draw command list.
//!
//! Run with tools/agent/ui-smoke.sh. A failure prints every failed check and panics, so the
//! exit code is non-zero.

require strings
require math
require tryengine/Ui

var private g_failures : int = 0
var private g_checks : int = 0
var private g_clicks : int = 0
var private g_button_seen : bool = false
var private g_sounds : array<string>
var private g_hover_enters : int = 0
var private g_slider : float = 0.25f
var private g_checked : bool = false

def private Fail(what : string) {
    g_failures++
    print("FAIL: {what}\n")
}

def private Check(ok : bool; what : string) {
    g_checks++
    if (!ok) {
        Fail(what)
    }
}

def private HasSound(path : string) : bool {
    for (sound in g_sounds) {
        if (sound == path) {
            return true
        }
    }
    return false
}

def private OnButton() {
    g_clicks++
}

def private OnUiSound(path : string; node_id : uint64) {
    g_sounds |> push(path)
    if (path == "ui/hover") {
        g_hover_enters++
    }
}

let private kButtonStyle = UiStyle(
    color = float4(0.2f, 0.3f, 0.5f, 1.0f),
    hover_color = float4(0.3f, 0.4f, 0.6f, 1.0f),
    text_color = float4(1.0f, 1.0f, 1.0f, 1.0f),
    padding = float4(10.0f, 6.0f, 10.0f, 6.0f),
    sound_hover = "ui/hover",
    sound_click = "ui/click"
)

def private Build() {
    Ui::Frame() {
        Ui::Group("##root", UiStyle(), UiLayout(offset = float2(40.0f, 40.0f), spacing = 6.0f)) {
            Ui::Label("##hello", "hello", UiStyle(text_size = 16.0f))
            if (Ui::Button("##button", "Fire", @@OnButton, kButtonStyle)) {
                g_button_seen = true
            }
            Ui::Checkbox("##check", g_checked, UiStyle())
            Ui::Slider("##slider", g_slider, 0.0f, 1.0f, UiStyle())
        }
    }
}

def private Center(rect : float4) : float2 {
    return float2(rect.x + rect.z * 0.5f, rect.y + rect.w * 0.5f)
}

[init]
def run_smoke() {
    Ui::SetInputViewport(float2(0.0f, 0.0f), float2(800.0f, 600.0f), true)
    Ui::OnSound(@@OnUiSound)

    // Frame 1: layout with the pointer far away.
    Ui::InjectInput(float2(-1000.0f, -1000.0f), false, false, false)
    Build()
    Check(Ui::DebugCommandCount() > 0, "the frame emits draw commands")
    Check(Ui::DebugCommandKind(0) == UIDC_QUAD, "the first command is a filled rect")

    let button = Ui::DebugRect("##root/##button")
    let checkbox = Ui::DebugRect("##root/##check")
    let slider = Ui::DebugRect("##root/##slider")
    Check(button.z > 0.0f && button.w > 0.0f, "the button has a non-empty rect")
    Check(checkbox.z > 0.0f && checkbox.w > 0.0f, "the checkbox has a non-empty rect")
    Check(slider.z > 0.0f && slider.w > 0.0f, "the slider has a non-empty rect")
    Check(button.y > Ui::DebugRect("##root/##hello").y, "flow puts the button below the label")

    // Frame 2: hover the button. The hover sound fires exactly once on enter.
    let button_center = Center(button)
    Ui::InjectInput(button_center, false, false, false)
    Build()
    Check(g_hover_enters == 1, "the hover sound fires once on enter")
    Check(Ui::HotId() != 0ul, "an interactive node is hot under the pointer")
    Check(Ui::ConsumesMouse(), "the UI consumes the pointer over an interactive node")

    // Frame 3: press without release.
    Ui::InjectInput(button_center, true, true, false)
    Build()
    Check(g_clicks == 0, "a press alone does not invoke the action")
    Check(Ui::ActiveId() != 0ul, "the pressed node becomes active")

    // Frame 4: release over the button.
    Ui::InjectInput(button_center, false, false, true)
    Build()
    Check(g_clicks == 1, "the click invokes the action exactly once")
    Check(g_button_seen, "Button returns true on its click frame")
    Check(HasSound("ui/click"), "the click sound is emitted")
    Check(!HasSound("ui/press"), "no press sound when the style does not set one")

    // Frame 5: nothing.
    Ui::InjectInput(button_center, false, false, false)
    Build()
    Check(g_clicks == 1, "the click is not repeated without a new press")

    // Checkbox: press + release toggles the caller's variable.
    let checkbox_center = Center(checkbox)
    Ui::InjectInput(checkbox_center, true, true, false)
    Build()
    Ui::InjectInput(checkbox_center, false, false, true)
    Build()
    Check(g_checked, "the checkbox writes its value back")

    // Slider: press at 20%, drag to 80%, release.
    let slider_low = float2(slider.x + slider.z * 0.2f, slider.y + slider.w * 0.5f)
    let slider_high = float2(slider.x + slider.z * 0.8f, slider.y + slider.w * 0.5f)
    Ui::InjectInput(slider_low, true, true, false)
    Build()
    Ui::InjectInput(slider_high, true, false, false)
    Build()
    Check(g_slider > 0.6f, "the slider writes the dragged value back")
    Ui::InjectInput(slider_high, false, false, true)
    Build()
    Check(Ui::ActiveId() == 0ul, "the release clears the active node")

    // Watchers: first value counts as a change, equal values do not, a new one does.
    Check(Ui::WatchFloat("smoke", 1.0f), "a watcher reports the first value as a change")
    Check(!Ui::WatchFloat("smoke", 1.0f), "a watcher stays quiet for an unchanged value")
    Check(Ui::WatchFloat("smoke", 2.0f), "a watcher reports a changed value")

    // Duplicate ids in one frame: diagnosed, not fatal.
    Ui::Frame() {
        Ui::Label("##dup", "first")
        Ui::Label("##dup", "second")
    }

    // One-shot highlight must not break the next frame.
    Ui::Pulse("##root")
    Ui::InjectInput(float2(-1000.0f, -1000.0f), false, false, false)
    Build()
    Check(Ui::DebugCommandCount() > 0, "the frame after Pulse still draws")

    // Input outside the viewport cannot interact with anything.
    Ui::SetInputViewport(float2(0.0f, 0.0f), float2(800.0f, 600.0f), false)
    Ui::InjectInput(button_center, true, true, false)
    Build()
    Check(Ui::HotId() == 0ul, "an unfocused viewport has no hot node")
    Ui::SetInputViewport(float2(0.0f, 0.0f), float2(800.0f, 600.0f), true)

    if (g_failures != 0) {
        panic("ui-smoke failed: {g_failures} of {g_checks} checks")
    }
    print("UI SMOKE OK ({g_checks} checks)\n")
}
