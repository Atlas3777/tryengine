options gen2
options relaxed_assign = false

require daslib/decs


//! A compiled prefab stores ready ComponentValues and drops the parsed ConfigFormat tree.
//! `ComponentValue.data` is raw bytes, so a string baked into it must stay alive without its
//! author: the template owns every string it compiled.
//!
//! The author is a runtime-built string inside BuildPlan (a literal would live forever).
//! After it dies the heap is churned with same-size strings: if the block was freed, its
//! bytes get rewritten and the tag read back stops matching.
var g_values : decs::ComponentMap
var g_eid : EntityId
var g_failures : int

let kExpected = "probe_payload_77777777"

def Check(ok : bool; what : string) {
    if (!ok) {
        g_failures++
        print("FAIL: {what}\n")
    }
}

def BuildPlan() {
    var cmp : decs::ComponentMap
    // Built character by character so nothing outside `payload` owns the block.
    var payload = "probe_payload_"
    for (ch in range(8)) {
        payload += "7"
    }
    cmp |> set("tag", payload)
    cmp |> set("hp", 42)
    for (cv in cmp) {
        g_values |> push(cv)
    }
}

def ChurnHeap(count : int) {
    for (i in range(count)) {
        // Same byte length as the payload: only then does the allocator reuse the block.
        var junk = "ZZZZZZZZZZZZZZ"
        for (ch in range(8)) {
            junk += "Z"
        }
        if (i % 5000 == 0) {
            print("churn {i} '{junk}'\n")
        }
    }
}

def SpawnFromPlan() : EntityId {
    create_entity() @(e, cmp) {
        for (cv in g_values) {
            cmp |> push(cv)
        }
        g_eid = e
    }
    commit()
    return g_eid
}

[export]
def main() : int {
    BuildPlan()
    ChurnHeap(20000)
    let eid = SpawnFromPlan()
    let tag = decs::get_component(eid, "tag", "")
    print("tag = '{tag}'\n")
    print("hp  = {decs::get_component(eid, "hp", 0)}\n")
    Check(tag == kExpected, "ComponentValue keeps the string alive after its author dies")
    Check(decs::get_component(eid, "hp", 0) == 42, "a scalar component survives the heap churn")
    print("-- {g_failures} check(s) failed --\n")
    return g_failures == 0 ? 0 : 1
}
