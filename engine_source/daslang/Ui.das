options gen2
options relaxed_assign = false
options no_unused_block_arguments = false

module Ui shared public

//! Retained game UI described in daslang.
//!
//! A frame is declared every frame: widgets look their node up by a stable 64-bit id and patch
//! it; nodes, their hover/press animation, and the input state survive between frames, so a
//! widget that is declared again does not lose its transition. Layout and drawing happen in
//! `End`: the tree is measured bottom-up, arranged top-down, and turned into a flat command
//! list that tryengine/graphics/UiPass expands into vertices (glyph quads are built in C++).
//!
//! Widgets never compute geometry per vertex: a per-vertex push costs ~45 ns in daslang, so a
//! 300-widget screen would spend ~1.7 ms per frame doing it. Commands cost ~60 ns each.
//!
//! Two-way flow:
//!   * code -> UI: values are read in the declaration (`Label("##hp", "HP {hp}")`);
//!   * UI -> code: `@@` actions (`Button`), `var x : T&` binders (`Slider`, `Checkbox`);
//!   * changes are observable with `Watch*`, sounds are forwarded to `OnSound`.
//!
//! Style fields use sentinels (negative / empty = not set) and merge over per-kind defaults;
//! templates are just values, see UiButtonTemplate.

require strings
require daslib/strings_boost
require math
require daslib/math_boost
require tryengine/Time
require tryengine/Input
require tryengine/Profiling
require tryengine/UiTypes public
require tryResources
require tryUi

// --- node flags ---------------------------------------------------------------------------

let private kFlagInteractive : uint = 1u
let private kFlagGroup : uint = 2u
let private kFlagDismissed : uint = 4u

// --- node / frame data --------------------------------------------------------------------

//! One retained node. Public only because a table of this type is instantiated from other
//! modules (a private struct used as a table value type fails "constructor can't be inferred");
//! it is an implementation detail of tryengine/Ui and not part of the widget API.
struct public UiNode {
    id : uint64 = 0ul
    parent : uint64 = 0ul
    kind : uint = 0u
    rect : float4 = float4(0.0f)
    measured : float2 = float2(0.0f, 0.0f)
    clip : float4 = float4(0.0f)
    text : string = ""
    style : UiStyle = default<UiStyle>
    layout : UiLayout = default<UiLayout>
    first_child : uint64 = 0ul
    last_child : uint64 = 0ul
    next_sibling : uint64 = 0ul
    cursor : float2 = float2(0.0f, 0.0f)
    hover_t : float = 0.0f
    press_t : float = 0.0f
    pulse_t : float = 0.0f
    last_frame : int = -1
    flags : uint = 0u
    action : UiAction = default<UiAction>
    action_data : UiActionData = default<UiActionData>
    data : int = 0
    value : float = 0.0f
    text_hash : uint64 = 0ul
    text_metric : float2 = float2(0.0f, 0.0f)
}

struct private PendingCall {
    id : uint64 = 0ul
    action : UiAction = default<UiAction>
    action_data : UiActionData = default<UiActionData>
    data : int = 0
}

struct private SoundEvent {
    path : string
    node_id : uint64
}

// The UI owns its clock and hangs it off the engine root: pausing game_world_clock must not
// freeze menus or hover animations.
struct private UiClock : ClockTag {}

var private g_clock = UiClock()

// Tree.
var private g_nodes : table<uint64; UiNode>
var private g_order : array<uint64>
var private g_frame : int = 0

// Frame construction state.
var private g_open : bool = false
var private g_parent : uint64 = 0ul
var private g_salt : uint64 = 0ul
var private g_dt : float = 0.0f
var private g_commands : array<UiDrawCmd>
var private g_clip_stack : array<float4>
var private g_duplicate_warned : bool = false

// Input. The host tells the UI where its viewport is on screen and whether the pointer may
// interact with it; an injected state replaces the engine input for tests and gamepads.
struct private UiInputState {
    pos : float2 = float2(0.0f, 0.0f)
    down : bool = false
    pressed : bool = false
    released : bool = false
}

var private g_input_injected : bool = false
var private g_injected : UiInputState = UiInputState()
//! Where the viewport starts in window coordinates. It is used only to map the pointer into
//! UI space: all geometry is in render-target pixels, 0..viewport size, because the pass draws
//! into that target (the editor's SceneGameView is a panel, not the whole window).
var private g_viewport_origin : float2 = float2(0.0f, 0.0f)
var private g_viewport_size : float2 = float2(0.0f, 0.0f)
var private g_viewport_hovered : bool = true
var private g_mouse : float2 = float2(0.0f, 0.0f)
var private g_mouse_down : bool = false
var private g_mouse_pressed : bool = false
var private g_mouse_released : bool = false
var private g_hot_id : uint64 = 0ul
var private g_active_id : uint64 = 0ul
var private g_clicked_id : uint64 = 0ul
var private g_consumed_mouse : bool = false

// Deferred side effects: callbacks and sounds run after the tree is built and drawn.
var private g_pending : array<PendingCall>
var private g_sounds : array<SoundEvent>
var private g_sound_handler : UiSoundHandler = @@NoopSound

// Text measurement cache, dropped whenever the font atlas changes.
var private g_measure : table<uint64; float2>
var private g_measure_version : uint = 0xFFFFFFFFu

// Value watchers and material resolution bookkeeping.
var private g_watch : table<uint64; double>
var private g_material_guid : table<string; uint64>
var private g_material_requested : table<uint64; int>

// --- styles -------------------------------------------------------------------------------

let private kNeutralStyle : UiStyle = UiStyle()

let private kPanelStyle : UiStyle = UiStyle(
    color = float4(0.09f, 0.10f, 0.13f, 0.88f),
    border_color = float4(0.25f, 0.27f, 0.33f, 1.0f),
    border_width = 1.0f,
    radius = 10.0f,
    text_size = 18.0f,
    text_color = float4(0.92f, 0.93f, 0.96f, 1.0f),
    anim_time = 0.12f,
    padding = float4(14.0f, 12.0f, 14.0f, 12.0f)
)

