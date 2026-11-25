options gen2
options relaxed_assign = false
options _allow_imgui_legacy = true

module LogWindow shared public

//! Log panel: the engine's in-memory ring (engine/core/LogHistory.hpp), written by C++ and
//! by scripts through the same Logger.
//!
//! The filter is applied in C++ (Log::SetFilter takes level and category bit masks), so a
//! full ring costs the panel the same as a short one: only the matching rows are walked,
//! and only the rows on screen are read from the ring. Clear empties the ring; the file on
//! disk is the run's history and stays where it is.

require tryengine/Log
require tryengine/Profiling
require strings
require imgui
require imgui/imgui_widgets_builtin
require imgui/imgui_containers_builtin
require imgui/imgui_layout_builtin
require imgui/imgui_boost_v2

var private initialized : bool = false
var private levels : array<LogSeverity>
var private categories : array<LogCategory>
var private level_rows : table<int; ToggleState>
var private category_rows : table<int; ToggleState>
// Filter last pushed to the ring: SetFilter is a no-op when the masks repeat, but the
// comparison here keeps the call out of the per-frame path entirely.
var private applied_levels : uint = 0xFFFFFFFF
var private applied_categories : uint = 0xFFFFFFFF
// Newest visible record seen last frame; Follow scrolls only when it advances, so a full
// ring (where the count no longer grows) still tails.
var private last_newest : uint64 = 0

let private COL_TRACE = float4(0.55f, 0.55f, 0.60f, 1.0f)
let private COL_INFO = float4(0.86f, 0.88f, 0.92f, 1.0f)
let private COL_WARN = float4(0.98f, 0.80f, 0.30f, 1.0f)
let private COL_ERROR = float4(0.98f, 0.45f, 0.35f, 1.0f)
let private COL_CRITICAL = float4(1.0f, 0.30f, 0.30f, 1.0f)

def private EnsureInit() {
    if (initialized) {
        return
    }
    initialized = true
    levels <- [LogSeverity.Trace, LogSeverity.Info, LogSeverity.Warning, LogSeverity.Error, LogSeverity.Critical]
    categories <- [
        LogCategory.General, LogCategory.Core, LogCategory.Graphics, LogCategory.Physics,
        LogCategory.Audio, LogCategory.Network, LogCategory.Gameplay, LogCategory.Assert,
        LogCategory.Importer, LogCategory.Script, LogCategory.FileSystem
    ]
    for (i in range(length(levels))) {
        var row : ToggleState
        row.value = true
        level_rows[i] = row
    }
    for (i in range(length(categories))) {
        var row : ToggleState
        row.value = true
        category_rows[i] = row
    }
}

def private LevelMask() : uint {
    var mask = uint(0)
    for (i in range(length(levels))) {
        if (level_rows[i].value) {
            mask = mask | uint(1 << i)
        }
    }
    return mask
}

def private CategoryMask() : uint {
    var mask = uint(0)
    for (i in range(length(categories))) {
        if (category_rows[i].value) {
            mask = mask | uint(1 << i)
        }
    }
    return mask
}

def private SetAllCategories(enabled : bool) {
    for (i in range(length(categories))) {
        category_rows[i].value = enabled
    }
}

def private LevelColor(level : LogSeverity) : float4 {
    if (level == LogSeverity.Trace) {
        return COL_TRACE
    }
    if (level == LogSeverity.Warning) {
        return COL_WARN
    }
    if (level == LogSeverity.Error) {
        return COL_ERROR
    }
    if (level == LogSeverity.Critical) {
        return COL_CRITICAL
    }
    return COL_INFO
}

def private DrawRow(index : int) {
    // A stale index (the ring overwrote the record between ViewCount and now) reads as an
    // empty message; the row is skipped for this one frame.
    var message = Log::ViewMessage(index)
    if (empty(message)) {
        return
    }
    // The ring may carry multi-line text (compiler diagnostics, panic reports). The panel
    // draws one record per line and the clipper assumes a uniform row height; a newline
    // inside a message would break both the layout and the scrollbar range. Flatten it.
    message = replace(message, "\r\n", " ")
    message = replace(message, "\n", " ")
    message = replace(message, "\r", " ")
    let level = Log::ViewLevel(index)
    let category = Log::ViewCategory(index)
    var line = "[{Log::ViewTime(index)}] {Log::LevelName(level)} {Log::CategoryName(category)}  {message}"
    if (level == LogSeverity.Warning || level == LogSeverity.Error || level == LogSeverity.Critical) {
        let file = Log::ViewFile(index)
        if (!empty(file)) {
            line += "  ({file}:{Log::ViewLine(index)})"
        }
    }
    // TextUnformatted, not TextColored: a message is user data and may contain '%'.
    PushStyleColor(ImGuiCol.Text, LevelColor(level))
    TextUnformatted(line)
    PopStyleColor(1)
}

