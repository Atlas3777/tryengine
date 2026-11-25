options gen2
options relaxed_assign = false

//! Engine log from a script: `tryengine/Log` writes through the same Logger as C++, and the
//! ring view the editor's Log window draws answers the filter by level and by category.
//!
//! The probe covers the C++ side and the file sink; this test covers what scripts can do:
//! write to the shared log, read it back through the filtered view, and clear the ring.

require tryengine/Log

var g_failures : int = 0

def Check(ok : bool; what : string) {
    if (!ok) {
        g_failures++
        print("FAIL: {what}\n")
    }
}

def private AllLevelsMask() : uint {
    var mask = uint(0)
    for (i in range(Log::LevelCount())) {
        mask = mask | uint(1 << i)
    }
    return mask
}

def private Bit(index : int) : uint {
    return uint(1 << index)
}

[export]
def main() : int {
    Log::Clear()

    LogInfo("first")
    LogWarn(LogCategory.Physics, "second")
    LogError("third")

    Check(Log::Count() == 3, "scripts reach the engine log")
    Check(Log::ViewCount() == 3, "all three are visible in the default view")
    Check(!empty(Log::ViewTime(0)), "a record carries its wall-clock time")
    Check(Log::CategoryCount() > int(LogCategory.FileSystem), "category list covers the C++ enum")
    Check(Log::LevelCount() == 5, "level list covers the C++ enum")

    // Importance: only errors.
    Log::SetFilter(Bit(int(LogSeverity.Error)) | Bit(int(LogSeverity.Critical)), 0xFFFFFFFF)
    Check(Log::ViewCount() == 1, "level filter keeps one record")
    Check(Log::ViewMessage(0) == "third", "the level-filtered record is the error")
    Check(Log::ViewLevel(0) == LogSeverity.Error, "the level comes back as the enum value")

    // Reason: only physics.
    Log::SetFilter(AllLevelsMask(), Bit(int(LogCategory.Physics)))
    Check(Log::ViewCount() == 1, "category filter keeps one record")
    Check(Log::ViewMessage(0) == "second", "the category-filtered record is the warning")
    Check(Log::ViewCategory(0) == LogCategory.Physics, "the category comes back as the enum value")

    // A one-argument call names the script category, so the default view says where it came from.
    Log::SetFilter(AllLevelsMask(), Bit(int(LogCategory.Script)))
    Check(Log::ViewCount() == 2, "default category is Script")

    // Follow hangs on the newest visible sequence: the ring keeps growing, the view may not.
    Log::SetFilter(AllLevelsMask(), 0xFFFFFFFF)
    let newest = Log::ViewNewestSequence()
    LogInfo("fourth")
    Check(Log::ViewNewestSequence() != newest, "a new record advances the newest sequence")
    Check(Log::Count() == 4, "the ring grew with it")

    Log::Clear()
    Check(Log::Count() == 0, "Clear empties the ring")
    Check(Log::ViewCount() == 0, "Clear empties the filtered view")

    print("-- {g_failures} check(s) failed --\n")
    return g_failures == 0 ? 0 : 1
}