let private kLabelStyle : UiStyle = UiStyle(
    text_size = 18.0f,
    text_color = float4(0.92f, 0.93f, 0.96f, 1.0f),
    text_align = 0,
    anim_time = 0.0f
)

let private kButtonStyle : UiStyle = UiStyle(
    color = float4(0.20f, 0.22f, 0.28f, 1.0f),
    hover_color = float4(0.27f, 0.30f, 0.38f, 1.0f),
    press_color = float4(0.14f, 0.16f, 0.21f, 1.0f),
    text_color = float4(0.94f, 0.95f, 0.98f, 1.0f),
    border_color = float4(0.33f, 0.36f, 0.44f, 1.0f),
    border_width = 1.0f,
    radius = 7.0f,
    text_size = 18.0f,
    text_align = 1,
    anim_time = 0.10f,
    padding = float4(16.0f, 8.0f, 16.0f, 8.0f)
)

let private kBarStyle : UiStyle = UiStyle(
    color = float4(0.10f, 0.11f, 0.14f, 0.95f),
    hover_color = float4(0.85f, 0.28f, 0.28f, 1.0f),
    border_color = float4(0.30f, 0.32f, 0.38f, 1.0f),
    border_width = 1.0f,
    radius = 6.0f,
    text_size = 14.0f,
    text_color = float4(1.0f, 1.0f, 1.0f, 1.0f),
    text_align = 1,
    anim_time = 0.10f,
    padding = float4(6.0f, 2.0f, 6.0f, 2.0f)
)

let private kSliderStyle : UiStyle = UiStyle(
    color = float4(0.12f, 0.13f, 0.17f, 1.0f),
    hover_color = float4(0.32f, 0.56f, 0.92f, 1.0f),
    border_color = float4(0.30f, 0.32f, 0.38f, 1.0f),
    border_width = 1.0f,
    radius = 6.0f,
    anim_time = 0.08f,
    padding = float4(0.0f, 0.0f, 0.0f, 0.0f)
)

let private kCheckboxStyle : UiStyle = UiStyle(
    color = float4(0.12f, 0.13f, 0.17f, 1.0f),
    hover_color = float4(0.32f, 0.56f, 0.92f, 1.0f),
    border_color = float4(0.30f, 0.32f, 0.38f, 1.0f),
    border_width = 1.0f,
    radius = 4.0f,
    anim_time = 0.08f,
    padding = float4(0.0f, 0.0f, 0.0f, 0.0f)
)

def private DefaultsFor(kind : uint) : UiStyle {
    if (kind == UIK_PANEL) {
        return kPanelStyle
    }
    if (kind == UIK_LABEL) {
        return kLabelStyle
    }
    if (kind == UIK_BUTTON) {
        return kButtonStyle
    }
    if (kind == UIK_BAR) {
        return kBarStyle
    }
    if (kind == UIK_SLIDER) {
        return kSliderStyle
    }
    if (kind == UIK_CHECKBOX) {
        return kCheckboxStyle
    }
    return kNeutralStyle
}

def private ResolveStyle(kind : uint; style : UiStyle) : UiStyle {
    var resolved = MergeStyle(DefaultsFor(kind), style)
    // Fill the remaining sentinels so drawing can read plain values.
    if (!IsSetColor(resolved.color)) { resolved.color = float4(0.0f, 0.0f, 0.0f, 0.0f) }
    if (!IsSetColor(resolved.hover_color)) { resolved.hover_color = resolved.color }
    if (!IsSetColor(resolved.press_color)) { resolved.press_color = resolved.hover_color }
    if (!IsSetColor(resolved.disabled_color)) { resolved.disabled_color = resolved.color }
    if (!IsSetColor(resolved.text_color)) { resolved.text_color = float4(1.0f, 1.0f, 1.0f, 1.0f) }
    if (!IsSetColor(resolved.border_color)) { resolved.border_color = float4(0.0f, 0.0f, 0.0f, 0.0f) }
    if (!IsSetFloat(resolved.text_size)) { resolved.text_size = 18.0f }
    if (!IsSetFloat(resolved.border_width)) { resolved.border_width = 0.0f }
    if (!IsSetFloat(resolved.radius)) { resolved.radius = 0.0f }
    if (!IsSetFloat(resolved.anim_time)) { resolved.anim_time = 0.10f }
    if (resolved.text_align < 0) { resolved.text_align = 0 }
    if (!IsSetPadding(resolved.padding)) { resolved.padding = float4(0.0f, 0.0f, 0.0f, 0.0f) }
    else {
        resolved.padding.x = max(0.0f, resolved.padding.x)
        resolved.padding.y = max(0.0f, resolved.padding.y)
        resolved.padding.z = max(0.0f, resolved.padding.z)
        resolved.padding.w = max(0.0f, resolved.padding.w)
    }
    return resolved
}

// --- ids ----------------------------------------------------------------------------------

def private HashString(s : string) : uint64 {
    var h : uint64 = 0xcbf29ce484222325ul
    for (ch in s) {
        h = (h ^ uint64(uint(ch))) * 0x100000001b3ul
    }
    return h
}

def private MixId(a : uint64; b : uint64) : uint64 {
    var h = a ^ (b + 0x9e3779b97f4a7c15ul + (a << 6ul) + (a >> 2ul))
    h = (h ^ (h >> 30ul)) * 0xbf58476d1ce4e5b9ul
    h = (h ^ (h >> 27ul)) * 0x94d049bb133111ebul
    return h ^ (h >> 31ul)
}

def private NodeId(local_id : string) : uint64 {
    let local = HashString(local_id)
    return MixId(g_parent, MixId(g_salt, local))
}

// --- frame lifecycle ----------------------------------------------------------------------

def private EnsureClock() {
    if (g_clock.slot_ >= 0) {
        return
    }
    // The engine root clock is registered by TimeInit (EngineStart). A headless probe runs
    // without it and uses a fixed frame delta instead of asserting.
    if (root_clock.slot_ < 0) {
        return
    }
    TimeAttach(g_clock)
}

def private ClockDelta() : float {
    if (g_clock.slot_ < 0) {
        return 1.0f / 60.0f
    }
    return TimeDelta(g_clock)
}

var private g_sound_reported : table<string; int>

