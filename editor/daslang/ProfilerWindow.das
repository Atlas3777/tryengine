options gen2
options relaxed_assign = false
options _allow_imgui_legacy = true

module ProfilerWindow shared public

//! Profiler panel: a shared timeline, the call tree of one frame, a multi-series plot and
//! the scope list.
//!
//! Frames come from the C++ profiler (engine/core/Profiler.hpp) through tryRenderer. The
//! engine never trims history: only Reset clears it. Export writes the whole capture to
//! logs/ in the compact format of ProfilerLog.hpp, the same format a running capture (game
//! client included) writes, and Load latest reads the newest of those back in.
//!
//! Call names and their text widths are cached per scope, and a window that holds thousands
//! of frames is drawn as an aggregate: the panel's own cost must stay out of the frame it
//! measures.
//!
//! Everything custom is painted through the window draw list (imgui_drawlist_builtin
//! primitives), which is why this file opts into the legacy imgui surface.

require tryengine/Profiling
require imgui
require imgui/imgui_widgets_builtin
require imgui/imgui_containers_builtin
require imgui/imgui_colors
require imgui/imgui_boost_runtime
require imgui/imgui_boost_v2
require tryRenderer
require math
require strings

let private LANE_HEIGHT = 20.0f
let private BAR_HEIGHT = 18.0f
let private TIMELINE_TOP = 34.0f
let private TIMELINE_BAND_GAP = 26.0f
let private MIN_BAR_WIDTH = 4.0f
let private PICK_SLOP = 2.0f
let private MIN_PX_PER_FRAME = 0.5f
// Deep enough that a microsecond call in a 16 ms frame spans the window: at 1e7 px per frame
// one pixel is 1.6 ns, finer than anything the timestamps resolve.
let private MAX_PX_PER_FRAME = 1.0e7f
let private DEFAULT_PX_PER_FRAME = 8.0f
// Names next to narrow bars need room between neighbours, hence the scale floor.
let private LABEL_PX_PER_FRAME = 120.0f
let private CALL_ZOOM_FILL = 0.8f
let private CALL_ZOOM_LEFT = 0.1f
let private MAX_CPU_LANES = 10
let private MAX_GPU_LANES = 10
let private TREE_ROW_HEIGHT = 16.0f
let private PLOT_HEIGHT = 120.0f
let private MAX_SERIES = 8
let private STATS_WINDOW = 60
let private TREE_INDENT = 14.0f
let private MAX_TREE_DEPTH = 32
// Below this many pixels per frame a bar is narrower than the space it describes: the
// timeline switches to an aggregated view instead of drawing every call of every frame.
let private AGG_COLUMN_PX = 3.0f
// A window with more calls than this is aggregated even when the bars would fit: walking
// them one by one is the cost the aggregated view exists to remove.
let private MAX_DRAWN_CALLS = 20000
// The aggregate summarises a window that moves by a frame per displayed frame; rebuilding it
// faster than this would only cost, never show.
let private AGG_REFRESH_SEC = 0.12f

let private COL_BG = rgba(18, 18, 22, 255)
let private COL_GRID = rgba(48, 48, 56, 255)
let private COL_FRAME_LINE = rgba(70, 70, 80, 255)
let private COL_TEXT = rgba(230, 230, 235, 255)
let private COL_TEXT_DIM = rgba(140, 140, 150, 255)
let private COL_FRAME_TIME = rgba(120, 120, 130, 255)
let private COL_HOVER = rgba(80, 90, 115, 90)
let private COL_SELECTED = rgba(110, 130, 175, 80)
let private COL_BAR = rgba(70, 110, 170, 55)
// The pinned frame is marked by a hairline, not a wash: a filled band hid the bars behind it.
let private COL_FOCUS = rgba(255, 200, 90, 190)
let private COL_GPU_BORDER = rgba(214, 196, 255, 170)
let private COL_GPU_DIVIDER = rgba(120, 100, 190, 180)
let private COL_OUTLINE = rgba(250, 250, 255, 230)
// One frame column out of two gets a whisper of a tint: neighbouring frames then read as
// separate columns instead of one long strip.
let private COL_COLUMN_TINT = rgba(130, 140, 170, 14)
let private COL_HOVER_OUTLINE = rgba(255, 255, 255, 110)
let private COL_BAR_TEXT = rgba(14, 14, 18, 235)

var private ROW_NAME : table<int; NarrativeState>
var private ROW_STATS : table<int; NarrativeState>
var private ROW_SAME_LINE : table<int; EmptyMarkerState>
var private ROW_SAME_LINE_2 : table<int; EmptyMarkerState>
var private ROW_MENU : table<int; ClickState>

// Rows keep registration order: re-sorting by time every frame made the list jump around.
var private row_order : array<uint>
var private row_order_count : int = -1

// Timeline camera. The left edge is stored split - a frame index plus the fraction of that
// frame already scrolled past - because one float cannot hold both: at 1e7 px per frame the
// offset inside a frame has to stay precise, and the frame number is the part that grows.
// `follow` pins the right edge to the newest frame; any manual move detaches it.
var private px_per_frame : float = DEFAULT_PX_PER_FRAME
var private cam_first : int = 0
var private cam_frac : float = 0.0f
var private follow : bool = true
// Content width of the last timeline draw, so the toolbar zooms the window it shows.
var private view_width : float = 400.0f

// Focused frame: -1 means "the newest frame in view". Clicking a bar pins a frame, and the
// call tree below always shows that frame.
var private focus_frame : int = -1
var private selected_event : int = -1
var private export_status : string = ""

// Cached depth-first order of one frame (event indices), rebuilt when the frame changes.
var private tree_rows : array<uint>
var private tree_frame : int = -1
var private tree_event_count : int = -1

// Right edge of the last bar label printed on each lane. A name is drawn only when it has
// room, so the timeline never turns into a pile of overlapping text.
var private label_end : array<float>

// Timeline bands: CPU calls on lanes by depth, GPU passes on their own lanes below a divider.
var private lane_scope_count : int = -1
var private cpu_lanes : int = 4
var private gpu_lanes : int = 2
var private gpu_lane_of : array<int>

// Plot series: scope indices picked from the row menu, plus an optional fixed scale so the
// curve does not breathe with every spike.
var private plot_series : array<uint>
var private plot_auto_scale : bool = true
var private plot_y_max : float = 16.0f

// Names and text widths per scope, read once per scope instead of once per call in the
// window: at 54k visible frames the per-call lookup alone was most of the panel's frame cost.
var private scope_names : array<string>
var private scope_name_width : array<float>
var private scope_names_count : int = -1

// Aggregated timeline: per column the frame time, the stacked phase times and the GPU total.
var private agg_dt : array<float>
var private agg_gpu : array<float>
var private agg_ms : array<float>
var private agg_phases : array<uint>
var private agg_columns : int = 0
var private agg_first : int = -1
var private agg_last : int = -1
var private agg_dt_max : float = 0.0f
var private agg_gpu_max : float = 0.0f
var private agg_stamp : float = -1000.0f
var private agg_clock : float = 0.0f

var private palette : array<uint>
var private gpu_palette : array<uint>
var private initialized : bool = false

