options gen2
options relaxed_assign = false

//! Probe for tryeditor/UndoRedo: runs the history against a DECS world, without the editor.
//!
//! The probe has entities but no models, so no resource is touched and the CLI can run it
//! (`tools/agent/undo-probe.sh`). What it covers is what a GUI cannot check cheaply: the
//! component snapshot round trip, add/remove component undo, deleting a subtree and getting
//! it back under the same parent and sibling slot with a *new* entity id, an older history
//! entry still reaching the re-spawned entity, copy/paste, and gesture coalescing.
//!
//! A module [init] may run before the ones of `Types` and `Serializable`, so the probe
//! declares the components and the writer it uses itself. The names and types match the real
//! declarations, so the later registration only overwrites the flags.

require strings
require math
require daslib/decs
require daslib/decs_boost
require tryengine/EcsMacro
require tryengine/TransformSystem
require tryengine/Types
require tryengine/Prefab
require tryengine/ConfigFormat
require tryengine/ConfigFormatBoost
require tryengine/Serializable
require tryeditor/UndoRedo

var g_failures : int = 0

def Check(cond : bool; what : string) {
    if (cond) {
        print("  ok   {what}\n")
    } else {
        print("  FAIL {what}\n")
        g_failures++
    }
}

def MakeRoot(tag : string; position : float3) : EntityId {
    let eid = create_entity() @(eid : EntityId; var cmp : ComponentMap) {
        cmp.tag := tag
        ApplyEntityTransform(cmp, position, float4(0.0f, 0.0f, 0.0f, 1.0f), float3(1.0f, 1.0f, 1.0f))
        cmp |> set(RELATIONSHIP_FIELD, Relationship())
    }
    return eid
}

def MakeChild(parent : EntityId; tag : string) : EntityId {
    let eid = create_entity() @(eid : EntityId; var cmp : ComponentMap) {
        cmp.tag := tag
        ApplyEntityTransform(cmp, float3(0.0f, 0.0f, 0.0f), float4(0.0f, 0.0f, 0.0f, 1.0f), float3(1.0f, 1.0f, 1.0f))
        cmp |> set(RELATIONSHIP_FIELD, Relationship())
    }
    return eid
}

def Link(parent : EntityId; child : EntityId) {
    var parent_rel = Relationship()
    query(parent) $(relationship : Relationship) {
        parent_rel = relationship
    }
    var last = INVALID_ENTITY_ID
    var current = parent_rel.first_child
    var steps = 0
    while (current != INVALID_ENTITY_ID && steps < 64) {
        last = current
        current = RelOf(current).next
        steps++
    }
    var child_rel = Relationship()
    query(child) $(relationship : Relationship) {
        child_rel = relationship
    }
    child_rel.parent = parent
    child_rel.prev = last
    update_entity(child) @(e, cmp) {
        cmp |> set(RELATIONSHIP_FIELD, child_rel)
    }
    if (last == INVALID_ENTITY_ID) {
        parent_rel.first_child = child
        update_entity(parent) @(e, cmp) {
            cmp |> set(RELATIONSHIP_FIELD, parent_rel)
        }
    } else {
        var last_rel = RelOf(last)
        last_rel.next = child
        update_entity(last) @(e, cmp) {
            cmp |> set(RELATIONSHIP_FIELD, last_rel)
        }
    }
    commit()
}

def TagOf(eid : EntityId) : string => get_component(eid, "tag", "")
def HpOf(eid : EntityId) : uint => get_component(eid, "HP", 0u)
def PosOf(eid : EntityId) : float3 => get_component(eid, TRANSFORM_FIELD, Transform()).position
def RelOf(eid : EntityId) : Relationship => get_component(eid, RELATIONSHIP_FIELD, Relationship())
def HasHp(eid : EntityId) : bool {
    var found = false
    query(eid) $(HP : uint) {
        found = true
    }
    return found
}

def SetTag(eid : EntityId; tag : string) {
    update_entity(eid) @(e, cmp) {
        cmp |> set("tag", tag)
    }
    commit()
}