def private NoopSound(path : string; node_id : uint64) {
    // The engine has no audio subsystem yet: report the event once per path so a missing hook
    // is visible without flooding the log.
    if (!key_exists(g_sound_reported, path)) {
        g_sound_reported[path] = 1
        print("[UI SOUND] {path}\n")
    }
}

//! Remembers where the UI lives on screen. The standalone game calls it with the window size;
//! the editor passes the SceneGameView panel rectangle from ImGui.
def public SetInputViewport(origin : float2; size : float2; hovered : bool = true) {
    g_viewport_origin = origin
    g_viewport_size = size
    g_viewport_hovered = hovered
}

//! Replaces engine input for a frame (headless tests, gamepad, touch).
def public InjectInput(pos : float2; down : bool; pressed : bool; released : bool) {
    g_input_injected = true
    g_injected = UiInputState(pos = pos, down = down, pressed = pressed, released = released)
}

def public ClearInjectedInput() {
    g_input_injected = false
}

def public ViewportSize() : float2 {
    let current = g_viewport_size
    if (current.x <= 0.0f || current.y <= 0.0f) {
        return UiWindowSize()
    }
    return current
}

def public IsReady() : bool {
    return UiIsReady()
}

def public ConsumesMouse() : bool {
    return g_consumed_mouse
}

def public HotId() : uint64 {
    return g_hot_id
}

def public OnSound(handler : UiSoundHandler) {
    g_sound_handler = handler
}

// --- value watchers -----------------------------------------------------------------------

def public WatchFloat(key : string; value : float) : bool {
    let k = MixId(0ul, HashString(key))
    let v = double(value)
    if (!key_exists(g_watch, k)) {
        g_watch[k] = v
        return true
    }
    if (g_watch[k] == v) {
        return false
    }
    g_watch[k] = v
    return true
}

def public WatchInt(key : string; value : int) : bool {
    return WatchFloat(key, float(value))
}

def public WatchBool(key : string; value : bool) : bool {
    return WatchFloat(key, value ? 1.0f : 0.0f)
}

def public WatchString(key : string; value : string) : bool {
    let k = MixId(0x9e3779b97f4a7c15ul, HashString(key))
    let v = double(HashString(value) & 0xFFFFFFFFul)
    if (!key_exists(g_watch, k)) {
        g_watch[k] = v
        return true
    }
    if (g_watch[k] == v) {
        return false
    }
    g_watch[k] = v
    return true
}

//! One-shot highlight of a node by its root-level name (see PulseNode for scoped ids).
def public Pulse(name : string) {
    let nid = MixId(0ul, MixId(0ul, HashString(name)))
    if (key_exists(g_nodes, nid)) {
        var node = g_nodes[nid]
        node.pulse_t = 1.0f
        g_nodes[nid] = node
    }
}

// --- text ---------------------------------------------------------------------------------

def private MeasureText(text : string; size : float) : float2 {
    // Dynamic text (resource counters, timers) would otherwise grow the cache forever; a full
    // flush is cheaper than an LRU and happens rarely.
    if (length(g_measure) > 8192) {
        g_measure.clear()
    }
    let key = MixId(HashString(text), uint64(max(0, int(size * 64.0f))))
    if (key_exists(g_measure, key)) {
        return g_measure[key]
    }
    let metric = UiTextMeasure(text, size)
    g_measure[key] = metric
    return metric
}

def private WrapText(text : string; size : float; max_width : float) : array<string> {
    var lines : array<string>
    if (empty(text)) {
        lines |> push("")
        return <- lines
    }
    if (max_width <= 0.0f) {
        lines |> push(text)
        return <- lines
    }

    let paragraphs <- split(text, "\n")
    for (paragraph in paragraphs) {
        if (empty(paragraph)) {
            lines |> push("")
            continue
        }
        let words <- split(paragraph, " ")
        var line = ""
        var line_width = 0.0f
        for (word in words) {
            if (empty(word)) {
                continue
            }
            let word_width = MeasureText(word, size).x
            let space_width = empty(line) ? 0.0f : MeasureText(" ", size).x
            if (!empty(line) && line_width + space_width + word_width > max_width) {
                lines |> push(line)
                line = word
                line_width = word_width
            } else {
                if (empty(line)) {
                    line = word
                } else {
                    line = line + " " + word
                }
                line_width += space_width + word_width
            }
        }
        lines |> push(line)
    }
    return <- lines
}

def private LineHeight(size : float) : float {
    let h = UiTextLineHeight(size)
    return h > 0.0f ? h : size * 1.25f
}

// --- node helpers -------------------------------------------------------------------------

def private NewNode(id : uint64; kind : uint; parent : uint64) : UiNode {
    var node : UiNode = default<UiNode>
    node.id = id
    node.parent = parent
    node.kind = kind
    node.last_frame = g_frame
    node.clip = float4(0.0f, 0.0f, ViewportSize().x, ViewportSize().y)
    return node
}

//! Finds or creates a node for `local_id` under the current container and attaches it to the
//! parent's child list. Returns 0 when no frame is open.
def private Acquire(local_id : string; kind : uint) : uint64 {
    if (!g_open) {
        return 0ul
    }

    let nid = NodeId(local_id)
    var created = false
    if (!key_exists(g_nodes, nid)) {
        g_nodes[nid] = NewNode(nid, kind, g_parent)
        created = true
    } elif (g_nodes[nid].last_frame == g_frame) {
        if (!g_duplicate_warned) {
            g_duplicate_warned = true
            print("[UI] duplicate widget id in one frame; ids must be unique among siblings\n")
        }
    }

    var node = g_nodes[nid]
    node.kind = kind
    node.parent = g_parent
    node.last_frame = g_frame
    node.first_child = 0ul
    node.last_child = 0ul
    node.next_sibling = 0ul
    if (created) {
        node.hover_t = 0.0f
        node.press_t = 0.0f
    }
    g_nodes[nid] = node

    if (g_parent != 0ul) {
        var parent = g_nodes[g_parent]
        if (parent.first_child == 0ul) {
            parent.first_child = nid
        } else {
            let tail = parent.last_child
            if (tail != 0ul) {
                var tail_node = g_nodes[tail]
                tail_node.next_sibling = nid
                g_nodes[tail] = tail_node
            }
        }
        parent.last_child = nid
        g_nodes[g_parent] = parent
    }

    if (length(g_order) == 0 || g_order[length(g_order) - 1] != nid) {
        g_order |> push(nid)
    }
    return nid
}

