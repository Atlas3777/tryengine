options gen2
options relaxed_assign = false
options no_unused_block_arguments = false

module UiSmoke shared public

//! Headless test of the retained UI: no window, no GPU, injected mouse. It exercises the
//! parts that can be wrong without looking at pixels — id addressing, hit-testing, the
//! press/click state machine, sound events, `var` write-back and the draw command list, plus
//! the layers above the widgets: the theme, preview mode, and the image path that has no
//! texture behind it.
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

// --- screens and theme --------------------------------------------------------------------

def private ColorNear(a : float4; b : float4) : bool {
    return abs(a.x - b.x) < 1e-4f && abs(a.y - b.y) < 1e-4f && abs(a.z - b.z) < 1e-4f && abs(a.w - b.w) < 1e-4f
}

// --- preview mode and modal layers ---------------------------------------------------------

var private g_preview_clicks : int = 0
var private g_preview_button_true : bool = false
var private g_preview_checked : bool = false
var private g_preview_value : float = 0.25f
var private g_base_clicks : int = 0
var private g_modal_clicks : int = 0

def private OnPreviewClick() {
    g_preview_clicks++
}

def private OnBaseClick() {
    g_base_clicks++
}

def private OnModalClick() {
    g_modal_clicks++
}

def private BuildPreviewScreen() {
    Ui::Row("##row", UiStyle(), UiLayout(spacing = 6.0f)) {
        if (Ui::Button("##go", "Go", @@OnPreviewClick, kButtonStyle)) {
            g_preview_button_true = true
        }
        Ui::Checkbox("##check", g_preview_checked, UiStyle())
        Ui::Slider("##slider", g_preview_value, 0.0f, 1.0f, UiStyle())
    }
}

def private FramePreview() {
    Ui::Frame() {
        BuildPreviewScreen()
    }
}

def private ClickPreview(point : float2) {
    Ui::InjectInput(point, true, true, false)
    FramePreview()
    Ui::InjectInput(point, false, false, true)
    FramePreview()
}

def private RunPreviewChecks() {
    Ui::InjectInput(float2(-1000.0f, -1000.0f), false, false, false)
    FramePreview()

    let go = Ui::DebugRect("##row/##go")
    let check = Ui::DebugRect("##row/##check")
    let slider = Ui::DebugRect("##row/##slider")
    Check(go.z > 0.0f && check.z > 0.0f && slider.z > 0.0f, "the preview screen lays out its widgets")

    // Preview: the pointer still presses and hovers, nothing leaves the tree.
    Ui::SetPreviewMode(true)
    Check(Ui::IsPreviewing(), "preview mode reports itself")
    ClickPreview(Center(go))
    Check(g_preview_clicks == 0, "preview mode invokes no action")
    Check(!g_preview_button_true, "Button returns false in preview mode")

    ClickPreview(Center(check))
    Check(!g_preview_checked, "the checkbox does not write its variable in preview mode")

    let slider_low = float2(slider.x + slider.z * 0.2f, slider.y + slider.w * 0.5f)
    let slider_high = float2(slider.x + slider.z * 0.8f, slider.y + slider.w * 0.5f)
    Ui::InjectInput(slider_low, true, true, false)
    FramePreview()
    Ui::InjectInput(slider_high, true, false, false)
    FramePreview()
    Ui::InjectInput(slider_high, false, false, true)
    FramePreview()
    Check(g_preview_value == 0.25f, "the slider does not write its variable in preview mode")

    // Hover state is still tracked, so the preview looks alive.
    Ui::InjectInput(Center(go), false, false, false)
    FramePreview()
    Check(Ui::HotId() != 0ul, "preview mode still hit-tests")

    Ui::SetPreviewMode(false)
    Check(!Ui::IsPreviewing(), "preview mode turns off")
    ClickPreview(Center(go))
    Check(g_preview_clicks == 1, "the same click works once preview mode is off")
    Check(g_preview_button_true, "Button returns true outside preview mode")
}

def private RunImageChecks() {
    // A textured image with no texture behind it (guid 0, and no UiSystem in a headless run)
    // must emit nothing instead of a draw command with a null material.
    Ui::InjectInput(float2(-1000.0f, -1000.0f), false, false, false)
    Ui::Frame() {
        Ui::ImageTexture("##tex", 0ul, float2(32.0f, 32.0f))
    }
    Check(Ui::DebugCommandCount() == 0, "an image without a loaded texture emits no command")
}

def private RunThemeChecks() {
    let engine_theme = Ui::Theme()
    Check(engine_theme.panel.color.w > 0.0f, "the engine theme fills panels")
    Check(engine_theme.button.text_size > 0.0f, "the engine theme sizes button text")

    // A theme is partial: the kinds and fields it leaves unset keep the engine default.
    Ui::SetTheme(UiTheme(button = UiStyle(color = float4(0.1f, 0.2f, 0.3f, 1.0f), text_size = 21.0f)))
    let themed = Ui::Theme()
    Check(ColorNear(themed.button.color, float4(0.1f, 0.2f, 0.3f, 1.0f)), "SetTheme applies a kind override")
    Check(abs(themed.button.text_size - 21.0f) < 1e-4f, "SetTheme applies numeric fields")
    Check(ColorNear(themed.button.text_color, engine_theme.button.text_color),
          "an unset style field keeps the engine default")
    Check(ColorNear(themed.panel.color, engine_theme.panel.color), "an unset kind keeps the engine default")

    Ui::SetTheme(UiTheme())
    Check(ColorNear(Ui::Theme().button.color, engine_theme.button.color), "resetting the theme restores the engine look")
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

    RunThemeChecks()
    RunImageChecks()
    RunPreviewChecks()

    if (g_failures != 0) {
        panic("ui-smoke failed: {g_failures} of {g_checks} checks")
    }
    print("UI SMOKE OK ({g_checks} checks)\n")
}