// A module [init] may run before the ones of Types and Serializable, so the probe declares the
// components and writers it touches itself. Same names, same types: the real registration
// later only overwrites the flags.
[init]
def RegisterProbeComponents() {
    EcsRegister("transform", type<Transform>)
    EcsRegister("tag", type<string>)
    EcsRegister("HP", type<uint>)
    EcsRegister("relationship", type<Relationship>, transient = true)
    RegisterBuiltinWriters()
    if (!HasComponentWriter("Transform")) {
        g_decs_writers["Transform"] = @@(ptr : void?) : StructField? {
            return <- WriteValue(type<Transform>, ptr)
        }
        g_decs_adders["Transform"] = @@(var cmp : decs::ComponentMap; component_name : string;
                                        cf : StructField? const) : void {
            var value <- from_CF(cf, default<Transform>)
            decs::set(cmp, component_name, value)
        }
    }
}

[export]
def main() : int {
    print("-- undo/redo probe --\n")
    UndoRedo::ClearHistory()

    // --- 1. component edit: tag ---
    let root = MakeRoot("Root", float3(1.0f, 2.0f, 3.0f))
    commit()
    var before <- UndoRedo::CaptureEntity(root)
    SetTag(root, "Edited")
    var after <- UndoRedo::CaptureEntity(root)
    Check(UndoRedo::PushEdit(before, after, false), "component edit recorded")
    Check(UndoRedo::CanUndo(), "can undo after edit")
    UndoRedo::Undo()
    Check(TagOf(root) == "Root", "undo restored the tag, got '{TagOf(root)}'")
    UndoRedo::Redo()
    Check(TagOf(root) == "Edited", "redo re-applied the tag, got '{TagOf(root)}'")
    UndoRedo::Undo()
    Check(TagOf(root) == "Root", "second undo restored the tag")
    // leave the tag as authored
    SetTag(root, "Root")

    // --- 2. add component then undo it ---
    before <- UndoRedo::CaptureEntity(root)
    update_entity(root) @(e, cmp) {
        cmp |> set("HP", 42u)
    }
    commit()
    after <- UndoRedo::CaptureEntity(root)
    Check(UndoRedo::PushEdit(before, after, false), "add component recorded")
    Check(HasHp(root), "HP exists after the add")
    UndoRedo::Undo()
    Check(!HasHp(root), "undo removed the added component")
    UndoRedo::Redo()
    Check(HasHp(root) && HpOf(root) == 42u, "redo added the component back")

    // --- 3. inspect a child and delete the subtree, then undo ---
    let child = MakeChild(root, "Child")
    commit()
    Link(root, child)
    UndoRedo::ClearHistory()

    Check(UndoRedo::DeleteSubtree(root), "subtree delete recorded and applied")
    Check(!decs::is_alive(root) && !decs::is_alive(child), "both entities are gone")

    let restored = UndoRedo::Undo()
    Check(restored != INVALID_ENTITY_ID && decs::is_alive(restored), "undo brought the root back")
    Check(TagOf(restored) == "Root", "restored root keeps its tag, got '{TagOf(restored)}'")
    Check(HasHp(restored) && HpOf(restored) == 42u, "restored root keeps its components")
    Check(abs(PosOf(restored).x - 1.0f) < 1e-4f && abs(PosOf(restored).z - 3.0f) < 1e-4f,
          "restored root keeps its position, got {PosOf(restored)}")
    let child_rel = RelOf(RelOf(restored).first_child)
    Check(child_rel.parent == restored, "restored child points at the new root")
    Check(RelOf(restored).first_child.id != child.id, "child got a fresh id ({child.id:d} -> {RelOf(restored).first_child.id:d})")

    // redo deletes it again
    UndoRedo::Redo()
    Check(!decs::is_alive(restored), "redo deleted the restored subtree again")
    let restored_again = UndoRedo::Undo()
    Check(decs::is_alive(restored_again), "second undo brought it back again")

    // --- 4. an older edit still lands on the re-spawned entity (id remap) ---
    UndoRedo::ClearHistory()
    before <- UndoRedo::CaptureEntity(restored_again)
    SetTag(restored_again, "Renamed")
    after <- UndoRedo::CaptureEntity(restored_again)
    UndoRedo::PushEdit(before, after, false)
    Check(UndoRedo::DeleteSubtree(restored_again), "delete after the edit")
    let back = UndoRedo::Undo()  // undo the delete
    Check(decs::is_alive(back) && TagOf(back) == "Renamed", "delete-undo keeps the later edit")
    UndoRedo::Undo()             // undo the edit: must find the entity through the alias
    Check(TagOf(back) == "Root", "undo of an older edit reaches the re-spawned entity, got '{TagOf(back)}'")

    // --- 5. copy and paste ---
    UndoRedo::ClearHistory()
    Check(UndoRedo::CopyEntity(back), "copy recorded")
    let pasted = UndoRedo::PasteClipboard()
    Check(pasted != INVALID_ENTITY_ID && decs::is_alive(pasted), "paste created an entity")
    Check(pasted.id != back.id, "paste is a different entity")
    Check(TagOf(pasted) == "Root", "paste copied the tag, got '{TagOf(pasted)}'")
    Check(abs(PosOf(pasted).x - PosOf(back).x) > 1e-3f, "paste is offset from the source")
    let pasted2 = UndoRedo::PasteClipboard()
    Check(decs::is_alive(pasted2) && pasted2.id != pasted.id, "second paste is another entity")
    UndoRedo::Undo()
    Check(!decs::is_alive(pasted2), "undo removed the second paste")
    UndoRedo::Undo()
    Check(!decs::is_alive(pasted), "undo removed the first paste")
    Check(decs::is_alive(back), "the source entity is untouched")

    // --- 6. deleting a middle child keeps the sibling list, undo puts it back in place ---
    UndoRedo::ClearHistory()
    let parent3 = MakeRoot("Parent3", float3(0.0f, 0.0f, 0.0f))
    let a3 = MakeChild(parent3, "A")
    let b3 = MakeChild(parent3, "B")
    let c3 = MakeChild(parent3, "C")
    commit()
    Link(parent3, a3)
    Link(parent3, b3)
    Link(parent3, c3)
    Check(UndoRedo::DeleteSubtree(b3), "delete the middle child")
    Check(RelOf(parent3).first_child == a3 && RelOf(a3).next == c3 && RelOf(c3).prev == a3,
          "the sibling list closed the gap")
    let b3_back = UndoRedo::Undo()
    Check(decs::is_alive(b3_back) && RelOf(b3_back).parent == parent3, "the undo re-parented the child")
    Check(RelOf(a3).next == b3_back && RelOf(b3_back).prev == a3 && RelOf(b3_back).next == c3 &&
          RelOf(c3).prev == b3_back, "the restored child is back in its sibling slot")

    // --- 7. a widget drag is one step, not one per frame ---
    UndoRedo::ClearHistory()
    let drag = MakeRoot("Drag", float3(0.0f, 0.0f, 0.0f))
    commit()
    var stepA <- UndoRedo::CaptureEntity(drag)
    SetTag(drag, "Drag1")
    var stepB <- UndoRedo::CaptureEntity(drag)
    UndoRedo::PushEdit(stepA, stepB, true)
    stepA <- UndoRedo::CaptureEntity(drag)
    SetTag(drag, "Drag2")
    stepB <- UndoRedo::CaptureEntity(drag)
    UndoRedo::PushEdit(stepA, stepB, true)
    Check(UndoRedo::HistorySize() == 1, "a coalesced drag is one history entry, got {UndoRedo::HistorySize()}")
    UndoRedo::Undo()
    Check(TagOf(drag) == "Drag", "undo of a drag goes back to its start, got '{TagOf(drag)}'")

    print("-- failures: {g_failures} --\n")
    return g_failures == 0 ? 0 : 1
}