def private ApplyStyle(nid : uint64; kind : uint; style : UiStyle; layout : UiLayout) {
    if (nid == 0ul) {
        return
    }
    var node = g_nodes[nid]
    node.style = ResolveStyle(kind, style)
    node.layout = layout
    g_nodes[nid] = node
}

def private SetText(nid : uint64; text : string) {
    if (nid == 0ul) {
        return
    }
    var node = g_nodes[nid]
    node.text = text
    g_nodes[nid] = node
}

def private MakeInteractive(nid : uint64) {
    if (nid == 0ul) {
        return
    }
    var node = g_nodes[nid]
    node.flags |= kFlagInteractive
    g_nodes[nid] = node
}

def private SetAction(nid : uint64; action : UiAction) {
    if (nid == 0ul) {
        return
    }
    var node = g_nodes[nid]
    node.flags |= kFlagInteractive
    node.action = action
    g_nodes[nid] = node
}

def private SetActionData(nid : uint64; action : UiActionData; data : int) {
    if (nid == 0ul) {
        return
    }
    var node = g_nodes[nid]
    node.flags |= kFlagInteractive
    node.action_data = action
    node.data = data
    g_nodes[nid] = node
}

def private PushContainer(nid : uint64) : uint64 {
    let saved = g_parent
    g_parent = nid
    return saved
}

def private PopContainer(saved : uint64) {
    g_parent = saved
}

// --- begin ---------------------------------------------------------------------------------

def private ReadInput() {
    var pos = float2(0.0f, 0.0f)
    if (g_input_injected) {
        pos = g_injected.pos
        g_mouse_down = g_injected.down
        g_mouse_pressed = g_injected.pressed
        g_mouse_released = g_injected.released
    } elif (HasInputSource()) {
        pos = mouse_pos()
        g_mouse_down = is_mouse_down(Mouse.Left)
        g_mouse_pressed = mouse_pressed(Mouse.Left)
        g_mouse_released = mouse_released(Mouse.Left)
    } else {
        g_mouse_down = false
        g_mouse_pressed = false
        g_mouse_released = false
    }
    g_mouse = pos - g_viewport_origin
    if (!g_viewport_hovered) {
        g_mouse_pressed = false
        g_mouse_released = false
        g_mouse_down = false
    }
}

def private HitTest() : uint64 {
    var hot : uint64 = 0ul
    if (!g_viewport_hovered) {
        return hot
    }
    for (id in g_order) {
        let node = g_nodes[id]
        if ((node.flags & kFlagInteractive) == 0u) {
            continue
        }
        if (!PointInRect(node.rect, g_mouse)) {
            continue
        }
        if (node.clip.z <= 0.0f || !PointInRect(node.clip, g_mouse)) {
            continue
        }
        hot = id
    }
    return hot
}

def private QueueAction(id : uint64) {
    if (id == 0ul || !key_exists(g_nodes, id)) {
        return
    }
    let node = g_nodes[id]
    if (node.action == null && node.action_data == null) {
        return
    }
    g_pending |> push(PendingCall(id = id, action = node.action, action_data = node.action_data, data = node.data))
}

def private PlaySound(path : string; id : uint64) {
    if (!empty(path)) {
        g_sounds |> push(SoundEvent(path = path, node_id = id))
    }
}

def private UpdateInteraction() {
    let previous_hot = g_hot_id
    g_hot_id = HitTest()
    g_clicked_id = 0ul

    if (previous_hot != g_hot_id) {
        if (g_hot_id != 0ul) {
            let style = g_nodes[g_hot_id].style
            PlaySound(style.sound_hover, g_hot_id)
        }
    }

    if (g_mouse_pressed && g_hot_id != 0ul) {
        g_active_id = g_hot_id
        let style = g_nodes[g_active_id].style
        PlaySound(style.sound_press, g_active_id)
    }

    if (g_mouse_released) {
        if (g_active_id != 0ul && g_active_id == g_hot_id) {
            g_clicked_id = g_active_id
            let style = g_nodes[g_clicked_id].style
            PlaySound(style.sound_click, g_clicked_id)
            QueueAction(g_clicked_id)
        }
        g_active_id = 0ul
    }

    g_consumed_mouse = g_hot_id != 0ul || g_active_id != 0ul
}

def private AdvanceAnimations() {
    for (id in g_order) {
        var node = g_nodes[id]
        let anim = node.style.anim_time
        let speed = anim <= 0.0f ? 1.0f : g_dt / anim
        let hover_target = (id == g_hot_id || id == g_active_id) ? 1.0f : 0.0f
        let press_target = id == g_active_id ? 1.0f : 0.0f
        node.hover_t = Approach(node.hover_t, hover_target, speed)
        node.press_t = Approach(node.press_t, press_target, speed)
        if (node.pulse_t > 0.0f) {
            node.pulse_t = max(0.0f, node.pulse_t - g_dt * 2.5f)
        }
        g_nodes[id] = node
    }
}

//! Opens a UI frame: reads input, updates hover/press state and starts a new command list.
def public Begin() {
    EnsureClock()
    if (g_open) {
        // A previous frame was never closed (early return). Start over; stale nodes are swept
        // by the next End because their last_frame no longer matches.
        print("[UI] Begin called while a frame is open; the previous frame was dropped\n")
    }

    if (g_viewport_size.x <= 0.0f || g_viewport_size.y <= 0.0f) {
        g_viewport_origin = float2(0.0f, 0.0f)
        g_viewport_size = UiWindowSize()
    }

    g_dt = ClockDelta()

    let version = UiAtlasVersion()
    if (version != g_measure_version) {
        g_measure_version = version
        g_measure.clear()
    }

    // Input handling queues callbacks and sounds for this frame's End: clear the queues
    // before it, not after, or the click that opened the frame would be thrown away.
    g_pending.clear()
    g_sounds.clear()

    ReadInput()
    if (!g_open) {
        UpdateInteraction()
    }
    AdvanceAnimations()

    g_frame++
    g_open = true
    g_parent = 0ul
    g_salt = 0ul
    g_duplicate_warned = false
    g_order.clear()
    g_commands.clear()
    g_clip_stack.clear()

    UiFrameBegin()

    g_clip_stack |> push(float4(0.0f, 0.0f, g_viewport_size.x, g_viewport_size.y))
}