def private EnsureInit() {
    if (initialized) {
        return
    }
    initialized = true
    palette |> push(rgba(94, 170, 255, 255))
    palette |> push(rgba(255, 150, 90, 255))
    palette |> push(rgba(150, 220, 120, 255))
    palette |> push(rgba(230, 120, 200, 255))
    palette |> push(rgba(240, 210, 110, 255))
    palette |> push(rgba(120, 220, 220, 255))
    palette |> push(rgba(200, 160, 250, 255))
    palette |> push(rgba(240, 120, 120, 255))
    // GPU passes stay in the violet family: the band, the outline and the hue all say "GPU".
    gpu_palette |> push(rgba(150, 120, 240, 235))
    gpu_palette |> push(rgba(196, 124, 232, 235))
    gpu_palette |> push(rgba(120, 140, 255, 235))
    gpu_palette |> push(rgba(222, 132, 214, 235))
    gpu_palette |> push(rgba(128, 166, 255, 235))
    gpu_palette |> push(rgba(176, 110, 232, 235))
    PROFILE_RECORD_TOGGLE.value = ProfileIsRecording()
    PROFILE_FOLLOW.value = follow
    PROFILE_AUTOSCALE.value = plot_auto_scale
    PROFILE_CAPTURE.value = ProfileIsCapturing()
}

def private SeriesColor(scope : uint) : uint {
    if (empty(palette)) {
        return COL_TEXT
    }
    return palette[int(scope) % length(palette)]
}

def private IsPlotted(scope : uint) : bool {
    for (item in plot_series) {
        if (item == scope) {
            return true
        }
    }
    return false
}

def private TogglePlot(scope : uint) {
    for (i in range(0, length(plot_series))) {
        if (plot_series[i] == scope) {
            plot_series |> erase(i)
            return
        }
    }
    if (length(plot_series) < MAX_SERIES) {
        plot_series |> push(scope)
    }
}

/// Registration order, rebuilt only when the scope set changes: no per-frame reshuffling.
def private EnsureRowOrder() {
    let count = int(ProfileScopeCount())
    if (count == row_order_count) {
        return
    }
    row_order_count = count
    row_order.clear()
    for (i in range(0, count)) {
        row_order |> push(uint(i))
    }
}

/// Names and text widths, refreshed only when the scope set changes. Reset and import change
/// the names behind the same count, so both call InvalidateScopeNames.
def private EnsureScopeNames() {
    let count = int(ProfileScopeCount())
    if (count == scope_names_count) {
        return
    }
    scope_names_count = count
    scope_names.clear()
    scope_name_width.clear()
    for (i in range(0, count)) {
        let name = ProfileScopeName(uint(i))
        scope_names |> push(name)
        scope_name_width |> push(CalcTextSize(name).x)
    }
}

def private InvalidateScopeNames() {
    scope_names_count = -1
}

def private ScopeName(scope : uint) : string {
    let index = int(scope)
    if (index < length(scope_names)) {
        return scope_names[index]
    }
    return ""
}

def private ScopeNameWidth(scope : uint) : float {
    let index = int(scope)
    if (index < length(scope_name_width)) {
        return scope_name_width[index]
    }
    return 0.0f
}

/// Last / average / peak over the recent window, read from the per-frame totals.
def private ScopeStats(scope : uint) : tuple<float; float; float> {
    let frames = int(ProfileFrameCount())
    let from = max(0, frames - STATS_WINDOW)
    var last = 0.0f
    var total = 0.0f
    var peak = 0.0f
    var samples = 0
    for (f in range(from, frames)) {
        let value = ProfileScopeHistoryAt(scope, uint(f))
        last = value
        total += value
        peak = max(peak, value)
        samples += 1
    }
    return (last, samples > 0 ? total / float(samples) : 0.0f, peak)
}

/// Width of the visible window, in frames: `width` pixels at the current scale, never more
/// than the whole capture.
def private ViewSpan(frames : int; width : float) : float {
    if (frames <= 0) {
        return 1.0f
    }
    return min(width / px_per_frame, float(frames))
}

/// Left edge of a frame's column in pixels. Deliberately relative to `cam_first`: the frame
/// number of a long capture never enters the float math, only the small offset into it.
def private FrameX(frame : int; origin_x : float; px : float) : float {
    return origin_x + (float(frame - cam_first) - cam_frac) * px
}

/// Park the window on the newest frame: the live edge is the right side of the view.
def private FollowCamera(frames : int; span : float) {
    let whole = int(floor(span))
    let part = span - float(whole)
    if (part <= 0.0f) {
        cam_first = max(0, frames - whole)
        cam_frac = 0.0f
    } else {
        cam_first = max(0, frames - whole - 1)
        cam_frac = 1.0f - part
    }
}

/// Keep the window inside the capture after a pan or a zoom. The fraction is normalized into
/// [0, 1) here, which is what lets the callers move the camera by per-pixel steps: those
/// steps are tiny next to a frame, and never touch the frame index itself.
def private ClampCamera(frames : int; span : float) {
    let shift = floor(cam_frac)
    cam_first += int(shift)
    cam_frac -= shift
    if (cam_first < 0) {
        cam_first = 0
        cam_frac = 0.0f
    }
    if (cam_first > frames - 1) {
        cam_first = frames - 1
        cam_frac = 0.0f
    }
    // Right edge past the newest frame: slide back by the overshoot, then re-normalize.
    let over = cam_frac + span - float(frames - cam_first)
    if (over > 0.0f) {
        cam_frac -= over
        let back = floor(cam_frac)
        cam_first = max(0, cam_first + int(back))
        cam_frac -= back
        if (cam_frac < 0.0f) {
            cam_frac = 0.0f
        }
    }
}

/// Leaving the live edge: the camera keeps its place and scale until the user re-attaches.
def private FreezeCamera() {
    follow = false
    PROFILE_FOLLOW.value = false
}

/// Frame the call tree shows: the pinned one, or the newest one inside the window.
def private ResolveFocus(frames : int; span : float) : int {
    if (frames <= 0) {
        return -1
    }
    if (focus_frame >= 0) {
        return clamp(focus_frame, 0, frames - 1)
    }
    return clamp(cam_first + int(ceil(cam_frac + span)) - 1, 0, frames - 1)
}

/// Zoom by `factor`, keeping the frame `offset` pixels from the left edge in place. Detaches
/// the camera: zooming in means the user stopped to look at one call.
def private ZoomAt(factor : float; offset : float; frames : int; width : float) {
    let before = width / ViewSpan(frames, width)
    let anchor = cam_frac + offset / before
    px_per_frame = clamp(px_per_frame * factor, MIN_PX_PER_FRAME, MAX_PX_PER_FRAME)
    FreezeCamera()
    if (frames <= 0) {
        return
    }
    let span = ViewSpan(frames, width)
    cam_frac = anchor - offset / (width / span)
    ClampCamera(frames, span)
}

/// Frame one call: its duration fills most of the width. The shortcut for "the smallest
/// thing in this frame, full screen".
def private ZoomToCall(frame : int; event : int; frames : int; width : float) {
    let dt = max(ProfileFrameMs(uint(frame)), 0.001f)
    let duration = ProfileEventDuration(uint(frame), uint(event))
    if (duration <= 0.0f) {
        return
    }
    px_per_frame = clamp(width * CALL_ZOOM_FILL * dt / duration, MIN_PX_PER_FRAME, MAX_PX_PER_FRAME)
    FreezeCamera()
    let span = ViewSpan(frames, width)
    // The call starts a tenth of a screen in, so what led up to it stays visible.
    cam_first = frame
    cam_frac = ProfileEventStart(uint(frame), uint(event)) / dt - CALL_ZOOM_LEFT * span
    ClampCamera(frames, span)
}

