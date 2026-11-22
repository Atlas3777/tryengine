options gen2
options relaxed_assign = false
options no_unused_block_arguments = false

module GameUi shared public

//! The game's screens and the screen stack that puts them on screen.
//!
//! Four pieces, four files:
//! - primitives — `tryengine/Ui` (`Panel`, `Label`, `Button`, `Backdrop`, ...);
//! - composite widgets — `trygame/UiWidgets` (`StatRow`, `MeterRow`);
//! - look — `trygame/UiTheme` (per-kind theme, named styles, button templates, sound names);
//! - screens and the stack — this file.
//!
//! A screen is a build function registered by name. Which screens are live is **game state**, not
//! engine state: `g_layers` below is a plain array in game code, and the UI module only knows how
//! to draw a screen by name. That keeps the engine free of a "current screen" concept and makes
//! stacking trivial — the stack order is the draw order.
//!
//! Everything runs on the UI clock (`tryengine/Ui` owns it). The editor freezes the whole game,
//! UI included, when it is paused: it simply does not declare a UI frame, so the last command
//! list stays on screen and nothing reacts to the pointer.

require math
require tryengine/Ui
require trygame/UiTheme
require trygame/UiWidgets

struct private GameUiState {
    hp : float = 100.0f
    hp_max : float = 100.0f
    volume : float = 0.6f
    gold : int = 50
    paused : bool = false
    clicks : int = 0
    damage_taken : int = 0
}

var private g_state = GameUiState()

// A pulse has to be requested while the node is declared: click callbacks run after the tree is
// built, and their id scope is already gone. The button sets the flag, the HUD consumes it.
var private g_pulse : bool = false

// --- screen stack ---------------------------------------------------------------------------

// Bottom layer first. The last layer is the top one and gets the pointer where layers overlap.
var private g_layers <- array<string>("hud")

def public HasLayer(name : string) : bool {
    for (layer in g_layers) {
        if (layer == name) {
            return true
        }
    }
    return false
}

def public TopLayer() : string {
    if (length(g_layers) == 0) {
        return ""
    }
    return g_layers[length(g_layers) - 1]
}

//! Replaces the stack with one screen.
def public Show(name : string) {
    if (!Ui::HasScreen(name)) {
        print("[GameUi] screen '{name}' is not registered\n")
        return
    }
    g_layers.clear()
    g_layers |> push(name)
}

//! Draws a screen on top of the current one: a shop, an inventory, a dialog. A layer that is
//! already open is not pushed twice — two copies in one frame would declare the same ids.
def public Push(name : string) {
    if (!Ui::HasScreen(name)) {
        print("[GameUi] screen '{name}' is not registered\n")
        return
    }
    if (HasLayer(name)) {
        return
    }
    g_layers |> push(name)
}

//! Removes the top layer. The bottom layer stays, so the game always has a screen.
def public Pop() {
    if (length(g_layers) > 1) {
        g_layers.resize(length(g_layers) - 1)
    }
}

def public Close(name : string) {
    var kept : array<string>
    for (layer in g_layers) {
        if (layer != name) {
            kept |> push(layer)
        }
    }
    if (length(kept) == 0) {
        kept |> push("hud")
    }
    g_layers <- kept
}

// --- actions --------------------------------------------------------------------------------

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

def private OnOpenShop() {
    Push("shop")
}

def private OnCloseShop() {
    Close("shop")
}

def private OnBuyHealth() {
    if (g_state.gold >= 10) {
        g_state.gold -= 10
        g_state.hp = min(g_state.hp_max, g_state.hp + 25.0f)
    }
}

// --- text -----------------------------------------------------------------------------------

def private PauseLabel(paused : bool) : string {
    if (paused) {
        return "Paused"
    }
    return "Running"
}

def private PhaseLabel() : string {
    if (g_state.paused) {
        return "Paused"
    }
    if (HasLayer("shop")) {
        return "Shop open"
    }
    return "Running"
}

def private HpText() : string {
    return "{int(g_state.hp)} / {int(g_state.hp_max)}"
}

// --- screens --------------------------------------------------------------------------------