// --- layout --------------------------------------------------------------------------------

def private ContentBox(node : UiNode) : float4 {
    let pad = node.style.padding
    return float4(node.rect.x + pad.x, node.rect.y + pad.y, max(0.0f, node.rect.z - pad.x - pad.z),
                  max(0.0f, node.rect.w - pad.y - pad.w))
}

// Panels, columns and groups stack unanchored children vertically; rows stack them horizontally.
def private IsFlowContainer(kind : uint) : bool {
    return kind == UIK_PANEL || kind == UIK_COLUMN || kind == UIK_ROW || kind == UIK_GROUP
}

def private MeasureNode(id : uint64) {
    var node = g_nodes[id]
    let style = node.style
    let pad = style.padding
    let kind = node.kind

    var content = float2(0.0f, 0.0f)
    if (kind == UIK_LABEL) {
        let hash = MixId(HashString(node.text), uint64(max(0, int(style.text_size * 64.0f))))
        if (hash != node.text_hash) {
            node.text_hash = hash
            node.text_metric = MeasureText(node.text, style.text_size)
        }
        content = node.text_metric
        if (node.layout.size.x > 0.0f && content.x > node.layout.size.x) {
            let lines <- WrapText(node.text, style.text_size, node.layout.size.x)
            var widest = 0.0f
            for (line in lines) {
                widest = max(widest, MeasureText(line, style.text_size).x)
            }
            content = float2(widest, LineHeight(style.text_size) * float(length(lines)))
        } else {
            content = float2(content.x, max(content.y, LineHeight(style.text_size)))
        }
    } elif (kind == UIK_BUTTON) {
        let m = MeasureText(node.text, style.text_size)
        content = float2(m.x + pad.x + pad.z, max(m.y, LineHeight(style.text_size)) + pad.y + pad.w)
    } elif (kind == UIK_BAR) {
        content = float2(120.0f, LineHeight(style.text_size) + pad.y + pad.w)
    } elif (kind == UIK_SLIDER) {
        content = float2(160.0f, 22.0f)
    } elif (kind == UIK_CHECKBOX) {
        content = float2(22.0f, 22.0f)
    } elif (kind == UIK_IMAGE) {
        content = node.layout.size
    } elif (kind == UIK_SPACER) {
        content = node.layout.size
    } elif (IsFlowContainer(kind)) {
        var sum = float2(0.0f, 0.0f)
        var count = 0
        var child = node.first_child
        while (child != 0ul) {
            let child_node = g_nodes[child]
            if (child_node.layout.anchor.x == 0.0f && child_node.layout.anchor.y == 0.0f) {
                if (kind == UIK_ROW) {
                    sum.x += child_node.measured.x
                    sum.y = max(sum.y, child_node.measured.y)
                } else {
                    sum.x = max(sum.x, child_node.measured.x)
                    sum.y += child_node.measured.y
                }
                count++
            }
            child = child_node.next_sibling
        }
        if (count > 1) {
            if (kind == UIK_ROW) {
                sum.x += node.layout.spacing * float(count - 1)
            } else {
                sum.y += node.layout.spacing * float(count - 1)
            }
        }
        content = float2(sum.x + pad.x + pad.z, sum.y + pad.y + pad.w)
    }

    if (node.layout.size.x > 0.0f) {
        content.x = node.layout.size.x
    }
    if (node.layout.size.y > 0.0f) {
        content.y = node.layout.size.y
    }
    node.measured = content
    g_nodes[id] = node
}

def private ArrangeNode(id : uint64) {
    var node = g_nodes[id]
    let parent = node.parent
    let layout = node.layout

    var content : float4 = float4(0.0f)
    if (parent == 0ul) {
        content = float4(0.0f, 0.0f, g_viewport_size.x, g_viewport_size.y)
    } else {
        content = ContentBox(g_nodes[parent])
    }

    let absolute = parent == 0ul || layout.anchor.x != 0.0f || layout.anchor.y != 0.0f
    var width = node.measured.x
    var height = node.measured.y

    if (!absolute && layout.grow.x > 0.0f) {
        width = max(width, content.z * layout.grow.x)
    }
    if (!absolute && layout.grow.y > 0.0f) {
        let parent_node = g_nodes[parent]
        height = max(height, content.w - (parent_node.cursor.y - content.y))
    }

    var x = content.x
    var y = content.y
    if (absolute) {
        x = content.x + layout.anchor.x * (content.z - width) + layout.offset.x
        y = content.y + layout.anchor.y * (content.w - height) + layout.offset.y
        node.cursor = float2(x + node.style.padding.x, y + node.style.padding.y)
    } else {
        let parent_node = g_nodes[parent]
        let cursor = parent_node.cursor
        if (parent_node.kind == UIK_ROW) {
            x = cursor.x
            y = cursor.y
            var advanced = g_nodes[parent]
            advanced.cursor.x = cursor.x + width + parent_node.layout.spacing
            g_nodes[parent] = advanced
        } else {
            x = cursor.x
            y = cursor.y
            var advanced = g_nodes[parent]
            advanced.cursor.y = cursor.y + height + parent_node.layout.spacing
            g_nodes[parent] = advanced
        }
        node.cursor = float2(x + node.style.padding.x, y + node.style.padding.y)
    }

    node.rect = float4(x, y, width, height)
    if (parent == 0ul) {
        node.clip = float4(0.0f, 0.0f, g_viewport_size.x, g_viewport_size.y)
    } else {
        node.clip = g_nodes[parent].clip
    }
    g_nodes[id] = node
}