/// Depth-first order of one frame: every call right after its parent, siblings by start.
///
/// Events are stored in the order they finish, so a parent sits after its children. Children
/// lists are prepended while walking the events backwards, which keeps them ascending: for
/// calls of one thread that order is also the order they started in.
def private BuildTree(frame : int) {
    tree_frame = frame
    tree_rows.clear()
    tree_event_count = int(ProfileFrameEventCount(uint(frame)))
    if (tree_event_count <= 0) {
        return
    }

    var first_child : array<int>
    var next_sibling : array<int>
    var roots : array<int>
    first_child |> resize(tree_event_count)
    next_sibling |> resize(tree_event_count)
    for (i in range(0, tree_event_count)) {
        first_child[i] = -1
        next_sibling[i] = -1
    }

    for (k in range(0, tree_event_count)) {
        let i = tree_event_count - 1 - k
        let parent = int(ProfileEventParent(uint(frame), uint(i)))
        if (parent < 0 || parent >= tree_event_count) {
            roots |> push(i)
            continue
        }
        next_sibling[i] = first_child[parent]
        first_child[parent] = i
    }

    var stack : array<int>
    for (k in range(0, length(roots))) {
        stack |> push(roots[length(roots) - 1 - k])
    }
    var kids : array<int>
    while (length(stack) > 0) {
        let node = stack[length(stack) - 1]
        pop(stack)
        tree_rows |> push(uint(node))
        kids.clear()
        var child = first_child[node]
        while (child >= 0) {
            kids |> push(child)
            child = next_sibling[child]
        }
        for (k in range(0, length(kids))) {
            stack |> push(kids[length(kids) - 1 - k])
        }
    }
}

def private EnsureTree(frame : int) {
    let count = int(ProfileFrameEventCount(uint(frame)))
    if (tree_frame == frame && tree_event_count == count) {
        return
    }
    BuildTree(frame)
}

/// "Root > Parent > Leaf" of one event, walking the parent links upwards.
def private EventPath(frame : int; event : int) : string {
    let count = int(ProfileFrameEventCount(uint(frame)))
    if (event < 0 || event >= count) {
        return ""
    }
    var path = ProfileScopeName(ProfileEventScope(uint(frame), uint(event)))
    var parent = int(ProfileEventParent(uint(frame), uint(event)))
    var guard = 0
    while (parent >= 0 && parent < count && guard < MAX_TREE_DEPTH) {
        let scope = ProfileEventScope(uint(frame), uint(parent))
        path = ProfileScopeName(scope) + " > " + path
        parent = int(ProfileEventParent(uint(frame), uint(parent)))
        guard += 1
    }
    return path
}

def private GpuColor(scope : uint) : uint {
    if (empty(gpu_palette)) {
        return COL_TEXT
    }
    return gpu_palette[int(scope) % length(gpu_palette)]
}

/// Lane counts come from the scope table: CPU lanes are the deepest nesting, GPU lanes are
/// the number of distinct passes. Cached because it walks every registered scope.
def private EnsureLaneCounts() {
    let count = int(ProfileScopeCount())
    if (count == lane_scope_count) {
        return
    }
    lane_scope_count = count
    var cpu = 1
    var gpu = 0
    gpu_lane_of |> resize(count)
    for (i in range(0, count)) {
        if (ProfileScopeKind(uint(i)) == 1) {
            // Пасс получает свою дорожку: два Opaque кадра лягут в одну полосу и не
            // перекроются, потому что они идут в разные моменты кадра.
            gpu_lane_of[i] = gpu < MAX_GPU_LANES ? gpu : MAX_GPU_LANES - 1
            gpu += 1
        } else {
            gpu_lane_of[i] = -1
            cpu = max(cpu, int(ProfileScopeDepth(uint(i))) + 1)
        }
    }
    cpu_lanes = clamp(cpu, 1, MAX_CPU_LANES)
    gpu_lanes = clamp(gpu, 0, MAX_GPU_LANES)
}

/// Vertical position of one call inside the timeline bands.
def private LaneY(gpu : bool; lane : int; top : float; gpu_top : float) : float {
    if (gpu) {
        return gpu_top + float(clamp(lane, 0, max(gpu_lanes - 1, 0))) * LANE_HEIGHT
    }
    return top + float(min(lane, cpu_lanes - 1)) * LANE_HEIGHT
}

/// Lane of a GPU pass: its index among the GPU scopes, so it never moves between frames.
def private GpuLane(scope : uint) : int {
    let index = int(scope)
    if (index < 0 || index >= length(gpu_lane_of)) {
        return 0
    }
    return max(gpu_lane_of[index], 0)
}

/// Pixel span of one call: the frame column, then the call inside it. Bars keep a 4 px
/// minimum, so a call of a few microseconds still has something to aim at.
def private BarSpan(frame : int; px : float; origin_x : float; start : float; duration : float; dt : float) : tuple<float; float> {
    let x0 = FrameX(frame, origin_x, px)
    let x1 = x0 + clamp(start / dt, 0.0f, 1.0f) * px
    let x2 = max(x1 + MIN_BAR_WIDTH, x0 + clamp((start + duration) / dt, 0.0f, 1.0f) * px)
    return (x1, x2)
}

/// Which of two candidate bars wins: the deepest, so a click inside a nested call never
/// reports its parent; a bar the cursor is strictly inside over one it reached only through
/// the slop; and the narrower call between near equals.
def private PickWins(strict : bool; depth : int; duration : float; best_strict : bool; best_depth : int; best_duration : float) : bool {
    if (best_depth < 0) {
        return true
    }
    if (strict != best_strict) {
        return strict
    }
    if (depth != best_depth) {
        return depth > best_depth
    }
    return duration < best_duration
}

/// Call under the cursor, or -1 for empty canvas (then only the frame is selected).
def private PickCall(frame : int; mouse : float2; origin_x : float; px : float; cpu_top : float; gpu_top : float) : int {
    let count = int(ProfileFrameEventCount(uint(frame)))
    let dt = max(ProfileFrameMs(uint(frame)), 0.001f)
    var best = -1
    var best_strict = false
    var best_depth = -1
    var best_duration = 0.0f
    for (e in range(0, count)) {
        let gpu = ProfileEventKind(uint(frame), uint(e)) == 1
        let scope = ProfileEventScope(uint(frame), uint(e))
        let lane = gpu ? GpuLane(scope) : int(ProfileEventDepth(uint(frame), uint(e)))
        let lane_y = LaneY(gpu, lane, cpu_top, gpu_top)
        if (mouse.y < lane_y || mouse.y > lane_y + BAR_HEIGHT) {
            continue
        }
        let depth = int(ProfileEventDepth(uint(frame), uint(e)))
        let duration = ProfileEventDuration(uint(frame), uint(e))
        let (x1, x2) = BarSpan(frame, px, origin_x, ProfileEventStart(uint(frame), uint(e)), duration, dt)
        let strict = mouse.x >= x1 && mouse.x <= x2
        if (!strict && (mouse.x < x1 - PICK_SLOP || mouse.x > x2 + PICK_SLOP)) {
            continue
        }
        if (PickWins(strict, depth, duration, best_strict, best_depth, best_duration)) {
            best = e
            best_strict = strict
            best_depth = depth
            best_duration = duration
        }
    }
    return best
}

