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
    let found = decsState.archetypeLookup[rec.archetype]
    if (found == 0) {
        return false
    }
    out_arch_idx = found - 1
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
// Отрисовка значения компонента. Если это структура — рекурсивно
// обходим её поля. Иначе — рисуем скаляр/вектор/чекбокс/строку.
// ---------------------------------------------------------------------
def draw_component_value(comp : Component; index : int64; display_name : string) {
    let offset = int(index) * comp.stride
    let id_key = "{selected_eid.id}_{comp.name}"

    unsafe {
        let ptr = addr(comp.data[offset])

        if (comp.info.basicType == Type.tStructure) {
            // info.basicType == tStructure — как в ComponentToCF
            let ti = comp.info.mkTypeInfo()
            draw_struct_fields(reinterpret<void?>(ptr), ti.firstType.structType, id_key)
        } else {
            draw_value_at_ptr(reinterpret<void?>(ptr), comp.info.basicType, display_name, id_key)
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

        text("Entity [{uint(selected_eid.id):d}]")
        let a = unsafe(reinterpret<uint64>(selected_eid));
        text("Entity2 [{a:d}]")

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

// ---------------------------------------------------------------------
// Отрисовка значения по "сырому" указателю (без привязки к Component).
// Аналог SerializeBase из сохранения.
// ---------------------------------------------------------------------
def private draw_value_at_ptr(ptr : void?; t : Type; display_name : string; id_key : string) {
    unsafe {
        // Скаляры (float, double, int, uint, int64, uint64)
        if (t == Type.tFloat) {
            edit_drag_scalar(reinterpret<float?>(ptr), (id = "drag_float", text = display_name, speed = 0.01f))

        } elif (t == Type.tInt) {
            edit_drag_scalar(reinterpret<int?>(ptr), (id = "drag_int", text = display_name, speed = 1.0f))

        } elif (t == Type.tUInt) {
            edit_drag_scalar(reinterpret<uint?>(ptr), (id = "drag_uint", text = display_name, speed = 1.0f))

        } elif (t == Type.tInt64) {
            edit_drag_scalar(reinterpret<int64?>(ptr), (id = "drag_int64", text = display_name, speed = 1.0f))

        } elif (t == Type.tUInt64) {
            edit_drag_scalar(reinterpret<uint64?>(ptr), (id = "drag_uint64", text = display_name, speed = 1.0f))

        } elif (t == Type.tDouble) {
            edit_drag_scalar(reinterpret<double?>(ptr), (id = "drag_double", text = display_name, speed = 0.01f))

        // Векторы Float
        } elif (t == Type.tFloat2) {
            edit_drag_float2(reinterpret<float2?>(ptr), (id = "drag_float2", text = display_name, speed = 0.01f))

        } elif (t == Type.tFloat3) {
            edit_drag_float3(reinterpret<float3?>(ptr), (id = "drag_float3", text = display_name, speed = 0.01f))

        } elif (t == Type.tFloat4) {
            edit_drag_float4(reinterpret<float4?>(ptr), (id = "drag_float4", text = display_name, speed = 0.01f))

        // Векторы Int
        } elif (t == Type.tInt2) {
            edit_drag_int2(reinterpret<int2?>(ptr), (id = "drag_int2", text = display_name, speed = 1.0f))

        } elif (t == Type.tInt3) {
            edit_drag_int3(reinterpret<int3?>(ptr), (id = "drag_int3", text = display_name, speed = 1.0f))

        } elif (t == Type.tInt4) {
            edit_drag_int4(reinterpret<int4?>(ptr), (id = "drag_int4", text = display_name, speed = 1.0f))

        // Векторы UInt
        } elif (t == Type.tUInt2) {
            edit_drag_int2(reinterpret<int2?>(ptr), (id = "drag_uint2", text = display_name, speed = 1.0f))

        } elif (t == Type.tUInt3) {
            edit_drag_int3(reinterpret<int3?>(ptr), (id = "drag_uint3", text = display_name, speed = 1.0f))

        } elif (t == Type.tUInt4) {
            edit_drag_int4(reinterpret<int4?>(ptr), (id = "drag_uint4", text = display_name, speed = 1.0f))

        // Чекбокс
        } elif (t == Type.tBool) {
            W_BOOL[id_key].value = *reinterpret<bool?>(ptr)
            if (checkbox(W_BOOL[id_key], (text = display_name))) {
                *reinterpret<bool?>(ptr) = W_BOOL[id_key].value
            }

        // Текст
        } elif (t == Type.tString) {
            let current_val = *reinterpret<string?>(ptr)
            if (!key_exists(W_STRING, id_key) || W_STRING[id_key].value != current_val) {
                init_input_text(W_STRING[id_key], current_val)
            }
            if (input_text(W_STRING[id_key], (text = display_name, flags = ImGuiInputTextFlags.None))) {
                *reinterpret<string?>(ptr) = W_STRING[id_key].value
            }

        } elif (t == Type.none) {
            text("{display_name}: none type")

        } else {
            text("{display_name}: not found drawer for this type ({t})")
        }
    }
}

// ---------------------------------------------------------------------
// Рекурсивная отрисовка полей структуры (аналог SerializeStruct).
// Обходит VarInfo?? внутри StructInfo, вычисляет offset каждого поля
// и либо рекурсивно спускается в tStructure, либо рисует скаляр/вектор.
// ---------------------------------------------------------------------
def private draw_struct_fields(ptr : void?; si : StructInfo const?; id_prefix : string) {
    if (ptr == null) {
        return
    }
    if (si == null) {
        return
    }

    unsafe {
        if (si.fields != null) {
            var pp = si.fields   // VarInfo??
            for (c in range(si.count)) {
                let current = *pp
                pp++
                if (current == null) {
                    continue
                }

                let fieldPtr = reinterpret<void?>(reinterpret<uint64>(ptr) + uint64(current.offset))
                let field_key = "{id_prefix}_{current.name}"

                with_id(current.name) {
                    if (current.basicType == Type.tStructure) {
                        // Вложенная структура — tree_node даёт TreePop + отступ тела.
                        // DefaultOpen — чтобы структура сразу была развёрнута;
                        // убери флаг, если хочешь свёрнутое состояние по умолчанию.
                        tree_node(BASIC_TREE, (text = current.name,
                                               flags = ImGuiTreeNodeFlags.DefaultOpen)) {
                            draw_struct_fields(fieldPtr, current.structType, field_key)
                        }
                    } else {
                        draw_value_at_ptr(fieldPtr, current.basicType, current.name, field_key)
                    }
                }
            }
        }
    }
}