def private LayoutAll() {
    var i = length(g_order) - 1
    while (i >= 0) {
        MeasureNode(g_order[i])
        i--
    }
    for (id in g_order) {
        ArrangeNode(id)
    }
}

// --- drawing -------------------------------------------------------------------------------

def private CurrentClip() : float4 {
    if (length(g_clip_stack) == 0) {
        return float4(0.0f, 0.0f, g_viewport_size.x, g_viewport_size.y)
    }
    return g_clip_stack[length(g_clip_stack) - 1]
}

def private EmitQuad(rect : float4; color : float4; radius : float; scissor : float4) {
    if (color.w <= 0.0f || rect.z <= 0.0f || rect.w <= 0.0f) {
        return
    }
    var cmd : UiDrawCmd = default<UiDrawCmd>
    cmd.kind = UIDC_QUAD
    cmd.text_id = 0u
    cmd.material = null
    cmd.rect = rect
    cmd.color = Premul(color)
    cmd.extra = float4(max(0.0f, radius), 0.0f, 0.0f, 0.0f)
    cmd.scissor = scissor
    g_commands |> push(cmd)
}

def private EmitBorder(rect : float4; color : float4; radius : float; width : float; scissor : float4) {
    if (color.w <= 0.0f || width <= 0.0f || rect.z <= 0.0f || rect.w <= 0.0f) {
        return
    }
    var cmd : UiDrawCmd = default<UiDrawCmd>
    cmd.kind = UIDC_BORDER
    cmd.text_id = 0u
    cmd.material = null
    cmd.rect = rect
    cmd.color = Premul(color)
    cmd.extra = float4(max(0.0f, radius), width, 0.0f, 0.0f)
    cmd.scissor = scissor
    g_commands |> push(cmd)
}

def private EmitTextRun(rect : float4; text : string; px : float; color : float4; scissor : float4) {
    if (empty(text) || color.w <= 0.0f || rect.z <= 0.0f) {
        return
    }
    var cmd : UiDrawCmd = default<UiDrawCmd>
    cmd.kind = UIDC_TEXT
    cmd.text_id = UiTextId(text)
    cmd.material = null
    cmd.rect = rect
    cmd.color = Premul(color)
    cmd.extra = float4(px, 0.0f, 0.0f, 0.0f)
    cmd.scissor = scissor
    g_commands |> push(cmd)
}

def private EmitTextured(rect : float4; color : float4; material : TryMat?; scissor : float4) {
    if (material == null || rect.z <= 0.0f || rect.w <= 0.0f) {
        return
    }
    var cmd : UiDrawCmd = default<UiDrawCmd>
    cmd.kind = UIDC_TEXTURED
    cmd.text_id = 0u
    cmd.material = material
    cmd.rect = rect
    cmd.color = Premul(color)
    cmd.extra = float4(0.0f, 0.0f, 0.0f, 0.0f)
    cmd.scissor = scissor
    g_commands |> push(cmd)
}

def private StateFill(style : UiStyle; node : UiNode) : float4 {
    var color = style.color
    if (node.hover_t > 0.0f) {
        color = ColorLerp(color, style.hover_color, EaseOutCubic(node.hover_t))
    }
    if (node.press_t > 0.0f) {
        color = ColorLerp(color, style.press_color, EaseOutCubic(node.press_t))
    }
    if (node.pulse_t > 0.0f) {
        color = ColorLerp(color, float4(1.0f, 1.0f, 1.0f, color.w), EaseOutCubic(node.pulse_t) * 0.45f)
    }
    return color
}

def private MaterialFor(path : string) : TryMat? {
    if (empty(path)) {
        return null
    }
    var guid = 0ul
    if (key_exists(g_material_guid, path)) {
        guid = g_material_guid[path]
    } else {
        guid = ResolveAssetRef(path)
        if (guid != 0ul) {
            g_material_guid[path] = guid
        }
    }
    if (guid == 0ul) {
        return null
    }
    if (!ResourceIsReadyMaterial(guid)) {
        if (!key_exists(g_material_requested, guid)) {
            g_material_requested[guid] = 1
            RequestLoadMaterial(guid)
        }
        return null
    }
    return GetMaterialPointer(guid)
}

def private EmitLabel(node : UiNode; style : UiStyle; clip : float4) {
    let rect = node.rect
    let pad = style.padding
    EmitQuad(rect, style.color, style.radius, clip)
    EmitBorder(rect, style.border_color, style.radius, style.border_width, clip)

    let max_width = rect.z - pad.x - pad.z
    let lines <- WrapText(node.text, style.text_size, node.layout.size.x > 0.0f ? max_width : 0.0f)
    let line_height = LineHeight(style.text_size)
    var y = rect.y + pad.y
    for (line in lines) {
        if (!empty(line)) {
            let metric = MeasureText(line, style.text_size)
            var x = rect.x + pad.x
            if (style.text_align == 1) {
                x = rect.x + (rect.z - metric.x) * 0.5f
            } elif (style.text_align == 2) {
                x = rect.x + rect.z - pad.z - metric.x
            }
            EmitTextRun(float4(x, y, metric.x, metric.y), line, style.text_size, style.text_color, clip)
        }
        y += line_height
    }
}

def private EmitButton(node : UiNode; style : UiStyle; clip : float4) {
    let rect = node.rect
    EmitQuad(rect, StateFill(style, node), style.radius, clip)
    EmitBorder(rect, style.border_color, style.radius, style.border_width, clip)
    if (!empty(node.text)) {
        let metric = MeasureText(node.text, style.text_size)
        let x = rect.x + (rect.z - metric.x) * 0.5f
        let y = rect.y + (rect.w - metric.y) * 0.5f
        EmitTextRun(float4(x, y, metric.x, metric.y), node.text, style.text_size, style.text_color, clip)
    }
}

def private EmitBar(node : UiNode; style : UiStyle; clip : float4) {
    let rect = node.rect
    let radius = style.radius
    EmitQuad(rect, style.color, radius, clip)
    let value = clamp(node.value, 0.0f, 1.0f)
    let fill = float4(rect.x, rect.y, rect.z * value, rect.w)
    EmitQuad(fill, style.hover_color, radius, clip)
    EmitBorder(rect, style.border_color, radius, style.border_width, clip)
    if (!empty(node.text)) {
        let metric = MeasureText(node.text, style.text_size)
        let x = rect.x + (rect.z - metric.x) * 0.5f
        let y = rect.y + (rect.w - metric.y) * 0.5f
        EmitTextRun(float4(x, y, metric.x, metric.y), node.text, style.text_size, style.text_color, clip)
    }
}