/// Outline of one call. It reads the same rect the bars are drawn from, so a highlight can
/// never land on a different bar than the one the cursor is on.
def private DrawCallOutline(frame : int; event : int; origin_x : float; px : float; cpu_top : float; gpu_top : float; color : uint; thickness : float) {
    let gpu = ProfileEventKind(uint(frame), uint(event)) == 1
    let scope = ProfileEventScope(uint(frame), uint(event))
    let lane = gpu ? GpuLane(scope) : int(ProfileEventDepth(uint(frame), uint(event)))
    let lane_y = LaneY(gpu, lane, cpu_top, gpu_top)
    let dt = max(ProfileFrameMs(uint(frame)), 0.001f)
    let (x1, x2) = BarSpan(frame, px, origin_x, ProfileEventStart(uint(frame), uint(event)), ProfileEventDuration(uint(frame), uint(event)), dt)
    var dl = GetWindowDrawList()
    *dl |> AddRect(float2(x1 - 1.0f, lane_y - 1.0f), float2(x2 + 1.0f, lane_y + BAR_HEIGHT + 1.0f), color, 2.0f,
                   ImDrawFlags.None, thickness)
}

/// Zoom printed compactly: the useful range covers six orders of magnitude.
def private ZoomLabel(px : float) : string {
    if (px >= 1.0e6f) {
        let millions = px / 1.0e6f
        return "{millions:5.2f}M px/frame"
    }
    if (px >= 1.0e3f) {
        let thousands = px / 1.0e3f
        return "{thousands:5.2f}k px/frame"
    }
    return "{px:5.2f} px/frame"
}

/// A window of thousands of frames is a summary, not a wall of hairlines: one column per few
/// pixels, the frame time as a pale bar and the phase scopes stacked over it. Whole-window
/// work is a few hundred calls, where one bar per call would be one per call of every frame.
def private IsAggregateView(px : float; first : int; last : int) : bool {
    if (px < MIN_BAR_WIDTH) {
        return true
    }
    return ProfileRangeEventCount(uint(first), uint(last)) > uint64(MAX_DRAWN_CALLS)
}

/// Rebuilds the aggregated columns for [first, last]. Phases are the root's direct CPU
/// children: deeper calls nest, and stacking nested calls would count the same time twice.
def private RefreshAggregate(first : int; last : int; columns : int) {
    let span = last - first + 1
    if (span < 1) {
        return
    }
    // The aggregate is drawn before the scope list is built, so it cannot rely on row order.
    EnsureRowOrder()
    agg_first = first
    agg_last = last
    agg_columns = columns

    agg_phases.clear()
    var gpu_scopes : array<uint>
    for (scope in row_order) {
        if (int(ProfileScopeKind(scope)) == 1) {
            gpu_scopes |> push(scope)
        } elif (int(ProfileScopeDepth(scope)) == 1) {
            agg_phases |> push(scope)
        }
    }
    let phases = length(agg_phases)
    agg_ms |> resize(columns * max(1, phases))
    agg_dt |> resize(columns)
    agg_gpu |> resize(columns)
    agg_dt_max = 0.0f
    agg_gpu_max = 0.0f
    for (i in range(0, length(agg_ms))) {
        agg_ms[i] = 0.0f
    }

    for (c in range(0, columns)) {
        let lo_index = first + int(float(span) * float(c) / float(columns))
        let hi_index = first + int(float(span) * float(c + 1) / float(columns)) - 1
        let lo = uint(clamp(lo_index, first, last))
        let hi = uint(clamp(max(lo_index, hi_index), first, last))
        let dt = ProfileRangeFrameMsMax(lo, hi)
        agg_dt[c] = dt
        agg_dt_max = max(agg_dt_max, dt)
        for (p in range(0, phases)) {
            agg_ms[c * phases + p] = ProfileRangeScopeMs(agg_phases[p], lo, hi)
        }
        var gpu = 0.0f
        for (scope in gpu_scopes) {
            gpu += ProfileRangeScopeMs(scope, lo, hi)
        }
        agg_gpu[c] = gpu
        agg_gpu_max = max(agg_gpu_max, gpu)
    }
}

def private DrawTimelineAggregate(focus : int; first_frame : int; last_frame : int; rmin : float2; rmax : float2;
                                  cpu_top : float; gpu_top : float; px : float; width : float) {
    let columns = max(1, int(width / AGG_COLUMN_PX))
    agg_clock += GetIO().DeltaTime
    // Refreshing on every displayed frame would rebuild the summary faster than it can
    // change: while the camera follows the live edge the window moves by one frame per frame,
    // a fraction of a percent of its width. The summary is rebuilt on the throttle, on a
    // zoom (column count changes) and on a jump the cached window no longer covers.
    let span_frames = max(1, last_frame - first_frame)
    let shift = abs(first_frame - agg_first)
    let moved = shift * 16 > span_frames
    let expired = agg_clock - agg_stamp > AGG_REFRESH_SEC
    let stale = agg_first < 0 || agg_columns != columns || moved || expired
    if (stale) {
        RefreshAggregate(first_frame, last_frame, columns)
        agg_stamp = agg_clock
    }

    var dl = GetWindowDrawList()
    let band = float(cpu_lanes) * LANE_HEIGHT
    let dt_ref = max(agg_dt_max, 0.0001f)
    let phases = length(agg_phases)
    for (c in range(0, agg_columns)) {
        let x0 = rmin.x + float(c) * AGG_COLUMN_PX
        let x1 = x0 + AGG_COLUMN_PX - 1.0f
        if (x1 < rmin.x || x0 > rmax.x) {
            continue
        }
        // Pale bar: what the frame cost against the tallest column in the window.
        let dt_height = band * agg_dt[c] / dt_ref
        *dl |> AddRectFilled(float2(x0, cpu_top + band - dt_height), float2(x1, cpu_top + band), COL_BAR, 0.0f,
                             ImDrawFlags.None)
        // Stacked phases from the bottom up. A column never grows past the band: the sum of
        // the phases can exceed the frame (a gap in the names, not a lie about the frame).
        var used = 0.0f
        for (p in range(0, phases)) {
            let ms = agg_ms[c * phases + p]
            if (ms <= 0.0f) {
                continue
            }
            let y1 = cpu_top + band - used
            if (y1 <= cpu_top) {
                break
            }
            let y0 = max(cpu_top, y1 - band * ms / dt_ref)
            used += y1 - y0
            *dl |> AddRectFilled(float2(x0, y0), float2(x1, y1), SeriesColor(agg_phases[p]), 0.0f, ImDrawFlags.None)
        }
    }

    if (gpu_lanes > 0) {
        let gpu_band = float(gpu_lanes) * LANE_HEIGHT
        let gpu_ref = max(agg_gpu_max, 0.0001f)
        let divider_y = gpu_top - TIMELINE_BAND_GAP + 6.0f
        *dl |> AddLine(float2(rmin.x, divider_y), float2(rmax.x, divider_y), COL_GPU_DIVIDER, 1.0f)
        *dl |> AddText(float2(rmin.x + 8.0f, divider_y + 2.0f), COL_GPU_BORDER, "GPU")
        for (c in range(0, agg_columns)) {
            let x0 = rmin.x + float(c) * AGG_COLUMN_PX
            let x1 = x0 + AGG_COLUMN_PX - 1.0f
            if (x1 < rmin.x || x0 > rmax.x || agg_gpu[c] <= 0.0f) {
                continue
            }
            let height = gpu_band * agg_gpu[c] / gpu_ref
            *dl |> AddRectFilled(float2(x0, gpu_top + gpu_band - height), float2(x1, gpu_top + gpu_band),
                                 COL_GPU_DIVIDER, 0.0f, ImDrawFlags.None)
        }
    }

    // Pinned frame: the same hairline as the detailed view, so freezing one frame reads the
    // same in both views.
    if (focus >= first_frame && focus <= last_frame) {
        let fx = FrameX(focus, rmin.x, px)
        *dl |> AddLine(float2(fx, rmin.y), float2(fx, rmax.y), COL_FOCUS, 2.0f)
    }

    let frames = int(ProfileFrameCount())
    let avg = ProfileRangeFrameMsAvg(uint(first_frame), uint(last_frame))
    let mode = follow ? "live" : "frozen"
    var header = "frames {first_frame}..{last_frame} / {frames}   aggregated {last_frame - first_frame + 1} frames"
    header += "   avg {avg:5.2f} ms   max {agg_dt_max:5.2f} ms   {ZoomLabel(px)}   {mode}"
    *dl |> AddText(rmin + float2(8.0f, 4.0f), COL_TEXT_DIM, header)

    // Legend of the stacked phases, left to right, only the ones the window actually shows.
    var legend_x = rmin.x + 8.0f
    let legend_y = rmin.y + 19.0f
    for (p in range(0, min(phases, 8))) {
        let name = ScopeName(agg_phases[p])
        let name_w = ScopeNameWidth(agg_phases[p])
        if (legend_x + name_w + 16.0f > rmax.x) {
            break
        }
        *dl |> AddRectFilled(float2(legend_x, legend_y + 3.0f), float2(legend_x + 8.0f, legend_y + 11.0f),
                             SeriesColor(agg_phases[p]), 0.0f, ImDrawFlags.None)
        *dl |> AddText(float2(legend_x + 12.0f, legend_y), COL_TEXT, name)
        legend_x += name_w + 24.0f
    }
}

