options gen2
options relaxed_assign = false

require daslib/decs

//! Positional component insertion: is it safe to build an entity from pre-resolved
//! ComponentValues inserted in name order? DECS zips the archetype's component array with the
//! map by position, and it inserts `eid` before the entity block runs, so a compiled prefab has
//! to insert around it instead of appending. Mixed case matters: "Transform_position" sorts
//! before "eid", "tag" after it.
var g_values : decs::ComponentMap
var g_eid : EntityId
var g_eid2 : EntityId
var g_first : decs::ComponentMap
var g_second : decs::ComponentMap
var g_failures : int

def Check(ok : bool; what : string) {
    if (!ok) {
        g_failures++
        print("FAIL: {what}\n")
    }
}

def InsertComponent(var cmp : decs::ComponentMap; cv : decs::ComponentValue) {
    var at = 0
    while (at < length(cmp) && cmp[at].name < cv.name) {
        at++
    }
    cmp |> push(cv, at)
}

def MakeValues(tag : string; hp : int) : decs::ComponentMap {
    var cmp : decs::ComponentMap
    cmp |> set("Transform_position", float3(1.0f, 2.0f, 3.0f))
    cmp |> set("hp", hp)
    cmp |> set("tag", tag)
    return <- cmp
}

def SpawnPair() {
    for (cv in MakeValues("first", 11)) {
        InsertComponent(g_first, cv)
    }
    for (cv in MakeValues("second", 22)) {
        InsertComponent(g_second, cv)
    }

    create_entity() @(e, cmp) {
        for (cv in g_first) {
            InsertComponent(cmp, cv)
        }
        g_eid = e
    }
    create_entity() @(e, cmp) {
        for (cv in g_second) {
            InsertComponent(cmp, cv)
        }
        g_eid2 = e
    }
    commit()
}

[export]
def main() : int {
    SpawnPair()

    let tag1 = decs::get_component(g_eid, "tag", "")
    let tag2 = decs::get_component(g_eid2, "tag", "")
    let hp1 = decs::get_component(g_eid, "hp", 0)
    let hp2 = decs::get_component(g_eid2, "hp", 0)
    let pos1 = decs::get_component(g_eid, "Transform_position", float3())
    let pos2 = decs::get_component(g_eid2, "Transform_position", float3())

    print("eid1 tag='{tag1}' hp={hp1} pos={pos1}\n")
    print("eid2 tag='{tag2}' hp={hp2} pos={pos2}\n")

    var ok = tag1 == "first" && tag2 == "second" && hp1 == 11 && hp2 == 22
    ok = ok && pos1 == float3(1.0f, 2.0f, 3.0f) && pos2 == float3(1.0f, 2.0f, 3.0f)
    Check(ok, "positional zip kept every value on its entity")
    print("-- {g_failures} check(s) failed --\n")
    return g_failures == 0 ? 0 : 1
}
