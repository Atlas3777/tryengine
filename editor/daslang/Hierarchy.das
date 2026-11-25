options gen2
options relaxed_assign = false

module Hierarchy shared

require imgui
require imgui/imgui_widgets_builtin
require imgui/imgui_containers_builtin
require imgui/imgui_layout_builtin
require imgui/imgui_id_builtin
require imgui/imgui_boost_runtime
require tryengine/Live
require tryengine/Profiling
require tryengine/TransformSystem
require tryengine/Types
require daslib/decs
require daslib/decs_boost
require tryeditor/UndoRedo

var ENTITY_NODE : TreeNodeState

//! The selection is editor state, not world state: `@live` keeps it on the restored heap
//! (the id is stable there because the heap is the same snapshot).
var @live selected_eid : EntityId = INVALID_ENTITY_ID

// Selection reveal: when selected_eid changes (viewport pick, prefab spawn, inspector),
// open the selection's ancestors and its own children once, then scroll it into view.
// The force is one frame only, so the user can collapse the node afterwards.
var private g_last_selected_eid : EntityId = INVALID_ENTITY_ID
var private g_selection_chain : array<EntityId>
var private g_force_open_selection : bool
var private g_scroll_to_selection : bool

// The panel walks the authored sibling links, but the links are optional runtime data:
// a script can create an entity without a Relationship, and the component can be removed
// in the Inspector. Arrays indexed by eid.id prepare the forest in one scan, and the
// reachability pass remembers which entities the chains still own. Anything the chains
// lose is listed as a root, so no entity silently disappears from the panel.
var private g_first_child : array<EntityId>
var private g_next_sibling : array<EntityId>
var private g_has_relationship : array<bool>
var private g_reachable : array<bool>
var private g_drawn : array<bool>

def private IsLinkRoot(relationship : Relationship) => relationship.parent == INVALID_ENTITY_ID || !decs::is_alive(relationship.parent)

def private IsLinkedEntity(eid : EntityId) : bool {
    if (eid == INVALID_ENTITY_ID || !decs::is_alive(eid)) {
        return false
    }
    let index = int(eid.id)
    return index >= 0 && index < length(g_has_relationship) && g_has_relationship[index]
}

def private PrepareHierarchyIndex() {
    let slots = length(decsState.entityLookup)
    resize(g_first_child, slots)
    resize(g_next_sibling, slots)
    resize(g_has_relationship, slots)
    resize(g_reachable, slots)
    resize(g_drawn, slots)
    for (index in range(slots)) {
        g_first_child[index] = INVALID_ENTITY_ID
        g_next_sibling[index] = INVALID_ENTITY_ID
        g_has_relationship[index] = false
        g_reachable[index] = false
        g_drawn[index] = false
    }
    query() $(eid : EntityId; relationship : Relationship) {
        let index = int(eid.id)
        if (index < 0 || index >= slots) {
            return
        }
        g_first_child[index] = relationship.first_child
        g_next_sibling[index] = relationship.next
        g_has_relationship[index] = true
    }
}

def private MarkReachable(eid : EntityId; depth : int) {
    if (depth > 1024 || !decs::is_alive(eid)) {
        return
    }
    let slots = length(g_reachable)
    let index = int(eid.id)
    if (index < 0 || index >= slots || g_reachable[index]) {
        return
    }
    g_reachable[index] = true

    var child = g_first_child[index]
    var steps = 0
    while (child != INVALID_ENTITY_ID && steps < slots) {
        if (!IsLinkedEntity(child)) {
            break
        }
        MarkReachable(child, depth + 1)
        let child_index = int(child.id)
        if (child_index < 0 || child_index >= slots) {
            break
        }
        child = g_next_sibling[child_index]
        steps++
    }
}

def private IsInSelectionChain(eid : EntityId) : bool {
    for (chain_eid in g_selection_chain) {
        if (chain_eid == eid) {
            return true
        }
    }
    return false
}

def private UpdateSelectionReveal() {
    if (selected_eid == g_last_selected_eid) {
        return
    }
    g_last_selected_eid = selected_eid

    clear(g_selection_chain)
    g_force_open_selection = false
    g_scroll_to_selection = false

    if (selected_eid == INVALID_ENTITY_ID) {
        return
    }

    var current = selected_eid
    var depth = 0
    while (current != INVALID_ENTITY_ID && depth < 1024) {
        if (!decs::is_alive(current)) {
            break
        }
        g_selection_chain |> push(current)

        var parent = INVALID_ENTITY_ID
        query(current) $(relationship : Relationship) {
            parent = relationship.parent
        }
        current = parent
        depth++
    }

    g_force_open_selection = true
    g_scroll_to_selection = true
}

