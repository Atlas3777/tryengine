options gen2
options relaxed_assign = false

//! Scene lifecycle: `LoadEmptyScene` fires the loaded event once per call, with the empty flag
//! set, and a listener registered before the load sees it. The editor uses the same event to
//! spawn its camera after every load.

require tryengine/Scene
require strings

var g_failures : int
var g_count : int
var g_name : string
var g_path : string
var g_empty : bool

def Check(ok : bool; what : string) {
    if (!ok) {
        g_failures++
        print("FAIL: {what}\n")
    }
}

def OnLoaded(name : string; path : string; empty : bool) {
    g_count++
    g_name = name
    g_path = path
    g_empty = empty
}

[export]
def main() : int {
    RegisterSceneLoadedListener(@@OnLoaded)

    LoadEmptyScene()
    Check(g_count == 1, "loading an empty scene fires the event once")
    Check(g_empty, "the event says the scene is empty")
    Check(empty(g_name) && empty(g_path), "an empty scene has no name and no path")

    LoadEmptyScene()
    Check(g_count == 2, "a second load fires the event again")

    print("-- {g_failures} check(s) failed --\n")
    return g_failures == 0 ? 0 : 1
}
