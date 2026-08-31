options gen2

module Inspector shared

require imgui
require imgui/imgui_widgets_builtin
require imgui/imgui_containers_builtin
require imgui/imgui_layout_builtin
require imgui/imgui_id_builtin
require imgui/imgui_boost_runtime
require imgui/imgui_boost_v2
require tryengine/Types
require daslib/decs
require daslib/decs_boost
require strings
require math
require tryeditor/Hierarchy   // selected_eid

// ---------------------------------------------------------------------
// Поиск сущности в decsState
// ---------------------------------------------------------------------
def get_entity_location(eid : EntityId; var out_arch_idx : int&; var out_index : int64&) : bool {
    if (uint(eid.id) >= uint(length(decsState.entityLookup))) {
        return false
    }
    let rec = decsState.entityLookup[int(eid.id)]
    if (rec.generation != eid.generation) {
        return false
    }
    if (!key_exists(decsState.archetypeLookup, rec.archetype)) {
        return false
    }
    let afound = decsState.archetypeLookup[rec.archetype]
    if (afound == 0) {
        return false
    }
    out_arch_idx = afound - 1
    out_index = rec.index
    return true
}

// ---------------------------------------------------------------------
// Состояние для строк, чекбоксов и фильтра
// ---------------------------------------------------------------------
var W_BOOL   : table<string; ToggleState>
var W_STRING : table<string; InputTextState>
var W_FILTER : TextFilterState

// Переменная для отслеживания смены фокуса
var last_selected_eid : EntityId = INVALID_ENTITY_ID

def clear_inspector_states() {
    clear(W_BOOL)
    clear(W_STRING)
}

// ---------------------------------------------------------------------
// Отрисовка дефолтных типов через Drag Edit
// ---------------------------------------------------------------------
def draw_component_value(comp : Component; index : int64; display_name : string) {
    let offset = int(index) * comp.stride
    let i = "{selected_eid.id}_{comp.name}"

    unsafe {
        let ptr = addr(comp.data[offset])

        // Скаляры (float, double, int, uint, int64, uint64)
        if (comp.info.basicType == Type.tFloat) {
            edit_drag_scalar(reinterpret<float?>(ptr), (id = "drag_float", text = display_name, speed = 0.01f))

        } elif (comp.info.basicType == Type.tInt) {
            edit_drag_scalar(reinterpret<int?>(ptr), (id = "drag_int", text = display_name, speed = 1.0f))

        } elif (comp.info.basicType == Type.tUInt) {
            edit_drag_scalar(reinterpret<uint?>(ptr), (id = "drag_uint", text = display_name, speed = 1.0f))

        } elif (comp.info.basicType == Type.tInt64) {
            edit_drag_scalar(reinterpret<int64?>(ptr), (id = "drag_int64", text = display_name, speed = 1.0f))

        } elif (comp.info.basicType == Type.tUInt64) {
            edit_drag_scalar(reinterpret<uint64?>(ptr), (id = "drag_uint64", text = display_name, speed = 1.0f))

        // Векторы Float
        } elif (comp.info.basicType == Type.tFloat2) {
            edit_drag_float2(reinterpret<float2?>(ptr), (id = "drag_float2", text = display_name, speed = 0.01f))

        } elif (comp.info.basicType == Type.tFloat3) {
            edit_drag_float3(reinterpret<float3?>(ptr), (id = "drag_float3", text = display_name, speed = 0.01f))

        } elif (comp.info.basicType == Type.tFloat4) {
            edit_drag_float4(reinterpret<float4?>(ptr), (id = "drag_float4", text = display_name, speed = 0.01f))

        // Векторы Int
        } elif (comp.info.basicType == Type.tInt2) {
            edit_drag_int2(reinterpret<int2?>(ptr), (id = "drag_int2", text = display_name, speed = 1.0f))

        } elif (comp.info.basicType == Type.tInt3) {
            edit_drag_int3(reinterpret<int3?>(ptr), (id = "drag_int3", text = display_name, speed = 1.0f))

        } elif (comp.info.basicType == Type.tInt4) {
            edit_drag_int4(reinterpret<int4?>(ptr), (id = "drag_int4", text = display_name, speed = 1.0f))

        // Чекбокс
        } elif (comp.info.basicType == Type.tBool) {
            W_BOOL[i].value = *reinterpret<bool?>(ptr)
            if (checkbox(W_BOOL[i], (text = display_name))) {
                *reinterpret<bool?>(ptr) = W_BOOL[i].value
            }

        // Текст
        } elif (comp.info.basicType == Type.tString) {
            let current_val = *reinterpret<string?>(ptr)
            if (!key_exists(W_STRING, i) || W_STRING[i].value != current_val) {
                init_input_text(W_STRING[i], current_val)
            }
            if (input_text(W_STRING[i], (text = display_name, flags = ImGuiInputTextFlags.None))) {
                *reinterpret<string?>(ptr) = W_STRING[i].value
            }

        } else {
            text("{display_name}: not found drawer for this type")
        }
    }
}

