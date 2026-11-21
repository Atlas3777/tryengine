options gen2
options relaxed_assign = false

module Hierarchy shared

require imgui
require imgui/imgui_widgets_builtin
require imgui/imgui_containers_builtin
require imgui/imgui_layout_builtin
require imgui/imgui_id_builtin
require imgui/imgui_boost_runtime
require tryengine/Profiling
require tryengine/TransformSystem
require tryengine/Types
require daslib/decs
require daslib/decs_boost

var ENTITY_NODE : TreeNodeState

var selected_eid : EntityId = INVALID_ENTITY_ID

// Selection reveal: when selected_eid changes (viewport pick, prefab spawn, inspector),
// open the selection's ancestors and its own children once, then scroll it into view.
// The force is one frame only, so the user can collapse the node afterwards.
var private g_last_selected_eid : EntityId = INVALID_ENTITY_ID
var private g_selection_chain : array<EntityId>
var private g_force_open_selection : bool
var private g_scroll_to_selection : bool

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
        }
        separator()

        if (IsMouseClicked(ImGuiMouseButton.Left) && IsWindowHovered() && !IsAnyItemHovered()) {
            selected_eid = INVALID_ENTITY_ID
        }

        UpdateSelectionReveal()

        // Отрисовка корневых сущностей
        query() $(eid : EntityId; relationship : Relationship) {
            if (relationship.parent == INVALID_ENTITY_ID) {
                DrawEntityNode(eid)
            }
        }

        // One-shot: if the selection was not drawn (dead entity, collapsed window),
        // do not keep the force/scroll request alive for the next frame.
        g_force_open_selection = false
        g_scroll_to_selection = false
    }
}

def DrawEntityNode(eid : EntityId) {
    var labell = "Entity [{uint(eid.id)}]"
    query(eid) $(tag : string) {
        labell = tag
    }

    var has_children = false
    var first_child = INVALID_ENTITY_ID
    query(eid) $(relationship : Relationship) {
        first_child = relationship.first_child
        has_children = (first_child != INVALID_ENTITY_ID)
    }

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
                while (curr != INVALID_ENTITY_ID) {
                    DrawEntityNode(curr)

                    var next_sibling = INVALID_ENTITY_ID
                    query(curr) $(relationship : Relationship) {
                        next_sibling = relationship.next
                    }
                    curr = next_sibling
                }
            }
            tree_pop()
        }
    }
}
