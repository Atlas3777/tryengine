options gen2
options relaxed_assign = false

module ScreenPause shared public

//! Pause menu: a layer over the HUD. `Backdrop` swallows the pointer, so the HUD underneath is
//! visible but not clickable. The layer is pushed and removed by the HUD when the clock flag
//! changes; Resume flips the flag and pops the layer, so the menu and the paused world agree.

require tryengine/AssetMap
require tryengine/Ui
require trygame/GameScreens
require trygame/GameState
require trygame/UiTheme
require trygame/UiWidgets

//! Bundle of this screen; the scene request loads the same path.
let public kPauseBundle : string = "data/bundles/pause.bundle"

def private OnResume() {
    GameState::TogglePause()
    GameScreens::Pop()
}

def public Draw(_assets : AssetMap) {
    Ui::Backdrop("##pause", UiTheme::kPauseCurtain) {
        Ui::Panel("##menu", UiTheme::kPausePanel, UiLayout(anchor = float2(0.5f, 0.5f), spacing = 12.0f)) {
            Ui::Label("##title", "Paused", UiTheme::kTitle)
            UiWidgets::MeterRow("##hp", "HP", GameState::g_state.hp, GameState::g_state.hp_max, UiTheme::kHpBar,
                                GameState::HpText())
            UiWidgets::StatRow("##volume_row", "Volume", "{int(GameState::g_state.volume * 100.0f)}%")
            Ui::Slider("##volume", GameState::g_state.volume, 0.0f, 1.0f)
            Ui::Button(UiTheme::kPrimaryButton, "##resume", "Resume", @@OnResume)
        }
    }
}