def private DrawTimeline(focus : int) {
    EnsureScopeNames()
    let width = max(120.0f, GetContentRegionAvail().x)
    view_width = width
    let band_gap = gpu_lanes > 0 ? TIMELINE_BAND_GAP : 0.0f
    let height = TIMELINE_TOP + float(cpu_lanes) * LANE_HEIGHT + band_gap + float(gpu_lanes) * LANE_HEIGHT + 6.0f
    InvisibleButton("##profiler_timeline", ImVec2(width, height))
    let rmin = GetItemRectMin()
    let rmax = GetItemRectMax()
    var dl = GetWindowDrawList()

    *dl |> AddRectFilled(rmin, rmax, COL_BG, 0.0f, ImDrawFlags.None)

    let frames = int(ProfileFrameCount())
    if (frames <= 0) {
        *dl |> AddText(rmin + float2(8.0f, 6.0f), COL_TEXT_DIM, "no frames recorded yet")
        return
    }

    var span = ViewSpan(frames, width)
    if (follow) {
        FollowCamera(frames, span)
    }
    let hovered = IsItemHovered(ImGuiHoveredFlags.None)
    let mouse = GetMousePos()
    let cpu_top = rmin.y + TIMELINE_TOP
    let gpu_top = cpu_top + float(cpu_lanes) * LANE_HEIGHT + band_gap
    let px_in = width / span
    // The frame under the cursor, in camera-relative terms: the same sum done against the
    // absolute frame number would lose the sub-frame part at a long capture.
    let mouse_frame = clamp(cam_first + int(floor(cam_frac + (mouse.x - rmin.x) / px_in)), 0, frames - 1)

    // Wheel zooms at the cursor: the call under the mouse stays under the mouse, which is the
    // only way to open up a call that is a hairline at frame scale.
    let wheel = GetIO().MouseWheel
    if (hovered && wheel != 0.0f) {
        ZoomAt(pow(1.3f, wheel), mouse.x - rmin.x, frames, width)
        span = ViewSpan(frames, width)
    }

    // A click on a bar selects that call; a click on empty space selects only the frame. A
    // double click also zooms the picked call, so one gesture answers "what is this thing".
    if (hovered && IsMouseClicked(ImGuiMouseButton.Left, false)) {
        let hit = PickCall(mouse_frame, mouse, rmin.x, px_in, cpu_top, gpu_top)
        focus_frame = mouse_frame
        selected_event = hit
        FreezeCamera()
        if (hit >= 0 && IsMouseDoubleClicked(ImGuiMouseButton.Left)) {
            ZoomToCall(mouse_frame, hit, frames, width)
        }
    }

    // Dragging pans, and detaches the camera: the user is looking at a piece of the past.
    if (IsItemActive()) {
        let delta = GetIO().MouseDelta.x
        if (abs(delta) > 0.0f) {
            cam_frac -= delta / (width / span)
            FreezeCamera()
            ClampCamera(frames, span)
        }
    }

    // The camera as it stands after the input; a double click can have changed it mid-frame.
    span = ViewSpan(frames, width)
    let px = width / span
    let first_frame = max(0, min(frames - 1, cam_first))
    let last_frame = max(first_frame, min(frames - 1, cam_first + int(ceil(cam_frac + span))))
    let hover_frame = clamp(cam_first + int(floor(cam_frac + (mouse.x - rmin.x) / px)), 0, frames - 1)
    var hover_event = -1
    if (hovered) {
        hover_event = PickCall(hover_frame, mouse, rmin.x, px, cpu_top, gpu_top)
    }

    PushClipRect(rmin, rmax, true)

    // A window of thousands of frames is drawn as a summary: the per-frame column work below
    // is linear in the number of visible frames, which is exactly what must not happen when
    // the whole capture is on screen.
    if (IsAggregateView(px, first_frame, last_frame)) {
        DrawTimelineAggregate(focus, first_frame, last_frame, rmin, rmax, cpu_top, gpu_top, px, width)
        PopClipRect()
        return
    }

    // Column background and the frame index above it: without them a column that the window
    // cuts in half reads as a broken bar instead of a frame that continues off screen.
    let lanes_bottom = cpu_top + float(cpu_lanes) * LANE_HEIGHT + band_gap + float(gpu_lanes) * LANE_HEIGHT
    for (f in range(first_frame, last_frame + 1)) {
        let x = FrameX(f, rmin.x, px)
        let next_x = x + px
        if (next_x < rmin.x || x > rmax.x) {
            continue
        }
        if ((f % 2) != 0) {
            *dl |> AddRectFilled(float2(max(x, rmin.x), rmin.y + 17.0f), float2(min(next_x, rmax.x), lanes_bottom),
                                 COL_COLUMN_TINT, 0.0f, ImDrawFlags.None)
        }
        if (px >= 60.0f && x >= rmin.x - 1.0f) {
            *dl |> AddText(float2(x + 4.0f, rmin.y + 19.0f), COL_TEXT_DIM, "{f}")
        }
    }

    for (f in range(first_frame, last_frame + 1)) {
        let x = FrameX(f, rmin.x, px)
        if (x < rmin.x - 1.0f || x > rmax.x + 1.0f) {
            continue
        }
        *dl |> AddLine(float2(x, rmin.y), float2(x, rmax.y), COL_FRAME_LINE, 1.0f)
    }

    // GPU band sits below a divider and keeps its own violet palette: the two kinds of time
    // must never be confused for one another.
    if (gpu_lanes > 0) {
        let divider_y = gpu_top - band_gap + 6.0f
        *dl |> AddLine(float2(rmin.x, divider_y), float2(rmax.x, divider_y), COL_GPU_DIVIDER, 1.0f)
        *dl |> AddText(float2(rmin.x + 8.0f, divider_y + 2.0f), COL_GPU_BORDER, "GPU")
    }

    // One label slot per lane: a name is printed only when it does not run into the previous
    // one, so the timeline stays readable at any zoom instead of turning into a text pile.
    let lane_total = cpu_lanes + gpu_lanes
    if (length(label_end) != lane_total) {
        label_end |> resize(lane_total)
    }
    for (i in range(0, lane_total)) {
        label_end[i] = -1.0e9f
    }

    for (f in range(first_frame, last_frame + 1)) {
        let dt = max(ProfileFrameMs(uint(f)), 0.001f)
        for (e in range(0, int(ProfileFrameEventCount(uint(f))))) {
            let scope = ProfileEventScope(uint(f), uint(e))
            let gpu = ProfileEventKind(uint(f), uint(e)) == 1
            let lane = gpu ? GpuLane(scope) : int(ProfileEventDepth(uint(f), uint(e)))
            let lane_y = LaneY(gpu, lane, cpu_top, gpu_top)
            let duration = ProfileEventDuration(uint(f), uint(e))
            let (x1, x2) = BarSpan(f, px, rmin.x, ProfileEventStart(uint(f), uint(e)), duration, dt)
            if (x2 < rmin.x - 4.0f || x1 > rmax.x + 4.0f) {
                // Off screen: this is what keeps a deep zoom from feeding the draw list
                // coordinates in the millions.
                continue
            }
            let lane_slot = min(lane_total - 1, gpu ? cpu_lanes + lane : lane)
            let color = gpu ? GpuColor(scope) : SeriesColor(scope)
            *dl |> AddRectFilled(float2(x1, lane_y), float2(x2, lane_y + BAR_HEIGHT), color, 2.0f, ImDrawFlags.None)
            if (gpu) {
                *dl |> AddRect(float2(x1, lane_y), float2(x2, lane_y + BAR_HEIGHT), COL_GPU_BORDER, 2.0f,
                               ImDrawFlags.None, 1.0f)
            }

            // The name goes inside the bar as soon as it fits; a bar too narrow for it gets
            // the name to the right, but only when that space is free.
            let name = ScopeName(scope)
            let name_w = ScopeNameWidth(scope)
            if (x2 - x1 >= name_w + 8.0f) {
                let clip_lo = max(x1, rmin.x) + 2.0f
                let clip_hi = min(x2, rmax.x) - 2.0f
                PushClipRect(float2(clip_lo, lane_y), float2(clip_hi, lane_y + BAR_HEIGHT), true)
                *dl |> AddText(float2(clip_lo + 2.0f, lane_y + 2.0f), COL_BAR_TEXT, name)
                PopClipRect()
                label_end[lane_slot] = max(label_end[lane_slot], x2)
            } elif (px >= LABEL_PX_PER_FRAME && x1 > label_end[lane_slot]) {
                *dl |> AddText(float2(x2 + 6.0f, lane_y + 2.0f), COL_TEXT, name)
                label_end[lane_slot] = x2 + 6.0f + name_w
            }
        }
    }

    // What a click would pick, drawn before the click lands: the highlight and the pick come
    // from one rect, so they cannot disagree about which call is under the cursor.
    if (hover_event >= 0 && !(hover_frame == focus && hover_event == selected_event)) {
        DrawCallOutline(hover_frame, hover_event, rmin.x, px, cpu_top, gpu_top, COL_HOVER_OUTLINE, 1.0f)
    }
    if (selected_event >= 0 && focus >= first_frame && focus <= last_frame &&
        selected_event < int(ProfileFrameEventCount(uint(focus)))) {
        DrawCallOutline(focus, selected_event, rmin.x, px, cpu_top, gpu_top, COL_OUTLINE, 2.0f)
    }

    // Pinned frame: a hairline at the frame's left edge, so the bars behind it stay readable.
    if (focus >= first_frame && focus <= last_frame) {
        let fx = FrameX(focus, rmin.x, px)
        *dl |> AddLine(float2(fx, rmin.y), float2(fx, rmax.y), COL_FOCUS, 2.0f)
    }

    let mode = follow ? "live" : "frozen"
    var header = "frames {first_frame}..{last_frame} / {frames}   {ZoomLabel(px)}   cpu 0..{cpu_lanes - 1}   {mode}"
    if (gpu_lanes > 0) {
        header = header + "   gpu lanes below"
    }
    *dl |> AddText(rmin + float2(8.0f, 4.0f), COL_TEXT_DIM, header)

    PopClipRect()
}

