options gen2
options relaxed_assign = false

module UiTheme shared public

//! The game's look, in one file.
//!
//! Two layers live here:
//! - `kTheme` — the per-kind defaults (panel, text, button, bar, slider, checkbox). It is
//!   installed once from `[init]` with `Ui::SetTheme`, so a screen declares structure only:
//!   `Ui::Panel("##hud")` already has colours, padding, radius, text metrics and sounds.
//! - named styles and button templates that screens reference for a deliberate variant
//!   (`UiTheme::kDangerButton`, `UiTheme::kMuted`).
//!
//! Nothing in here knows about the layout of a screen: sizes that describe a widget's look are
//! style, sizes that describe where a widget sits are `UiLayout` and stay in the markup.

require tryengine/Ui

// --- palette ------------------------------------------------------------------------------
//
// One accent plus neutrals: every visible surface is one of these constants, so a reskin is a
// single edit. Colours are straight RGBA; the UI premultiplies them when it emits a command.

let public kInk : float4 = float4(0.06f, 0.07f, 0.09f, 0.94f)       // panel background
let public kInkSoft : float4 = float4(0.11f, 0.12f, 0.16f, 1.0f)    // wells, tracks
let public kInkRaised : float4 = float4(0.14f, 0.16f, 0.21f, 1.0f)  // buttons, boxes
let public kLine : float4 = float4(0.26f, 0.29f, 0.37f, 1.0f)       // borders
let public kPaper : float4 = float4(0.94f, 0.95f, 0.98f, 1.0f)      // primary text
let public kPaperDim : float4 = float4(0.70f, 0.74f, 0.82f, 1.0f)   // secondary text
let public kAccent : float4 = float4(0.26f, 0.56f, 0.96f, 1.0f)
let public kAccentHot : float4 = float4(0.36f, 0.66f, 1.0f, 1.0f)
let public kDanger : float4 = float4(0.86f, 0.29f, 0.33f, 1.0f)
let public kDangerHot : float4 = float4(0.96f, 0.39f, 0.43f, 1.0f)

// --- sound names --------------------------------------------------------------------------
//
// The UI never plays audio: it emits a path on hover/press/click and the game decides what that
// path means. Keeping the names here means the audio side knows one list, not one per screen.

let public kSfxHover : string = "ui/hover"
let public kSfxPress : string = "ui/press"
let public kSfxClick : string = "ui/click"

// --- per-kind theme -----------------------------------------------------------------------

let public kTheme = UiTheme(
    panel = UiStyle(
        color = kInk,
        border_color = kLine,
        border_width = 1.0f,
        radius = 12.0f,
        text_size = 18.0f,
        text_color = kPaper,
        anim_time = 0.12f,
        padding = float4(16.0f, 14.0f, 16.0f, 14.0f)
    ),
    text = UiStyle(
        text_size = 18.0f,
        text_color = kPaper,
        text_align = 0,
        anim_time = 0.0f
    ),
    button = UiStyle(
        color = kInkRaised,
        hover_color = float4(0.22f, 0.29f, 0.42f, 1.0f),
        press_color = float4(0.10f, 0.12f, 0.16f, 1.0f),
        border_color = kLine,
        border_width = 1.0f,
        radius = 8.0f,
        text_size = 18.0f,
        text_color = kPaper,
        text_align = 1,
        anim_time = 0.10f,
        padding = float4(18.0f, 8.0f, 18.0f, 8.0f),
        sound_hover = kSfxHover,
        sound_press = kSfxPress,
        sound_click = kSfxClick
    ),
    bar = UiStyle(
        color = kInkSoft,
        hover_color = kAccent,
        border_color = kLine,
        border_width = 1.0f,
        radius = 6.0f,
        text_size = 14.0f,
        text_color = kPaper,
        text_align = 1,
        anim_time = 0.10f,
        padding = float4(6.0f, 2.0f, 6.0f, 2.0f)
    ),
    slider = UiStyle(
        color = kInkRaised,
        hover_color = kAccentHot,
        border_color = kLine,
        border_width = 1.0f,
        radius = 6.0f,
        anim_time = 0.08f
    ),
    checkbox = UiStyle(
        color = kInkRaised,
        hover_color = kAccent,
        border_color = kLine,
        border_width = 1.0f,
        radius = 4.0f,
        anim_time = 0.08f
    )
)

// --- named styles -------------------------------------------------------------------------
//
// A screen passes one of these as the widget's `style`; unset fields fall back to the theme, so
// each entry states only what makes it different.

let public kTitle : UiStyle = UiStyle(text_size = 24.0f, text_color = kPaper)
let public kMuted : UiStyle = UiStyle(text_size = 15.0f, text_color = kPaperDim)
let public kValue : UiStyle = UiStyle(text_size = 18.0f, text_color = kPaper)
let public kHudPanel : UiStyle = UiStyle(radius = 14.0f, padding = float4(18.0f, 16.0f, 18.0f, 16.0f))
let public kHpBar : UiStyle = UiStyle(hover_color = kDanger)
let public kPausePanel : UiStyle = UiStyle(
    color = float4(0.05f, 0.055f, 0.07f, 0.97f),
    radius = 16.0f,
    padding = float4(24.0f, 20.0f, 24.0f, 20.0f)
)
// The modal backdrop covers the viewport: no radius, no border and no padding, so the centred
// panel inside it is centred on screen and not inside an inset box.
let public kPauseCurtain : UiStyle = UiStyle(
    color = float4(0.0f, 0.0f, 0.0f, 0.55f),
    radius = 0.0f,
    border_width = 0.0f,
    padding = float4(0.0f, 0.0f, 0.0f, 0.0f)
)

// --- button templates ---------------------------------------------------------------------
//
// A template is a value: the danger button inherits the primary look and overrides only the
// colours, so paddings, radius, sounds and text metrics are written once.

let public kPrimaryButton : UiButtonTemplate = UiButtonTemplate(
    style = UiStyle(
        color = float4(0.16f, 0.34f, 0.58f, 1.0f),
        hover_color = float4(0.22f, 0.46f, 0.76f, 1.0f),
        press_color = float4(0.12f, 0.26f, 0.46f, 1.0f),
        border_color = float4(0.32f, 0.54f, 0.84f, 1.0f)
    ),
    text = "Fire"
)

let public kDangerButton : UiButtonTemplate = ButtonTemplate(
    kPrimaryButton,
    UiButtonTemplate(
        style = UiStyle(
            color = float4(0.48f, 0.14f, 0.16f, 1.0f),
            hover_color = kDangerHot,
            press_color = float4(0.36f, 0.10f, 0.12f, 1.0f),
            border_color = float4(0.74f, 0.27f, 0.30f, 1.0f)
        ),
        text = "Take damage"
    )
)

let public kQuietButton : UiButtonTemplate = ButtonTemplate(
    kPrimaryButton,
    UiButtonTemplate(
        style = UiStyle(
            color = float4(0.12f, 0.13f, 0.17f, 0.0f),
            hover_color = float4(0.20f, 0.22f, 0.28f, 1.0f),
            press_color = float4(0.10f, 0.11f, 0.15f, 1.0f),
            border_color = kLine
        )
    )
)

// The theme is installed when the script context starts, so every screen in the game — and the
// editor's screen preview — sees it without wiring a call into `GameStart`.
[init]
def private InstallTheme() {
    Ui::SetTheme(kTheme)
}