// Вспомогательная функция для безопасной инициализации InputTextState
def init_input_text(var state : InputTextState; val : string) {
    state.value = val
    let len = length(val)
    state.capacity = max(256, len + 1)
    resize(state.buffer, state.capacity)

    for (j in range(len)) {
        state.buffer[j] = uint8(character_at(val, j))
    }
    state.buffer[len] = 0u8
}

// ---------------------------------------------------------------------
// Оверрайды под конкретные компоненты
// ---------------------------------------------------------------------
typedef ComponentDrawFn = function<(eid : EntityId) : void>
var g_overrides : table<string; ComponentDrawFn>

def register_override(name : string; draw : ComponentDrawFn) {
    g_overrides[name] = draw
}

def draw_relationship_override(eid : EntityId) {
    query(eid) <| $(rel : Relationship) {
        text("parent:      [{uint(rel.parent.id)}]")
        if (rel.parent != INVALID_ENTITY_ID) {
            same_line()
            if (button(BT_GO, (text = "Go"))) {
                selected_eid = rel.parent
            }
        }
        text("first_child: [{uint(rel.first_child.id)}]")
        text("next:        [{uint(rel.next.id)}]")
    }
}

[init]
def register_default_overrides() {
    register_override("Relationship", @@draw_relationship_override)
}

// ---------------------------------------------------------------------
// Окно инспектора
// ---------------------------------------------------------------------
def private split_component_name(name : string; var out_prefix : string&; var out_field : string&) : bool {
    let idx = find(name, "_")
    if (idx != -1) {
        out_prefix = slice(name, 0, idx)
        out_field = slice(name, idx + 1)
        return true
    }
    return false
}

def DrawInspector() {
    if (selected_eid != last_selected_eid) {
        clear_inspector_states()
        last_selected_eid = selected_eid
    }

    window(INSPECTOR_WIN, (text = "InspectorDas", closable = false, flags = ImGuiWindowFlags.None)) {
        if (selected_eid == INVALID_ENTITY_ID) {
            text("pick entity")
            return
        }

        var arch_idx = -1
        var rec_index : int64 = 0
        if (!get_entity_location(selected_eid, arch_idx, rec_index)) {
            text("dead entity")
            return
        }

        text("Entity [{uint(selected_eid.id)}]")

        // ---- ПОИСК ПО КОМПОНЕНТАМ ----
        text("Search:")
        same_line()
        text_filter(W_FILTER)

        separator()

        var processed_groups : table<string; bool>

        for (comp in decsState.allArchetypes[arch_idx].components) {
            if (comp.name == "eid") {
                continue
            }

            if (!passes_filter(W_FILTER, comp.name)) {
                continue
            }

            var prefix = ""
            var field_name = ""

            if (split_component_name(comp.name, prefix, field_name)) {
                if (key_exists(processed_groups, prefix)) {
                    continue
                }
                processed_groups[prefix] = true

                with_id("{prefix}{selected_eid.id}") {
                    collapsing_header(BASIC_CHR, (text = prefix, closable = false, flags = ImGuiTreeNodeFlags.None)) {
                        // Проверяем оверрайд для префикса всей структуры
                        if (key_exists(g_overrides, prefix)) {
                            g_overrides[prefix] |> invoke(selected_eid)
                        } else {
                            for (sub_comp in decsState.allArchetypes[arch_idx].components) {
                                var sub_prefix = ""
                                var sub_field = ""
                                if (split_component_name(sub_comp.name, sub_prefix, sub_field) && sub_prefix == prefix) {
                                    with_id(sub_field) {
                                        draw_component_value(sub_comp, rec_index, sub_field)
                                    }
                                }
                            }
                        }
                    }
                }
            } else {
                with_id("{comp.name}{selected_eid.id}") {
                    collapsing_header(BASIC_CHR, (text = comp.name, closable = false, flags = ImGuiTreeNodeFlags.None)) {
                        if (key_exists(g_overrides, comp.name)) {
                            g_overrides[comp.name] |> invoke(selected_eid)
                        } else {
                            draw_component_value(comp, rec_index, comp.name)
                        }
                    }
                }
            }
        }
    }
}