def private DrawPlot(frames : int; span : float) {
    let width = max(120.0f, GetContentRegionAvail().x)
    InvisibleButton("##profiler_plot", ImVec2(width, PLOT_HEIGHT))
    let rmin = GetItemRectMin()
    let rmax = GetItemRectMax()
    var dl = GetWindowDrawList()

    *dl |> AddRectFilled(rmin, rmax, COL_BG, 0.0f, ImDrawFlags.None)
    let first = max(0, min(cam_first, frames - 1))
    let last = min(frames, max(first + 2, cam_first + int(ceil(cam_frac + span))))
    if (last <= first) {
        return
    }

    var y_max = plot_y_max
    if (plot_auto_scale) {
        y_max = 0.0f
        for (scope in plot_series) {
            for (f in range(first, last)) {
                y_max = max(y_max, ProfileScopeHistoryAt(scope, uint(f)))
            }
        }
        y_max = max(y_max * 1.15f, 1.0f)
    }
    let y_scale = max(y_max, 0.001f)
    let plot_span = float(last - first)
    let plot_h = rmax.y - rmin.y
    let plot_w = rmax.x - rmin.x

    for (i in range(0, 5)) {
        let ty = rmin.y + plot_h * float(i) / 4.0f
        *dl |> AddLine(float2(rmin.x, ty), float2(rmax.x, ty), COL_GRID, 1.0f)
        let value = y_scale * (1.0f - float(i) / 4.0f)
        *dl |> AddText(float2(rmin.x + 4.0f, ty + 1.0f), COL_TEXT_DIM, "{value:.1f}")
    }

    // Frame time as the reference curve.
    var prev_y = rmin.y + plot_h
    for (i in range(0, int(span))) {
        let f = first + i
        let x = rmin.x + float(i) / plot_span * plot_w
        let y = rmin.y + plot_h * (1.0f - clamp(ProfileFrameMs(uint(f)) / y_scale, 0.0f, 1.0f))
        if (i > 0) {
            *dl |> AddLine(float2(x - plot_w / plot_span, prev_y), float2(x, y), COL_FRAME_TIME, 1.0f)
        }
        prev_y = y
    }

    for (scope in plot_series) {
        let color = SeriesColor(scope)
        var prev = float2(rmin.x, rmin.y + plot_h)
        for (i in range(0, int(span))) {
            let f = first + i
            let x = rmin.x + float(i) / plot_span * plot_w
            let value = clamp(ProfileScopeHistoryAt(scope, uint(f)) / y_scale, 0.0f, 1.0f)
            let y = rmin.y + plot_h * (1.0f - value)
            if (i > 0) {
                *dl |> AddLine(prev, float2(x, y), color, 1.5f)
            }
            prev = float2(x, y)
        }
    }

    var legend_y = rmin.y + 4.0f
    for (scope in plot_series) {
        let lx = rmax.x - 170.0f
        *dl |> AddRectFilled(float2(lx, legend_y + 2.0f), float2(lx + 10.0f, legend_y + 10.0f), SeriesColor(scope),
                             0.0f, ImDrawFlags.None)
        *dl |> AddText(float2(lx + 14.0f, legend_y), COL_TEXT, ProfileScopeName(scope))
        legend_y += 13.0f
    }
}