def private EmitSlider(node : UiNode; style : UiStyle; clip : float4) {
    let rect = node.rect
    let track = float4(rect.x, rect.y + rect.w * 0.35f, rect.z, max(3.0f, rect.w * 0.3f))
    EmitQuad(track, style.color, style.radius, clip)
    let value = clamp(node.value, 0.0f, 1.0f)
    let knob = float2(rect.x + rect.z * value, rect.y + rect.w * 0.5f)
    let knob_size = max(10.0f, rect.w * 0.9f)
    EmitQuad(float4(knob.x - knob_size * 0.5f, knob.y - knob_size * 0.5f, knob_size, knob_size),
             StateFill(style, node), knob_size * 0.5f, clip)
}

def private EmitCheckbox(node : UiNode; style : UiStyle; clip : float4) {
    let rect = node.rect
    EmitQuad(rect, style.color, style.radius, clip)
    if (node.value > 0.5f) {
        let inset = max(2.0f, rect.z * 0.2f)
        EmitQuad(float4(rect.x + inset, rect.y + inset, rect.z - inset * 2.0f, rect.w - inset * 2.0f),
                 style.hover_color, max(0.0f, style.radius - inset), clip)
    }
    EmitBorder(rect, style.border_color, style.radius, style.border_width, clip)
}

def private EmitAll() {
    for (id in g_order) {
        let node = g_nodes[id]
        let style = node.style
        let clip = node.clip
        if (node.rect.z <= 0.0f || node.rect.w <= 0.0f) {
            continue
        }
        let kind = node.kind
        if (kind == UIK_PANEL || kind == UIK_COLUMN || kind == UIK_ROW || kind == UIK_GROUP) {
            EmitQuad(node.rect, style.color, style.radius, clip)
            EmitBorder(node.rect, style.border_color, style.radius, style.border_width, clip)
        } elif (kind == UIK_LABEL) {
            EmitLabel(node, style, clip)
        } elif (kind == UIK_BUTTON) {
            EmitButton(node, style, clip)
        } elif (kind == UIK_BAR) {
            EmitBar(node, style, clip)
        } elif (kind == UIK_SLIDER) {
            EmitSlider(node, style, clip)
        } elif (kind == UIK_CHECKBOX) {
            EmitCheckbox(node, style, clip)
        } elif (kind == UIK_IMAGE) {
            EmitTextured(node.rect, float4(1.0f, 1.0f, 1.0f, 1.0f), MaterialFor(style.material), clip)
        }
    }
}

// --- end -----------------------------------------------------------------------------------

def private SweepNodes() {
    // keys()/values() iterators lock the table: index it only after the loop, not inside it
    // (a uint64-key table throws "can't insert to a locked table" on indexing while iterated).
    var stale : array<uint64>
    for (id, node in keys(g_nodes), values(g_nodes)) {
        if (node.last_frame != g_frame) {
            stale |> push(id)
        }
    }
    for (id in stale) {
        erase(g_nodes, id)
    }
}

def private DispatchPending() {
    for (call in g_pending) {
        if (call.action != null) {
            invoke(call.action)
        } elif (call.action_data != null) {
            invoke(call.action_data, call.data)
        }
    }
    g_pending.clear()

    for (event in g_sounds) {
        if (g_sound_handler != null) {
            invoke(g_sound_handler, event.path, event.node_id)
        }
    }
    g_sounds.clear()
}

//! Closes the frame: layout, drawing, callbacks and sounds, then stale node cleanup.
[profile(name = "Ui.End")]
def public End() {
    if (!g_open) {
        return
    }

    LayoutAll()
    EmitAll()
    SweepNodes()
    g_open = false
    g_parent = 0ul
    DispatchPending()
}

//! Begin + declaration body + End. A body that returns early skips End; the next Begin reports
//! and recovers, but prefer the explicit Begin/End in game code.
[profile(name = "Ui.Frame")]
def public Frame(blk : block) {
    Begin()
    invoke(blk)
    End()
}

//! The command list the render pass reads through get_ui_draw_cmds.
[export]
def get_ui_draw_cmds() : array<UiDrawCmd>? {
    return unsafe(addr(g_commands))
}

// --- widgets ------------------------------------------------------------------------------

def private OpenContainer(nid : uint64) : uint64 {
    let saved = g_parent
    g_parent = nid
    return saved
}

def private CloseContainer(saved : uint64) {
    g_parent = saved
}

def private Container(id : string; kind : uint; style : UiStyle; layout : UiLayout; blk : block) {
    let nid = Acquire(id, kind)
    ApplyStyle(nid, kind, style, layout)
    let saved = OpenContainer(nid)
    invoke(blk)
    CloseContainer(saved)
}

//! A framed container: vertical flow plus a default background and padding.
def public Panel(id : string; style : UiStyle = default<UiStyle>; layout : UiLayout = default<UiLayout>; blk : block) {
    Container(id, UIK_PANEL, style, layout, blk)
}

//! Vertical flow container without a background by default.
def public Column(id : string; style : UiStyle = default<UiStyle>; layout : UiLayout = default<UiLayout>; blk : block) {
    Container(id, UIK_COLUMN, style, layout, blk)
}

//! Horizontal flow container.
def public Row(id : string; style : UiStyle = default<UiStyle>; layout : UiLayout = default<UiLayout>; blk : block) {
    Container(id, UIK_ROW, style, layout, blk)
}

//! Container without a background: unanchored children stack vertically, children with an
//! `anchor` are placed inside it by that anchor.
def public Group(id : string; style : UiStyle = default<UiStyle>; layout : UiLayout = default<UiLayout>; blk : block) {
    Container(id, UIK_GROUP, style, layout, blk)
}

