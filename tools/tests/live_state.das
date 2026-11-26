options gen2
options relaxed_assign = false

//! `@live` state is written by the old script generation and read by the new one. A single
//! process cannot swap a context by itself, so the test checks the two halves the engine
//! owns: the native store round-trips bytes and reports "not a reload" on a cold run, and the
//! infer pass generated both reload hooks for a module that declares `@live` globals.
//!
//! The full save/restore cycle across a real context swap is checked by
//! `tools/agent/live-probe.sh`: it runs a script under daslang-live, edits it, and requires
//! the `@live` value to survive the reload.

require daslib/rtti
require live_host
require live/live_vars
require strings

var @live probe_value = 42
var @live probe_name = "cold"

var g_failures : int = 0

def Check(ok : bool; what : string) {
    if (!ok) {
        g_failures++
        print("FAIL: {what}\n")
    }
}

[export]
def main() : int {
    // The store is keyed: the hook of each @live variable writes its own entry.
    var data : array<uint8>
    data |> push(uint8(1))
    data |> push(uint8(2))
    data |> push(uint8(3))
    live_store_bytes("live_state_test", data)

    var loaded : array<uint8>
    Check(live_load_bytes("live_state_test", loaded), "a stored key is found")
    Check(length(loaded) == 3, "stored bytes keep their length")
    Check(length(loaded) == 3 && loaded[0] == uint8(1) && loaded[2] == uint8(3), "stored bytes round-trip")

    var missing : array<uint8>
    Check(!live_load_bytes("live_state_test_missing", missing), "an unknown key is not found")

    Check(!is_reload(), "a cold load does not report a reload")

    var before_hook = false
    var after_hook = false
    context_for_each_function() $(info) {
        if (info.name == "__before_reload_live_vars") {
            before_hook = true
        }
        if (info.name == "__after_reload_live_vars") {
            after_hook = true
        }
    }
    Check(before_hook, "@live generated the before_reload hook")
    Check(after_hook, "@live generated the after_reload hook")

    if (g_failures > 0) {
        return 1
    }
    print("live state: ok\n")
    return 0
}