/// Details of the selected call: the call itself plus what the function costs over history.
def private DrawSelectionDetails(frame : int) {
    separator(PROFILER_SEP_SELECTION)
    let count = int(ProfileFrameEventCount(uint(frame)))
    if (selected_event < 0 || selected_event >= count) {
        // Without a call the block still says which frame is pinned, otherwise a click on
        // empty canvas looks like nothing happened.
        let frame_ms = max(ProfileFrameMs(uint(frame)), 0.001f)
        text(PROFILER_SEL_TITLE, (text = "frame {frame}   {frame_ms:7.3f} ms   {count} calls"))
        text(PROFILER_SEL_HINT, (text = "no call selected: click a bar on the timeline or a row in the tree"))
        return
    }
    let event = selected_event
    let scope = ProfileEventScope(uint(frame), uint(event))
    let gpu = ProfileEventKind(uint(frame), uint(event)) == 1
    let start = ProfileEventStart(uint(frame), uint(event))
    let duration = ProfileEventDuration(uint(frame), uint(event))
    let frame_ms = max(ProfileFrameMs(uint(frame)), 0.001f)
    let share = duration / frame_ms * 100.0f

    var calls_here = 0
    for (e in range(0, count)) {
        if (ProfileEventScope(uint(frame), uint(e)) == scope) {
            calls_here += 1
        }
    }

    let total_calls = ProfileScopeTotalCalls(scope)
    let total_ms = ProfileScopeTotalMs(scope)
    let min_ms = ProfileScopeMinMs(scope)
    let max_ms = ProfileScopeMaxMs(scope)
    let avg_ms = float(total_calls) > 0.0f ? total_ms / float(total_calls) : 0.0f
    // uint64 interpolates as hex, so the call count goes through int64 to read as a number.
    let calls_shown = int64(total_calls)

    var title = ProfileScopeName(scope)
    if (gpu) {
        title = title + "   (GPU pass)"
    }
    text(PROFILER_SEL_TITLE, (text = title))
    let call_line = "frame {frame}   start {start:7.3f} ms   duration {duration:7.3f} ms   {share:5.1f}% of frame"
    text(PROFILER_SEL_CALL, (text = call_line))
    let frame_line = "this frame: {calls_here} calls"
    text(PROFILER_SEL_FRAME, (text = frame_line))
    let history_line = "history: {calls_shown} calls   {total_ms:9.2f} ms total   avg {avg_ms:6.3f}   min {min_ms:6.3f}   max {max_ms:6.3f} ms"
    text(PROFILER_SEL_HISTORY, (text = history_line))
    text(PROFILER_SEL_PATH, (text = EventPath(frame, event)))
}

/// One row per call of the pinned frame: children below their parents, indented by depth,
/// with a bar sized by the share of the frame the call took.
def private DrawFrameTree(frame : int) {
    EnsureTree(frame)
    let count = length(tree_rows)
    let frame_ms = max(ProfileFrameMs(uint(frame)), 0.001f)
    let header = "frame {frame}   {frame_ms:7.2f} ms   {count} calls   click a frame to pin and freeze"
    text(PROFILER_TREE_HEADER, (text = header))
    if (count <= 0) {
        text(PROFILER_TREE_EMPTY, (text = "no calls recorded in this frame"))
        return
    }

    let width = max(120.0f, GetContentRegionAvail().x)
    InvisibleButton("##profiler_tree", ImVec2(width, float(count) * TREE_ROW_HEIGHT + 2.0f))
    let rmin = GetItemRectMin()
    let rmax = GetItemRectMax()
    var dl = GetWindowDrawList()

    let hovered = IsItemHovered(ImGuiHoveredFlags.None)
    var hover_row = -1
    if (hovered) {
        hover_row = int((GetMousePos().y - rmin.y) / TREE_ROW_HEIGHT)
        if (hover_row < 0 || hover_row >= count) {
            hover_row = -1
        }
    }
    if (hover_row >= 0 && IsMouseClicked(ImGuiMouseButton.Left, false)) {
        // Selecting a call pins its frame and freezes the camera: otherwise the tree would
        // keep following the live edge and the selection would point into another frame.
        focus_frame = frame
        selected_event = int(tree_rows[hover_row])
        FreezeCamera()
    }

    let bar_width = max(1.0f, rmax.x - rmin.x - 24.0f)

    PushClipRect(rmin, rmax, true)
    for (i in range(0, count)) {
        let event = int(tree_rows[i])
        let scope = ProfileEventScope(uint(frame), uint(event))
        let depth = min(int(ProfileEventDepth(uint(frame), uint(event))), MAX_TREE_DEPTH)
        let start = ProfileEventStart(uint(frame), uint(event))
        let duration = ProfileEventDuration(uint(frame), uint(event))
        let y = rmin.y + float(i) * TREE_ROW_HEIGHT
        let x = rmin.x + 8.0f + float(depth) * TREE_INDENT

        if (i == hover_row) {
            *dl |> AddRectFilled(float2(rmin.x, y), float2(rmax.x, y + TREE_ROW_HEIGHT), COL_HOVER, 0.0f,
                                 ImDrawFlags.None)
        }
        if (event == selected_event) {
            *dl |> AddRectFilled(float2(rmin.x, y), float2(rmax.x, y + TREE_ROW_HEIGHT), COL_SELECTED, 0.0f,
                                 ImDrawFlags.None)
        }

        let bar = clamp(duration / frame_ms, 0.0f, 1.0f) * bar_width
        *dl |> AddRectFilled(float2(x, y + 4.0f), float2(x + max(bar, 1.0f), y + TREE_ROW_HEIGHT - 4.0f), COL_BAR,
                             1.0f, ImDrawFlags.None)
        *dl |> AddRectFilled(float2(x - 5.0f, y + 4.0f), float2(x - 2.0f, y + TREE_ROW_HEIGHT - 4.0f),
                             SeriesColor(scope), 0.0f, ImDrawFlags.None)
        *dl |> AddText(float2(x + 1.0f, y + 1.0f), COL_TEXT, ProfileScopeName(scope))

        // A GPU pass has its own clock base (first timestamp of the frame), so its start is
        // not comparable with the CPU rows: print the kind instead.
        var numbers = "{duration:7.2f} ms  {start:6.2f}"
        if (ProfileEventKind(uint(frame), uint(event)) == 1) {
            numbers = "{duration:7.2f} ms      gpu"
        }
        *dl |> AddText(float2(rmax.x - 8.0f - CalcTextSize(numbers).x, y + 1.0f), COL_TEXT_DIM, numbers)
    }
    PopClipRect()

    if (selected_event >= 0) {
        let path = EventPath(frame, selected_event)
        if (length(path) > 0) {
            let duration = ProfileEventDuration(uint(frame), uint(selected_event))
            let start = ProfileEventStart(uint(frame), uint(selected_event))
            text(PROFILER_EVENT_DETAILS, (text = "{path}   start {start:7.3f} ms   {duration:7.3f} ms"))
        }
    }
}

