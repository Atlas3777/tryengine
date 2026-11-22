options gen2
options relaxed_assign = false
options no_unused_block_arguments = false

module GameUi shared public

//! The game's screens.
//!
//! Three layers, three files:
//! - widgets — `tryengine/Ui` (`Panel`, `Label`, `Button`, ...; they repeat everywhere);
//! - look — `trygame/UiTheme` (per-kind theme, named styles, button templates, sound names);
//! - screens — this file. Each screen is a build function registered by name with
//!   `Ui::RegisterScreen`, and `Frame` decides which one is live this frame.
//!
//! A screen is drawn by name, so the editor can draw any registered screen without Play and
//! without knowing the game's logic. The functions below read game state in the declaration and
//! write it back through `@@` actions and `var` binders; they never touch colours — those come
//! from the theme.
//!
//! Everything runs on the UI clock (`tryengine/Ui` owns it), so gameplay pausing does not freeze
//! the screens.

require math
require tryengine/Ui
require trygame/UiTheme
require trygame/UiWidgets

struct private GameUiState {
    hp : float = 100.0f
    hp_max : float = 100.0f
    volume : float = 0.6f
    paused : bool = false
    clicks : int = 0
    damage_taken : int = 0
}

var private g_state = GameUiState()

// A pulse has to be requested while the node is declared: click callbacks run after the tree is
// built, and their id scope is already gone. The button sets the flag, the HUD consumes it.
var private g_pulse : bool = false

def private OnFire() {
    g_state.clicks++
    g_state.hp = min(g_state.hp_max, g_state.hp + 4.0f)
}

def private OnDamage() {
    g_state.damage_taken++
    g_state.hp = max(0.0f, g_state.hp - 20.0f)
    g_pulse = true
}

def private OnResume() {
    g_state.paused = false
}

def private PauseLabel(paused : bool) : string {
    if (paused) {
        return "Paused"
    }
    return "Running"
}

//! HUD screen: the in-game overlay. Pausing switches to the `pause` screen.
def private BuildHud() {
    Ui::Panel("##hud", UiTheme::kHudPanel, UiLayout(offset = float2(24.0f, 24.0f), spacing = 8.0f)) {
        Ui::Label("##title", "tryengine UI", UiTheme::kTitle)
        // Composite widgets: the health meter and the volume row also appear on the pause screen.
        UiWidgets::MeterRow("##hp", "HP", g_state.hp, g_state.hp_max, UiTheme::kHpBar,
                            "{int(g_state.hp)} / {int(g_state.hp_max)}")
        Ui::Row("##buttons", UiStyle(), UiLayout(spacing = 8.0f)) {
            Ui::Button(UiTheme::kPrimaryButton, "##fire", @@OnFire)
            Ui::Button(UiTheme::kDangerButton, "##damage", @@OnDamage)
        }
        Ui::Row("##settings", UiStyle(), UiLayout(spacing = 8.0f)) {
            Ui::Checkbox("##paused", g_state.paused)
            Ui::Label("##paused_label", PauseLabel(g_state.paused), UiTheme::kMuted)
        }
        UiWidgets::StatRow("##volume_row", "Volume", "{int(g_state.volume * 100.0f)}%")
        Ui::Slider("##volume", g_state.volume, 0.0f, 1.0f)
    }
    Ui::Label("##counter", "Fire {g_state.clicks} | Damage {g_state.damage_taken}", UiTheme::kMuted,
              UiLayout(anchor = float2(0.0f, 1.0f), offset = float2(24.0f, -40.0f)))

    if (g_pulse) {
        g_pulse = false
        Ui::Pulse("##hud")
    }
}

//! Pause screen: a curtain over the frozen scene, its own widgets and its own ids. It shares the
//! theme (and the volume slider's variable) with the HUD, nothing else.
def private BuildPause() {
    Ui::Group("##pause", UiTheme::kPauseCurtain, UiLayout(size = Ui::ViewportSize())) {
        Ui::Panel("##menu", UiTheme::kPausePanel, UiLayout(anchor = float2(0.5f, 0.5f), spacing = 12.0f)) {
            Ui::Label("##title", "Paused", UiTheme::kTitle)
            UiWidgets::MeterRow("##hp", "HP", g_state.hp, g_state.hp_max, UiTheme::kHpBar,
                                "{int(g_state.hp)} / {int(g_state.hp_max)}")
            UiWidgets::StatRow("##volume_row", "Volume", "{int(g_state.volume * 100.0f)}%")
            Ui::Slider("##volume", g_state.volume, 0.0f, 1.0f)
            Ui::Button(UiTheme::kPrimaryButton, "##resume", "Resume", @@OnResume)
        }
    }
}

// The registry lives in the script context, so it is filled when the module initializes — the
// editor's Screens panel can list the screens before Play has ever started.
[init]
def private RegisterScreens() {
    Ui::RegisterScreen("hud", @@BuildHud)
    Ui::RegisterScreen("pause", @@BuildPause)
}

//! Declares the live screen for this frame. Which screen is live is game state, one line here.
def public Frame() {
    if (g_state.paused) {
        Ui::FrameScreen("pause")
    } else {
        Ui::FrameScreen("hud")
    }
}

//! Game-side state the screens render; the game may also own it and pass values into a `Label`.
def public Hp() : float {
    return g_state.hp
}

def public SetHp(value : float) {
    g_state.hp = clamp(value, 0.0f, g_state.hp_max)
}

def public IsPaused() : bool {
    return g_state.paused
}
