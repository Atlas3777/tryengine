options gen2
options relaxed_assign = false
options no_unused_block_arguments = false

module GameUi shared public

//! The game's HUD: a small retained UI declared every frame from GameUpdate.
//!
//! It demonstrates the two directions of the binding: game state is read in the declaration
//! (hp, counters), and UI edits are written back through `@@` actions and `var` binders
//! (the pause checkbox, the volume slider). Sounds are named in the style and forwarded to
//! `Ui::OnSound` — the engine has no audio subsystem yet, so the default sink logs once.
//!
//! Run on the UI clock (tryengine/Ui owns it), so pausing `game_world_clock` leaves the HUD
//! alive: that is what a pause menu needs.

require math
require strings
require tryengine/Ui
require tryengine/Time
require tryengine/Input

struct private GameUiState {
    hp : float = 100.0f
    hp_max : float = 100.0f
    volume : float = 0.6f
    paused : bool = false
    clicks : int = 0
    damage_taken : int = 0
}

var private g_state = GameUiState()

let private kHudStyle = UiStyle(
    color = float4(0.08f, 0.09f, 0.12f, 0.86f),
    border_color = float4(0.24f, 0.26f, 0.32f, 1.0f),
    border_width = 1.0f,
    radius = 12.0f,
    padding = float4(16.0f, 14.0f, 16.0f, 14.0f)
)

let private kTitleStyle = UiStyle(
    text_size = 20.0f,
    text_color = float4(0.95f, 0.96f, 0.99f, 1.0f)
)

let private kHpTextStyle = UiStyle(
    text_size = 16.0f,
    text_color = float4(0.86f, 0.88f, 0.93f, 1.0f)
)

let private kHpBarStyle = UiStyle(
    color = float4(0.10f, 0.11f, 0.15f, 1.0f),
    hover_color = float4(0.88f, 0.30f, 0.32f, 1.0f),
    border_color = float4(0.30f, 0.32f, 0.40f, 1.0f),
    border_width = 1.0f,
    radius = 6.0f,
    text_size = 14.0f,
    text_color = float4(1.0f, 1.0f, 1.0f, 1.0f),
    text_align = 1,
    padding = float4(6.0f, 2.0f, 6.0f, 2.0f)
)

//! A template is a value: the danger button inherits the fire button's layout, padding, label
//! style and sounds, and overrides only the colours and the default text.
let private kFireTemplate = UiButtonTemplate(
    style = UiStyle(
        color = float4(0.18f, 0.36f, 0.60f, 1.0f),
        hover_color = float4(0.24f, 0.46f, 0.74f, 1.0f),
        press_color = float4(0.13f, 0.28f, 0.48f, 1.0f),
        border_color = float4(0.32f, 0.52f, 0.80f, 1.0f),
        border_width = 1.0f,
        radius = 8.0f,
        text_size = 18.0f,
        text_color = float4(1.0f, 1.0f, 1.0f, 1.0f),
        text_align = 1,
        anim_time = 0.10f,
        padding = float4(18.0f, 8.0f, 18.0f, 8.0f),
        sound_hover = "ui/hover",
        sound_click = "ui/click",
        sound_press = "ui/press"
    ),
    text = "Fire"
)

let private kDangerTemplate = ButtonTemplate(
    kFireTemplate,
    UiButtonTemplate(
        style = UiStyle(
            color = float4(0.48f, 0.14f, 0.16f, 1.0f),
            hover_color = float4(0.62f, 0.20f, 0.22f, 1.0f),
            press_color = float4(0.36f, 0.10f, 0.12f, 1.0f),
            border_color = float4(0.72f, 0.26f, 0.28f, 1.0f)
        ),
        text = "Take damage"
    )
)

def private OnFire() {
    g_state.clicks++
    g_state.hp = min(g_state.hp_max, g_state.hp + 4.0f)
}

def private OnDamage() {
    g_state.damage_taken++
    g_state.hp = max(0.0f, g_state.hp - 20.0f)
    // One-shot highlight: the bar flashes instead of just snapping down.
    Pulse("##hud")
}

def private StateLabel(paused : bool) : string {
    if (paused) {
        return "Paused"
    }
    return "Running"
}

//! Declares one UI frame. Harmless when the font/material are not ready yet: the UI still
//! builds its tree and the render pass draws nothing until the assets arrive.
def public Frame() {
    Ui::Frame() {
        Ui::Panel("##hud", kHudStyle, UiLayout(offset = float2(24.0f, 24.0f), spacing = 8.0f)) {
            Ui::Label("##title", "tryengine UI", kTitleStyle)
            Ui::Label("##hp", "HP {int(g_state.hp)} / {int(g_state.hp_max)}", kHpTextStyle)
            Ui::ProgressBar("##hpbar", g_state.hp / g_state.hp_max, "", kHpBarStyle)
            Ui::Row("##buttons", UiStyle(), UiLayout(spacing = 8.0f)) {
                Ui::Button(kFireTemplate, "##fire", @@OnFire)
                Ui::Button(kDangerTemplate, "##damage", @@OnDamage)
            }
            Ui::Row("##settings", UiStyle(), UiLayout(spacing = 8.0f)) {
                Ui::Checkbox("##paused", g_state.paused)
                Ui::Label("##paused_label", StateLabel(g_state.paused), kHpTextStyle)
            }
            Ui::Label("##volume_label", "Volume {int(g_state.volume * 100.0f)}%", kHpTextStyle)
            Ui::Slider("##volume", g_state.volume, 0.0f, 1.0f)
            if (g_state.paused) {
                Ui::Label("##paused_note", "Gameplay is paused, the HUD keeps running.", kHpTextStyle)
            }
        }

        Ui::Label("##counter", "Fire {g_state.clicks} | Damage {g_state.damage_taken}",
                  kHpTextStyle, UiLayout(anchor = float2(0.0f, 1.0f), offset = float2(24.0f, -40.0f)))
    }
}

//! Game-side state the HUD renders; the game may also own it and pass values into a `Label`.
def public Hp() : float {
    return g_state.hp
}

def public SetHp(value : float) {
    g_state.hp = clamp(value, 0.0f, g_state.hp_max)
}

def public IsPaused() : bool {
    return g_state.paused
}