def private DrawScopeRow(scope : uint) {
    let (last, avg, peak) = ScopeStats(scope)
    let kind = ProfileScopeKind(scope)
    let plotted = IsPlotted(scope) ? "*" : " "

    var indent = ""
    for (_ in range(0, int(ProfileScopeDepth(scope)))) {
        indent += "  "
    }
    let tag = (kind == 1) ? "GPU " : "    "
    let title = "{plotted}{tag}{indent}{ProfileScopeName(scope)}"
    // uint32 interpolates as hex, so the call count goes through int to read as a number.
    let calls = int(ProfileScopeCalls(scope))
    // GPU rows have no call counter: one pass is one bar, so print the kind instead.
    var numbers = "{last:8.2f} ms  avg {avg:8.2f}  max {peak:8.2f}  x{calls}"
    if (kind == 1) {
        numbers = "{last:8.2f} ms  avg {avg:8.2f}  max {peak:8.2f}  gpu"
    }

    text(ROW_NAME[int(scope)], (text = title))
    same_line(ROW_SAME_LINE[int(scope)])
    text(ROW_STATS[int(scope)], (text = numbers))
    same_line(ROW_SAME_LINE_2[int(scope)])

    if (small_button(ROW_MENU[int(scope)], (text = "..."))) {
        OpenPopup("scope_menu_{scope}")
    }
    if (BeginPopup("scope_menu_{scope}")) {
        if (MenuItem(IsPlotted(scope) ? "Remove from plot" : "Add to plot", "", false, true)) {
            TogglePlot(scope)
        }
        if (MenuItem("Reset all history", "", false, true)) {
            ProfileReset()
            plot_series.clear()
            row_order_count = -1
        }
        EndPopup()
    }
}

[export, profile]
def DrawProfilerWindow() {
    EnsureInit()
    window(PROFILER_WIN, (text = "Profiler", closable = false, flags = ImGuiWindowFlags.None)) {
        if (!ProfileIsEnabled()) {
            text(PROFILER_DISABLED_TEXT, (text = "Profiling is compiled out: rebuild with TRY_PROFILE_ENABLED=1"))
        } else {
            let frames = int(ProfileFrameCount())
            if (checkbox(PROFILE_RECORD_TOGGLE, "Record")) {
                ProfileSetRecording(PROFILE_RECORD_TOGGLE.value)
            }
            same_line(PROFILER_TOOLBAR_1)
            if (button(PROFILE_RESET_BTN, (text = "Reset"))) {
                ProfileReset()
                InvalidateScopeNames()
                PROFILE_CAPTURE.value = false
                plot_series.clear()
                row_order_count = -1
                cam_first = 0
                cam_frac = 0.0f
                px_per_frame = DEFAULT_PX_PER_FRAME
                follow = true
                PROFILE_FOLLOW.value = true
                focus_frame = -1
                selected_event = -1
                export_status = ""
            }
            same_line(PROFILER_TOOLBAR_2)
            if (button(PROFILE_EXPORT_BTN, (text = "Export"))) {
                let path = ProfileExportToLogs()
                if (length(path) == 0) {
                    export_status = "export failed: nothing recorded or logs/ is not writable"
                } else {
                    export_status = "saved " + path
                }
            }
            same_line(PROFILER_TOOLBAR_7)
            if (checkbox(PROFILE_CAPTURE, "Capture")) {
                if (PROFILE_CAPTURE.value) {
                    // 64 MB of frames is a few minutes at 1000 fps; the status file next to it
                    // is tiny and keeps going even after the stream hits the cap.
                    ProfileStartCapture(uint64(64 * 1024 * 1024))
                } else {
                    ProfileStopCapture()
                }
            }
            same_line(PROFILER_TOOLBAR_8)
            if (button(PROFILE_LOAD_BTN, (text = "Load latest"))) {
                if (ProfileImportLatestLog()) {
                    InvalidateScopeNames()
                    PROFILE_CAPTURE.value = false
                    row_order_count = -1
                    cam_first = 0
                    cam_frac = 0.0f
                    px_per_frame = DEFAULT_PX_PER_FRAME
                    follow = true
                    PROFILE_FOLLOW.value = true
                    focus_frame = -1
                    selected_event = -1
                    export_status = "loaded newest capture from logs/"
                } else {
                    export_status = "no capture in logs/"
                }
            }
            same_line(PROFILER_TOOLBAR_3)
            if (checkbox(PROFILE_FOLLOW, "Follow live")) {
                follow = PROFILE_FOLLOW.value
                if (follow) {
                    // Re-attach at the current scale: only the right edge moves.
                    focus_frame = -1
                    selected_event = -1
                }
            }
            same_line(PROFILER_TOOLBAR_4)
            if (button(PROFILE_ZOOM_IN, (text = "+"))) {
                ZoomAt(1.5f, view_width * 0.5f, frames, view_width)
            }
            same_line(PROFILER_TOOLBAR_5)
            if (button(PROFILE_ZOOM_OUT, (text = "-"))) {
                ZoomAt(1.0f / 1.5f, view_width * 0.5f, frames, view_width)
            }
            same_line(PROFILER_TOOLBAR_6)
            if (checkbox(PROFILE_AUTOSCALE, "Auto scale")) {
                plot_auto_scale = PROFILE_AUTOSCALE.value
            }

            let last_ms = frames > 0 ? ProfileFrameMs(uint(frames - 1)) : 0.0f
            let fps = last_ms > 0.0f ? 1000.0f / last_ms : 0.0f
            let summary = "{frames} frames   last {last_ms:7.2f} ms   {fps:6.1f} fps"
            text(PROFILER_SUMMARY, (text = summary))
            if (ProfileIsCapturing()) {
                let megabytes = float(int64(ProfileCaptureBytes())) / (1024.0f * 1024.0f)
                var info = "capture {ProfileCapturedFrames()} frames, {megabytes:5.2f} MB -> {ProfileCapturePath()}"
                if (ProfileCaptureCapped()) {
                    info += "  (frame stream stopped at the cap, status keeps going)"
                }
                text(PROFILER_CAPTURE_INFO, (text = info))
            }
            if (length(export_status) > 0) {
                text(PROFILER_EXPORT_STATUS, (text = export_status))
            }

            EnsureLaneCounts()
            if (frames > 0) {
                // The camera settles here, so the timeline, the tree and the plot all read
                // the same window.
                let span = ViewSpan(frames, max(120.0f, GetContentRegionAvail().x))
                if (follow) {
                    FollowCamera(frames, span)
                } else {
                    ClampCamera(frames, span)
                }
                let focus = ResolveFocus(frames, span)
                separator(PROFILER_SEP_TIMELINE)
                DrawTimeline(focus)
                DrawSelectionDetails(focus)
                separator(PROFILER_SEP_TREE)
                DrawFrameTree(focus)
                if (!empty(plot_series)) {
                    separator(PROFILER_SEP_PLOT)
                    DrawPlot(frames, span)
                }
            }

            EnsureRowOrder()
            separator(PROFILER_SEP_CPU)
            if (empty(row_order)) {
                text(PROFILER_EMPTY_CPU, (text = "no scopes recorded yet"))
            } else {
                for (scope in row_order) {
                    DrawScopeRow(scope)
                }
            }
        }
    }
}