//! HUD: the bottom layer. Widgets in three corners show how anchors work: the panel is pinned to
//! the top-left, the phase label to the top-right, the counter to the bottom-left.
def private BuildHud() {
    Ui::Panel("##hud", UiTheme::kHudPanel, UiLayout(anchor = float2(0.0f, 0.0f), offset = float2(24.0f, 24.0f), spacing = 8.0f)) {
        // A texture asset drawn directly, without a material file: the guid comes from the
        // asset's .meta, the size from the layout.
        Ui::Row("##brand", UiStyle(), UiLayout(spacing = 10.0f)) {
            Ui::ImageTexture("##logo", UiTheme::kLogoGuid, float2(48.0f, 48.0f))
            Ui::Column("##brand_text", UiStyle(), UiLayout(spacing = 2.0f)) {
                Ui::Label("##title", "tryengine UI", UiTheme::kTitle)
                Ui::Label("##subtitle", "hud screen", UiTheme::kMuted)
            }
        }
        // Composite widgets: the health meter and the volume row also appear on the pause screen.
        UiWidgets::MeterRow("##hp", "HP", g_state.hp, g_state.hp_max, UiTheme::kHpBar, HpText())
        Ui::Row("##buttons", UiStyle(), UiLayout(spacing = 8.0f)) {
            Ui::Button(UiTheme::kPrimaryButton, "##fire", @@OnFire)
            Ui::Button(UiTheme::kDangerButton, "##damage", @@OnDamage)
            Ui::Button(UiTheme::kQuietButton, "##shop", "Shop", @@OnOpenShop)
        }
        Ui::Row("##settings", UiStyle(), UiLayout(spacing = 8.0f)) {
            Ui::Checkbox("##paused", g_state.paused)
            Ui::Label("##paused_label", PauseLabel(g_state.paused), UiTheme::kMuted)
        }
        UiWidgets::StatRow("##volume_row", "Volume", "{int(g_state.volume * 100.0f)}%")
        Ui::Slider("##volume", g_state.volume, 0.0f, 1.0f)
    }

    // anchor.x = 1 pins the widget's right edge to the parent's right edge, so the offset is
    // negative to leave a margin. anchor.y = 0 keeps it at the top.
    Ui::Label("##phase", PhaseLabel(), UiTheme::kMuted,
              UiLayout(anchor = float2(1.0f, 0.0f), offset = float2(-24.0f, 24.0f)))

    // anchor.y = 1 pins the bottom edge; the negative Y offset lifts the label off it.
    Ui::Label("##counter", "Fire {g_state.clicks} | Damage {g_state.damage_taken}", UiTheme::kMuted,
              UiLayout(anchor = float2(0.0f, 1.0f), offset = float2(24.0f, -40.0f)))

    if (g_pulse) {
        g_pulse = false
        Ui::Pulse("##hud")
    }
}

//! Pause menu: a layer over the HUD. `Backdrop` swallows the pointer, so the HUD underneath is
//! visible but not clickable.
def private BuildPause() {
    Ui::Backdrop("##pause", UiTheme::kPauseCurtain) {
        Ui::Panel("##menu", UiTheme::kPausePanel, UiLayout(anchor = float2(0.5f, 0.5f), spacing = 12.0f)) {
            Ui::Label("##title", "Paused", UiTheme::kTitle)
            UiWidgets::MeterRow("##hp", "HP", g_state.hp, g_state.hp_max, UiTheme::kHpBar, HpText())
            UiWidgets::StatRow("##volume_row", "Volume", "{int(g_state.volume * 100.0f)}%")
            Ui::Slider("##volume", g_state.volume, 0.0f, 1.0f)
            Ui::Button(UiTheme::kPrimaryButton, "##resume", "Resume", @@OnResume)
        }
    }
}

//! Shop: a second layer over the HUD, on its own ids and with its own backdrop. Shows that two
//! screens can be on screen at once without either knowing about the other.
def private BuildShop() {
    Ui::Backdrop("##shop", UiTheme::kPauseCurtain) {
        Ui::Panel("##cart", UiTheme::kPausePanel, UiLayout(anchor = float2(0.5f, 0.5f), spacing = 12.0f)) {
            Ui::Label("##title", "Shop", UiTheme::kTitle)
            UiWidgets::StatRow("##gold_row", "Gold", "{g_state.gold}")
            Ui::Row("##actions", UiStyle(), UiLayout(spacing = 8.0f)) {
                Ui::Button(UiTheme::kPrimaryButton, "##buy", "Buy health", @@OnBuyHealth)
                Ui::Button(UiTheme::kQuietButton, "##close", "Close", @@OnCloseShop)
            }
        }
    }
}

// The registry lives in the script context, so it is filled when the module initializes — the
// editor's Screens panel can list the screens before Play has ever started.
[init]
def private RegisterScreens() {
    Ui::RegisterScreen("hud", @@BuildHud)
    Ui::RegisterScreen("pause", @@BuildPause)
    Ui::RegisterScreen("shop", @@BuildShop)
}

// The pause menu is a layer like any other: gameplay sets the flag, the stack follows it.
def private SyncPauseLayer() {
    if (g_state.paused && !HasLayer("pause")) {
        Push("pause")
    } elif (!g_state.paused && HasLayer("pause")) {
        Close("pause")
    }
}

//! Declares every layer of the stack, bottom first.
def public Frame() {
    SyncPauseLayer()
    Ui::Frame() {
        for (name in g_layers) {
            Ui::Screen(name)
        }
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
