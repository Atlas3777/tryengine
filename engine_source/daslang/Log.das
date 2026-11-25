options gen2
options relaxed_assign = false

module Log shared public

//! Engine log: the same sink chain as C++ `LogInfo`/`LogWarn`/`LogError`.
//!
//! A script line lands wherever a C++ line lands: the console, the in-memory ring the
//! editor's Log window draws, and `logs/engine-<date>.log`. The write side is the native
//! `tryLog` module: `LogInfo`/`LogWarn`/`LogError`/`LogCritical`/`LogTrace` mirror the C++
//! names and take an optional background category (the reason), defaulting to `Script`.
//! The source location is the script line itself, so a filtered Log window and the file
//! point at the same place for both languages.
//!
//!     LogInfo("loaded {count} entities")
//!     LogWarn(LogCategory.Physics, "body without collider")
//!
//! The read half feeds the Log window: SetFilter takes level and category bit masks
//! (bit N = the enum value N), View* walks the filtered ring from oldest to newest. An
//! index may go stale between a frame's ViewCount and the row read (the ring overwrote it);
//! the row then comes back empty and is not drawn.

require tryLog public

//! Drops every record from the in-memory ring. The file on disk is the run's history and
//! is not touched: the Clear button clears the window, not the log file.
def public Clear() {
    LogClear()
}

//! Live records in the ring, before filtering.
def public Count() : int {
    return int(LogRingCount())
}

def public SetFilter(level_mask : uint; category_mask : uint) {
    LogViewSetFilter(level_mask, category_mask)
}

//! Records that pass the current filter.
def public ViewCount() : int {
    return int(LogViewCount())
}

//! Sequence of the newest record that passes the filter; 0 when the view is empty.
def public ViewNewestSequence() : uint64 {
    return LogViewNewestSequence()
}

def public ViewTime(index : int) : string {
    return LogViewTime(uint(index))
}

def public ViewLevel(index : int) : LogSeverity {
    return LogViewLevel(uint(index))
}

def public ViewCategory(index : int) : LogCategory {
    return LogViewCategory(uint(index))
}

def public ViewLine(index : int) : uint {
    return LogViewLine(uint(index))
}

def public ViewMessage(index : int) : string {
    return LogViewMessage(uint(index))
}

def public ViewFile(index : int) : string {
    return LogViewFile(uint(index))
}

def public LevelCount() : int {
    return LogLevelCount()
}

def public CategoryCount() : int {
    return LogCategoryCount()
}

def public LevelName(level : LogSeverity) : string {
    return LogLevelName(level)
}

def public CategoryName(category : LogCategory) : string {
    return LogCategoryName(category)
}

//! `logs/engine-<date>.log` of this run, or an empty string when file logging is off.
def public FilePath() : string {
    return LogFilePath()
}
