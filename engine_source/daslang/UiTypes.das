options gen2
options relaxed_assign = false

module UiTypes shared public

//! Public data types of the game UI: the draw command the C++ pass consumes, widget styles,
//! templates, node kinds and a few small helpers (style merge, easing, colour maths).
//!
//! Styles use sentinels instead of nullable fields: a negative number or an empty string means
//! "not set here", so `MergeStyle` can tell a call-site override from a default. That keeps
//! `UiStyle(color = ..., text_size = ...)` literals ergonomic and avoids pointer-like optional
//! values all over the UI module.

require math
require daslib/math_boost
require tryResources

//! No-argument click action. Stored per node: function pointers capture nothing, so a node can
//! outlive the frame that declared it without leaking a closure.
typedef public UiAction = function<() : void>
//! List-friendly action: gets the `data` value passed with the widget (an index, an id).
typedef public UiActionData = function<(data : int) : void>
//! Sound event sink. The UI never plays audio itself; it names what should play and to whom.
typedef public UiSoundHandler = function<(path : string; node_id : uint64) : void>
//! One screen: the unique part of the interface, built from widgets. `Ui::RegisterScreen` stores
//! this pointer, so a screen holds no state of its own — it reads module globals and is drawn by
//! name. `Ui::Screen` draws it inline, `Ui::FrameScreen` makes it the whole frame.
typedef public UiScreenBuild = function<() : void>

//! Mirror of tryengine::graphics::UiPass::UiDrawCmd. The pass static_asserts this layout, and
//! glyph geometry is built in C++ from text commands, so no strings live here.
[cpp_layout]
struct public UiDrawCmd {
    kind : uint        // 0 filled rounded rect, 1 text run, 2 textured quad, 3 border ring
    text_id : uint     // frame string table index (kind 1)
    material : TryMat const?  // null = the built-in UI material
    rect : float4      // x, y, width, height in render-target pixels
    color : float4     // premultiplied RGBA
    extra : float4     // kind 0/3: radius, border width; kind 1: pixel size
    scissor : float4   // clip rectangle in pixels
}

//! Colors that were never set. A valid colour always has alpha >= 0.
let public UNSET_COLOR : float4 = float4(-1.0f, -1.0f, -1.0f, -1.0f)

//! One widget style. Every field is optional and merged over a default (see MergeStyle); a
//! negative number or an empty string is "unset", zero is a real zero.
struct public UiStyle {
    color : float4 = UNSET_COLOR           // fill / background
    color_top : float4 = UNSET_COLOR       // gradient top (reserved; the shader is flat today)
    hover_color : float4 = UNSET_COLOR
    press_color : float4 = UNSET_COLOR
    disabled_color : float4 = UNSET_COLOR
    text_color : float4 = UNSET_COLOR
    border_color : float4 = UNSET_COLOR

    text_size : float = -1.0f              // pixel size of the label
    border_width : float = -1.0f
    radius : float = -1.0f
    anim_time : float = -1.0f              // seconds for hover/press transitions; 0 = instant
    text_align : int = -1                  // 0 left, 1 center, 2 right

    //! left, top, right, bottom; any component < 0 is unset.
    padding : float4 = float4(-1.0f, -1.0f, -1.0f, -1.0f)

    material : string = ""                 // .mat path for kind-2 widgets
    sound_hover : string = ""
    sound_click : string = ""
    sound_press : string = ""
}

//! Layout request of one node. Zero size means "measure the content"; `grow` fills the space a
//! container leaves; `anchor` other than zero takes the node out of the flow and positions it
//! inside the parent's content box (0..1 along each axis).
struct public UiLayout {
    size : float2 = float2(0.0f, 0.0f)
    grow : float2 = float2(0.0f, 0.0f)
    padding : float4 = float4(0.0f, 0.0f, 0.0f, 0.0f)
    spacing : float = 0.0f
    align : int = 0
    anchor : float2 = float2(0.0f, 0.0f)
    offset : float2 = float2(0.0f, 0.0f)
}

//! A named, reusable button look: style, label style and a default label. Derived templates are
//! plain values: `ButtonTemplate(DANGER, UiButtonTemplate(style = UiStyle(...)))`.
struct public UiButtonTemplate {
    style : UiStyle = default<UiStyle>
    label_style : UiStyle = default<UiStyle>
    text : string = ""
    size : float2 = float2(0.0f, 0.0f)
    layout : UiLayout = default<UiLayout>
}

//! Per-kind default looks: the theme. A widget merges its call-site style over the theme entry
//! for its kind (see `MergeStyle`), so `Ui::Panel("##hud")` already has colours, padding, radius
//! and text metrics. A game installs its theme once with `Ui::SetTheme` from `[init]`, which
//! keeps styles in their own file instead of in the markup. Fields are partial: `SetTheme`
//! merges the theme over the engine defaults, so a theme may override one kind and leave the rest.
struct public UiTheme {
    panel : UiStyle = default<UiStyle>
    text : UiStyle = default<UiStyle>
    button : UiStyle = default<UiStyle>
    bar : UiStyle = default<UiStyle>
    slider : UiStyle = default<UiStyle>
    checkbox : UiStyle = default<UiStyle>
    image : UiStyle = default<UiStyle>
}

