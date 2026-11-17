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
require tryengine/Types
require daslib/decs
require daslib/decs_boost

var ENTITY_NODE : TreeNodeState

var selected_eid : EntityId = INVALID_ENTITY_ID

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
                apply_decs_template(cmp, Transform(
                    position = float3(0.0f, 0.0f, 0.0f),
                    rotation = float4(0.0f, 0.0f, 0.0f, 1.0f),
                    scale = float3(1.0f, 1.0f, 1.0f)
                ))
                apply_decs_template(cmp, Relationship())
            }
            commit()                       // иначе query в этом кадре не увидит новую entity
            selected_eid = new_eid
        }
        separator()

        if (IsMouseClicked(ImGuiMouseButton.Left) && IsWindowHovered() && !IsAnyItemHovered()) {
            selected_eid = INVALID_ENTITY_ID
        }

        // Отрисовка корневых сущностей
        query() $(eid : EntityId; rel : Relationship) {
            if (rel.parent == INVALID_ENTITY_ID) {
                DrawEntityNode(eid)
            }
        }
    }
}

def DrawEntityNode(eid : EntityId) {
    var labell = "Entity [{uint(eid.id)}]"
    query(eid) $(tag : string) {
        labell = tag
    }

    var has_children = false
    var first_child = INVALID_ENTITY_ID
    query(eid) $(rel : Relationship) {
        first_child = rel.first_child
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
        let opened = tree_node_ex(ENTITY_NODE, (text = labell, flags = flags))

        if (IsItemClicked()) {
            selected_eid = eid
        }

        if (opened) {
            if (has_children) {
                var curr = first_child
                while (curr != INVALID_ENTITY_ID) {
                    DrawEntityNode(curr)

                    var next_sibling = INVALID_ENTITY_ID
                    query(curr) $(rel : Relationship) {
                        next_sibling = rel.next
                    }
                    curr = next_sibling
                }
            }
            tree_pop()
        }
    }
}
