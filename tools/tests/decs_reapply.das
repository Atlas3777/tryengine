options gen2
options relaxed_assign = false

//! Editor heap restore: saving DECS to an archive zeroes raw pointers, `EcsApplyRegistry`
//! restores the component registry after `decs::restart`, and re-applying the model pointer
//! brings the component back. This covers the save/restore half of a hot reload.

require daslib/archive
require daslib/decs
require daslib/decs_boost
require tryengine/Types
require tryengine/EcsMacro
require tryResources

var g_failures : int
var dummy : int = 12345

def Check(ok : bool; what : string) {
    if (!ok) {
        g_failures++
        print("FAIL: {what}\n")
    }
}

[export]
def main() : int {
    EcsApplyRegistry()
    var ptr = unsafe(reinterpret<TryMesh?>(unsafe(addr(dummy))))
    var eid : EntityId
    unsafe {
        eid = create_entity() @(eid : EntityId; var cmp : ComponentMap) {
            cmp |> set("mesh", ptr)
            cmp |> set("HP", 77u)
        }
    }
    commit()

    var saved <- mem_archive_save(decsState)
    decs::restart()
    Check(mem_archive_load(saved, decsState), "the DECS archive loads")
    EcsApplyRegistry()

    var found = false
    var restored = INVALID_ENTITY_ID
    var mesh_null = false
    var hp = 0u
    query() $(eid : EntityId; mesh : TryMesh?; HP : uint) {
        found = true
        restored = eid
        mesh_null = mesh == null
        hp = HP
    }
    Check(found, "the entity survives the archive round trip")
    Check(mesh_null, "the archived model pointer comes back null")
    Check(hp == 77u, "scalar components keep their value")

    if (found) {
        update_entity(restored) @(eid : EntityId; var cmp : ComponentMap) {
            cmp |> set("mesh", ptr)
        }
        commit()
        var now_null = true
        query(restored) $(mesh : TryMesh?) {
            now_null = mesh == null
        }
        Check(!now_null, "re-applying the model pointer brings the component back")
    }

    print("-- {g_failures} check(s) failed --\n")
    return g_failures == 0 ? 0 : 1
}