[export]
def SetSelectedEntity(eid : uint64){
    unsafe {
        selected_eid = reinterpret<EntityId>(eid)
    }
}

[profile]
def DrawHierarchy() {
    window(BASICS_WIN, (text = "HierarchyDas", closable = false, flags = ImGuiWindowFlags.None)) {
        // --- NEW: создание новой сущности ---
        if (button(BT_GO, (text = "+ New Entity"))) {
            let new_eid = create_entity() @(eid, cmp) {
                cmp.tag := "New Entity"
                ApplyEntityTransform(cmp, float3(0.0f, 0.0f, 0.0f), float4(0.0f, 0.0f, 0.0f, 1.0f), float3(1.0f, 1.0f, 1.0f))
                cmp |> set(RELATIONSHIP_FIELD, Relationship())
            }
            commit()                       // иначе query в этом кадре не увидит новую entity
            selected_eid = new_eid
            UndoRedo::RecordCreated(new_eid, "New entity")
        }
        separator()

        if (IsMouseClicked(ImGuiMouseButton.Left) && IsWindowHovered() && !IsAnyItemHovered()) {
            selected_eid = INVALID_ENTITY_ID
        }

        UpdateSelectionReveal()

        PrepareHierarchyIndex()

        // The authored forest: roots are parentless entities, or entities whose parent is
        // gone. Mark their subtrees first, so the fallback pass below does not duplicate
        // entities that are hidden only because their node is collapsed.
        query() $(eid : EntityId; relationship : Relationship) {
            if (IsLinkRoot(relationship)) {
                MarkReachable(eid, 0)
            }
        }
        query() $(eid : EntityId; relationship : Relationship) {
            if (IsLinkRoot(relationship)) {
                DrawEntityNode(eid, 0)
            }
        }

        // Entities without a Relationship, and anything a broken sibling chain cut off,
        // are still scene nodes: list them as roots instead of hiding them.
        query() $(eid : EntityId) {
            let index = int(eid.id)
            if (index >= 0 && index < length(g_reachable) && !g_reachable[index] && !g_drawn[index]) {
                DrawEntityNode(eid, 0)
            }
        }

        // One-shot: if the selection was not drawn (dead entity, collapsed window),
        // do not keep the force/scroll request alive for the next frame.
        g_force_open_selection = false
        g_scroll_to_selection = false
    }
}

def DrawEntityNode(eid : EntityId; depth : int) {
    if (depth > 1024 || !decs::is_alive(eid)) {
        return
    }
    let index = int(eid.id)
    if (index < 0 || index >= length(g_drawn) || g_drawn[index]) {
        return
    }
    g_drawn[index] = true

    var labell = "Entity [{eid.id}]"
    query(eid) $(tag : string) {
        labell = tag
    }

    var first_child = index < length(g_first_child) ? g_first_child[index] : INVALID_ENTITY_ID
    if (!IsLinkedEntity(first_child)) {
        first_child = INVALID_ENTITY_ID
    }
    let has_children = (first_child != INVALID_ENTITY_ID)

    let is_selected = (eid == selected_eid)

    var flags = ImGuiTreeNodeFlags.OpenOnArrow | ImGuiTreeNodeFlags.SpanAvailWidth
    if (!has_children) {
        flags |= ImGuiTreeNodeFlags.Leaf
    }
    if (is_selected) {
        flags |= ImGuiTreeNodeFlags.Selected
    }

    with_id("{eid.id}") {
        if (g_force_open_selection && IsInSelectionChain(eid)) {
            SetNextItemOpen(true, ImGuiCond.Always)
        }

        let opened = tree_node_ex(ENTITY_NODE, (text = labell, flags = flags))

        if (IsItemClicked()) {
            selected_eid = eid
        }

        if (g_scroll_to_selection && is_selected) {
            if (!IsItemVisible()) {
                SetScrollHereY(0.5f)
            }
            g_scroll_to_selection = false
        }

        if (opened) {
            if (has_children) {
                var curr = first_child
                var steps = 0
                while (curr != INVALID_ENTITY_ID && steps < length(g_next_sibling)) {
                    if (!IsLinkedEntity(curr)) {
                        break
                    }
                    DrawEntityNode(curr, depth + 1)

                    let curr_index = int(curr.id)
                    var next_sibling = INVALID_ENTITY_ID
                    if (curr_index >= 0 && curr_index < length(g_next_sibling)) {
                        next_sibling = g_next_sibling[curr_index]
                    }
                    curr = next_sibling
                    steps++
                }
            }
            tree_pop()
        }
    }
}
