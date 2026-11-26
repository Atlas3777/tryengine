options gen2
options relaxed_assign = false

module LiveEcs shared public

//! The DECS world survives a script reload without any game code.
//!
//! The world is the `decsState` heap: entities, archetypes and component bytes. It is not a
//! global this repository owns (`daslib/decs` declares it), so it cannot carry `@live`; the
//! pair of reload hooks here serializes it into the same live-host store that `@live` uses.
//! Everything script-side that the heap does not carry — the active scene and its resource
//! references, the editor snapshot, the clocks — is `@live` in its own module instead.
//!
//! After the heap is back the component registry has to be registered again (`decs::restart`
//! wiped it) and the raw `mesh`/`material` pointers have to be resolved from the authored
//! `model`: the archive stores strings and ids, not pointers into C++ resources.
//!
//! `Engine.das` requires this module once, so the editor gets the hooks and the game (which
//! never reloads) pays nothing but a compiled-and-never-called pair of functions.

require daslib/archive
require daslib/decs
require daslib/decs_boost
require tryengine/EcsMacro
require live_host
require tryengine/Model

let private kEcsKey : string = "__tryengine_live_ecs"

[before_reload]
def private SaveEcsHeap() {
    let data <- mem_archive_save(decsState)
    live_store_bytes(kEcsKey, data)
}

[after_reload]
def private RestoreEcsHeap() {
    var data : array<uint8>
    if (!live_load_bytes(kEcsKey, data) || length(data) == 0) {
        return
    }

    decs::restart()
    if (!mem_archive_load(data, decsState, canfail = true)) {
        print("[RELOAD] the DECS world could not be restored; the empty scene stays\n")
        EcsApplyRegistry()
        return
    }
    EcsApplyRegistry()
    // The archive zeroes raw pointers: without this every model stays invisible until its
    // Inspector call re-applies the pair.
    Model::ReapplyPointers()
}