//! Numeric id scope for lists: child ids stay stable per index without building strings.
def public For(id : string; count : int; blk : block<(index : int) : void>) {
    let nid = Acquire(id, UIK_GROUP)
    ApplyStyle(nid, UIK_GROUP, kNeutralStyle, UiLayout())
    let saved_parent = g_parent
    let saved_salt = g_salt
    g_parent = nid
    var i = 0
    while (i < count) {
        g_salt = MixId(saved_salt, uint64(i + 1))
        invoke(blk, i)
        i++
    }
    g_salt = saved_salt
    g_parent = saved_parent
}

def public Spacer(id : string; size : float2) {
    let nid = Acquire(id, UIK_SPACER)
    ApplyStyle(nid, UIK_SPACER, kNeutralStyle, UiLayout(size = size))
}

def public Label(id : string; text : string; style : UiStyle = default<UiStyle>; layout : UiLayout = default<UiLayout>) {
    let nid = Acquire(id, UIK_LABEL)
    SetText(nid, text)
    ApplyStyle(nid, UIK_LABEL, style, layout)
}

def public Button(id : string; action : UiAction; style : UiStyle = default<UiStyle>) : bool {
    return ButtonText(id, "", action, style, UiLayout())
}

def public Button(id : string; text : string; action : UiAction; style : UiStyle = default<UiStyle>) : bool {
    return ButtonText(id, text, action, style, UiLayout())
}

def public Button(tpl : UiButtonTemplate; id : string; action : UiAction) : bool {
    return ButtonText(id, tpl.text, action, tpl.style, tpl.layout)
}

def public Button(tpl : UiButtonTemplate; id : string; text : string; action : UiAction) : bool {
    var title = text
    if (empty(title)) {
        title = tpl.text
    }
    return ButtonText(id, title, action, tpl.style, tpl.layout)
}

def public ButtonData(tpl : UiButtonTemplate; id : string; text : string; action : UiActionData; data : int) : bool {
    let nid = Acquire(id, UIK_BUTTON)
    var title = text
    if (empty(title)) {
        title = tpl.text
    }
    SetText(nid, title)
    ApplyStyle(nid, UIK_BUTTON, tpl.style, tpl.layout)
    SetActionData(nid, action, data)
    return g_clicked_id == nid
}

def private ButtonText(id : string; text : string; action : UiAction; style : UiStyle; layout : UiLayout) : bool {
    let nid = Acquire(id, UIK_BUTTON)
    SetText(nid, text)
    ApplyStyle(nid, UIK_BUTTON, style, layout)
    SetAction(nid, action)
    return g_clicked_id == nid
}

def public ProgressBar(id : string; value : float; text : string = ""; style : UiStyle = default<UiStyle>) {
    let nid = Acquire(id, UIK_BAR)
    var node = g_nodes[nid]
    node.value = clamp(value, 0.0f, 1.0f)
    node.text = text
    g_nodes[nid] = node
    ApplyStyle(nid, UIK_BAR, style, UiLayout(size = float2(0.0f, 22.0f)))
}

//! Horizontal slider. The caller's variable is written back while the knob is dragged.
def public Slider(id : string; var value : float&; min_value : float; max_value : float;
                  style : UiStyle = default<UiStyle>) : bool {
    let nid = Acquire(id, UIK_SLIDER)
    ApplyStyle(nid, UIK_SLIDER, style, UiLayout(size = float2(0.0f, 22.0f)))
    MakeInteractive(nid)

    let span = max_value - min_value
    let rect = g_nodes[nid].rect
    var changed = false
    if (g_active_id == nid && g_mouse_down && rect.z > 0.0f && span != 0.0f) {
        let t = clamp((g_mouse.x - rect.x) / rect.z, 0.0f, 1.0f)
        let next = min_value + t * span
        if (next != value) {
            value = next
            changed = true
        }
    }
    var node = g_nodes[nid]
    node.value = span == 0.0f ? 0.0f : clamp((value - min_value) / span, 0.0f, 1.0f)
    g_nodes[nid] = node
    return changed
}

def public Checkbox(id : string; var value : bool&; style : UiStyle = default<UiStyle>) : bool {
    let nid = Acquire(id, UIK_CHECKBOX)
    ApplyStyle(nid, UIK_CHECKBOX, style, UiLayout())
    MakeInteractive(nid)
    var changed = false
    if (g_clicked_id == nid) {
        value = !value
        changed = true
    }
    var node = g_nodes[nid]
    node.value = value ? 1.0f : 0.0f
    g_nodes[nid] = node
    return changed
}

//! Textured widget. `path` is an authored `.mat` path resolved through the asset registry.
def public Image(id : string; path : string; size : float2; style : UiStyle = default<UiStyle>) {
    let nid = Acquire(id, UIK_IMAGE)
    var merged = style
    merged.material = path
    ApplyStyle(nid, UIK_IMAGE, merged, UiLayout(size = size))
}

//! Clips everything emitted until PopClip. The scissor is intersected with the previous clip.
def public PushClip(rect : float4) {
    let current = CurrentClip()
    g_clip_stack |> push(RectIntersect(current, rect))
}

def public PopClip() {
    if (length(g_clip_stack) > 1) {
        g_clip_stack.resize(length(g_clip_stack) - 1)
    }
}

def public ClickedId() : uint64 {
    return g_clicked_id
}

//! Resolved rectangle of a node addressed by its declaration path, e.g. "##panel/##button".
//! Diagnostics and tests only: ids of widgets created inside `For` include a per-item salt and
//! cannot be addressed this way.
def public DebugRect(path : string) : float4 {
    var id = 0ul
    let parts <- split(path, "/")
    for (part in parts) {
        if (!empty(part)) {
            id = MixId(id, MixId(0ul, HashString(part)))
        }
    }
    if (id == 0ul || !key_exists(g_nodes, id)) {
        return float4(0.0f)
    }
    return g_nodes[id].rect
}

def public DebugCommandCount() : int {
    return length(g_commands)
}

def public DebugCommandKind(index : int) : uint {
    if (index < 0 || index >= length(g_commands)) {
        return 0xFFFFFFFFu
    }
    return g_commands[index].kind
}

def public ActiveId() : uint64 {
    return g_active_id
}
