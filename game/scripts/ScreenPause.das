options gen2
options relaxed_assign = false

module ScreenPause shared public

//! Pause menu: a layer over the HUD. `Backdrop` swallows the pointer, so the HUD underneath is
//! visible but not clickable. The layer is opened and closed by `GameState::TogglePause`, which
//! also flips the clock flag — a paused world and a visible menu always agree.

require tryengine/Ui
require trygame/GameState
require trygame/UiTheme
require trygame/UiWidgets

def private OnResume() {
    GameState::TogglePause()
}

def private BuildPause() {
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

[init]
def private RegisterScreen() {
    Ui::RegisterScreen("pause", @@BuildPause)
}
