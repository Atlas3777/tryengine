options gen2
options relaxed_assign = false
options _allow_imgui_legacy = true

module UiScreens shared public

//! Editor view of the game's screens.
//!
//! The game registers its screens by name (`Ui::RegisterScreen` from `[init]`), so the editor can
//! list them without knowing the game. In Edit mode the selected screen is declared every frame
//! and drawn into the SceneGameView target — the same target Play uses, because the game UI pass
//! lives in the game scope. Play ignores the selection: there `GameUiFrame` picks the screen from
//! gameplay state.
//!
//! Edit mode is view only: the screen is drawn, but the game viewport reports itself unfocused, so
//! no widget reacts to the pointer (see DrawGameSceneViewPort).

require tryengine/Ui
require tryengine/Profiling
require math
require imgui
require imgui/imgui_widgets_builtin
require imgui/imgui_containers_builtin

var public preview_screen : string = "hud"

// One row per screen: the widget state is indexed by the screen name, so adding a screen at run
// time does not need a new call site.
var private g_screen_rows : table<string; ClickState>

//! One empty frame. The UI command list is cleared when a frame opens, so a screen left over from
//! Play — or a screen that was just deselected — disappears instead of freezing on screen.
def public ClearScreenFrame() {
    Ui::Begin()
    Ui::End()
}

//! The Edit-mode frame: the previewed screen, or nothing when no screen is selected. It runs in
//! `Ui::SetPreviewMode`, so the screen reacts to the pointer (hover, press, animations) while
//! `@@` actions, sound events and `var` write-backs are suppressed — the game is not running, and
//! a click in the preview must not touch game state.
def public DrawPreviewFrame() {
    Ui::SetPreviewMode(true)
    if (empty(preview_screen) || !Ui::HasScreen(preview_screen)) {
        ClearScreenFrame()
    } else {
        Ui::FrameScreen(preview_screen)
    }
    Ui::SetPreviewMode(false)
}

[profile]
def public DrawUiScreens(live : bool) {
    window(UI_SCREENS_WND, (text = "Screens", closable = false, flags = ImGuiWindowFlags.None)) {
        let names <- Ui::ScreenNames()
        if (length(names) == 0) {
            text("No screens registered.")
            text("The game registers them from [init]: Ui::RegisterScreen(\"hud\", @@BuildHud).")
        } else {
            if (live) {
                text("Play is running: the game picks the screen.")
                text("Stop Play to preview one here.")
            } else {
                text("Previewed in the game view:")
            }

            if (selectable_label(g_screen_rows["##none"], (text = "(none)", selected = empty(preview_screen)))) {
                preview_screen = ""
            }

            for (name in names) {
                if (selectable_label(g_screen_rows[name], (text = name, selected = name == preview_screen))) {
                    preview_screen = name
                }
            }
        }
    }
}