// Node kinds. They drive both layout (flow vs group) and drawing in tryengine/Ui.
let public UIK_PANEL : uint = 0u
let public UIK_COLUMN : uint = 1u
let public UIK_ROW : uint = 2u
let public UIK_GROUP : uint = 3u
let public UIK_LABEL : uint = 4u
let public UIK_BUTTON : uint = 5u
let public UIK_BAR : uint = 6u
let public UIK_IMAGE : uint = 7u
let public UIK_SLIDER : uint = 8u
let public UIK_CHECKBOX : uint = 9u
let public UIK_SPACER : uint = 10u

// Kind of a draw command (mirror of the C++ constants in UiPass.cpp).
let public UIDC_QUAD : uint = 0u
let public UIDC_TEXT : uint = 1u
let public UIDC_TEXTURED : uint = 2u
let public UIDC_BORDER : uint = 3u

//! True when the style sets `field` (used by MergeStyle for colours).
def public IsSetColor(c : float4) : bool {
    return c.w >= 0.0f
}

def public IsSetFloat(v : float) : bool {
    return v >= 0.0f
}

def public IsSetPadding(p : float4) : bool {
    return p.x >= 0.0f || p.y >= 0.0f || p.z >= 0.0f || p.w >= 0.0f
}

//! `over` wins wherever it sets a field; everything else comes from `base`.
def public MergeStyle(base : UiStyle; over : UiStyle) : UiStyle {
    var result = base
    if (IsSetColor(over.color)) { result.color = over.color }
    if (IsSetColor(over.color_top)) { result.color_top = over.color_top }
    if (IsSetColor(over.hover_color)) { result.hover_color = over.hover_color }
    if (IsSetColor(over.press_color)) { result.press_color = over.press_color }
    if (IsSetColor(over.disabled_color)) { result.disabled_color = over.disabled_color }
    if (IsSetColor(over.text_color)) { result.text_color = over.text_color }
    if (IsSetColor(over.border_color)) { result.border_color = over.border_color }
    if (IsSetFloat(over.text_size)) { result.text_size = over.text_size }
    if (IsSetFloat(over.border_width)) { result.border_width = over.border_width }
    if (IsSetFloat(over.radius)) { result.radius = over.radius }
    if (IsSetFloat(over.anim_time)) { result.anim_time = over.anim_time }
    if (over.text_align >= 0) { result.text_align = over.text_align }
    if (IsSetPadding(over.padding)) { result.padding = over.padding }
    if (!empty(over.material)) { result.material = over.material }
    if (!empty(over.sound_hover)) { result.sound_hover = over.sound_hover }
    if (!empty(over.sound_click)) { result.sound_click = over.sound_click }
    if (!empty(over.sound_press)) { result.sound_press = over.sound_press }
    return result
}

//! Merges the two label/style halves of a template over another template.
def public ButtonTemplate(base : UiButtonTemplate; over : UiButtonTemplate) : UiButtonTemplate {
    var result : UiButtonTemplate = default<UiButtonTemplate>
    result.style = MergeStyle(base.style, over.style)
    result.label_style = MergeStyle(base.label_style, over.label_style)
    result.text = base.text
    if (!empty(over.text)) {
        result.text = over.text
    }
    result.size = base.size
    if (over.size.x > 0.0f || over.size.y > 0.0f) {
        result.size = over.size
    }
    result.layout = base.layout
    if (over.layout.size.x > 0.0f || over.layout.size.y > 0.0f || over.layout.anchor.x != 0.0f ||
        over.layout.anchor.y != 0.0f) {
        result.layout = over.layout
    }
    return result
}

//! Premultiplied colour: the shader blends with (ONE, ONE_MINUS_SRC_ALPHA) and expects it.
def public Premul(c : float4) : float4 {
    return float4(c.x * c.w, c.y * c.w, c.z * c.w, c.w)
}

def public WithAlpha(c : float4; alpha : float) : float4 {
    return float4(c.x, c.y, c.z, alpha)
}

def public ColorLerp(a : float4; b : float4; t : float) : float4 {
    let k = clamp(t, 0.0f, 1.0f)
    return float4(a.x + (b.x - a.x) * k, a.y + (b.y - a.y) * k, a.z + (b.z - a.z) * k, a.w + (b.w - a.w) * k)
}

// --- easing -------------------------------------------------------------------------------

def public EaseOutCubic(t : float) : float {
    let x = clamp(t, 0.0f, 1.0f)
    let inv = 1.0f - x
    return 1.0f - inv * inv * inv
}

def public EaseInOutQuad(t : float) : float {
    let x = clamp(t, 0.0f, 1.0f)
    if (x < 0.5f) {
        return 2.0f * x * x
    }
    let inv = -2.0f * x + 2.0f
    return 1.0f - inv * inv * 0.5f
}

//! Frame-rate independent approach of `current` to `target`: `step` is the fraction of the
//! remaining distance covered by one second of animation time.
def public Approach(current : float; target : float; step : float) : float {
    if (step <= 0.0f) {
        return current
    }
    let delta = target - current
    if (abs(delta) <= step) {
        return target
    }
    return current + (delta > 0.0f ? step : -step)
}

def public PointInRect(rect : float4; point : float2) : bool {
    return point.x >= rect.x && point.y >= rect.y && point.x < rect.x + rect.z && point.y < rect.y + rect.w
}

def public RectIntersect(a : float4; b : float4) : float4 {
    let x0 = max(a.x, b.x)
    let y0 = max(a.y, b.y)
    let x1 = min(a.x + a.z, b.x + b.z)
    let y1 = min(a.y + a.w, b.y + b.w)
    return float4(x0, y0, max(0.0f, x1 - x0), max(0.0f, y1 - y0))
}