[profile]
def public DrawLogWindow() {
    EnsureInit()
    SetNextWindowSize(ImVec2(760.0f, 460.0f), ImGuiCond.FirstUseEver)
    window(LOG_WINDOW, (text = "Log", closable = false, flags = ImGuiWindowFlags.None)) {
        // Level filter: one checkbox per severity, all on by default.
        for (i in range(length(levels))) {
            if (i > 0) {
                SameLine(0.0f, -1.0f)
            }
            checkbox(level_rows[i], (text = Log::LevelName(levels[i])))
        }
        SameLine(0.0f, -1.0f)
        if (button(LOG_CATEGORY_BTN, (text = "Categories"))) {
            open_popup(LOG_CATEGORY_POPUP)
        }
        SameLine(0.0f, -1.0f)
        if (button(LOG_CLEAR_BTN, (text = "Clear"))) {
            Log::Clear()
        }
        SameLine(0.0f, -1.0f)
        checkbox(LOG_FOLLOW, (text = "Follow", init = true))

        // Category filter: the reason a line was logged. A popup, because eleven
        // checkboxes do not fit on the toolbar.
        popup(LOG_CATEGORY_POPUP, (text = "Categories", flags = ImGuiWindowFlags.None)) {
            if (button(LOG_CAT_ALL, (text = "All"))) {
                SetAllCategories(true)
            }
            SameLine(0.0f, -1.0f)
            if (button(LOG_CAT_NONE, (text = "None"))) {
                SetAllCategories(false)
            }
            separator(LOG_CAT_SEP)
            for (i in range(length(categories))) {
                checkbox(category_rows[i], (text = Log::CategoryName(categories[i])))
            }
        }

        let level_mask = LevelMask()
        let category_mask = CategoryMask()
        let filter_changed = level_mask != applied_levels || category_mask != applied_categories
        if (filter_changed) {
            Log::SetFilter(level_mask, category_mask)
            applied_levels = level_mask
            applied_categories = category_mask
            // The visible row set changed under the scroll position; make Follow re-seat
            // itself instead of comparing against the newest row of the previous view.
            last_newest = 0
        }

        let total = Log::Count()
        let shown = Log::ViewCount()
        let newest = Log::ViewNewestSequence()
        text(LOG_SUMMARY, (text = "{shown} shown / {total} in ring"))
        let path = Log::FilePath()
        text_wrapped(LOG_FILE_PATH, (text = empty(path) ? "file: off" : "file: {path}"))

        // Horizontal scrollbar, no wrapping: a long message stays one click away from its
        // start, and the row layout stays one line per record.
        child(LOG_SCROLL, (text = "log rows", size = float2(0.0f, 0.0f),
              child_flags = ImGuiChildFlags.None,
              window_flags = ImGuiWindowFlags.HorizontalScrollbar)) {
            // The filter changed the row count under the old scroll offset. Reset it before
            // the clipper runs; otherwise the clipper computes its range from a position
            // that no longer exists, and the child keeps a phantom scrollbar for a frame.
            if (filter_changed) {
                SetScrollY(0.0f)
            }
            list_clipper(shown) $(start, end_) {
                for (i in range(start, end_)) {
                    DrawRow(i)
                }
            }
            if (LOG_FOLLOW.value && shown > 0 && newest != last_newest) {
                // Only tail when the user is already at the bottom. Otherwise a filter
                // change or a stream of new records yanks them away from what they read.
                let at_bottom = LOG_SCROLL.scroll.y + 1.0f >= LOG_SCROLL.scroll_max.y
                if (at_bottom || filter_changed) {
                    set_scroll_here_y(1.0f)
                }
            }
            last_newest = newest
        }
    }
}
