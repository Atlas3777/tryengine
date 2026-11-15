options gen2
options relaxed_assign = false

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
require tryeditor/Hierarchy
require tryengine/EcsMacro
require tryengine/Serializable
require daslib/rtti

// ---------------------------------------------------------------------
// Поиск сущности в decsState
// ---------------------------------------------------------------------
def get_entity_location(eid : EntityId; var out_arch_idx : int&; var out_index : int64&) : bool {
    if (eid.id >= uint(length(decsState.entityLookup)) || decsState.entityLookup[int(eid.id)].generation != eid.generation) {
        return false
    }

    let rec = decsState.entityLookup[int(eid.id)]
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

var W_BOOL   : table<string; ToggleState>
var W_STRING : table<string; InputTextState>

// Переменная для отслеживания смены фокуса
var last_selected_eid : EntityId = INVALID_ENTITY_ID

def clear_inspector_states() {
    clear(W_BOOL)
    clear(W_STRING)
}

// ---------------------------------------------------------------------
// Отрисовка значения компонента. Поля структуры обходит схема, сгенерированная
// через apply на этапе компиляции (см. tryengine/Serializable): runtime reflection
// по StructInfo не нужен, а аннотации полей (@hide, @title, @min, @max, @speed,
// @readonly) попадают прямо в UI.
// ---------------------------------------------------------------------
def draw_component_value(comp : Component; index : int64; display_name : string) {
    let offset = int(index) * comp.stride
    let id_key = "{selected_eid.id}_{comp.name}"
    let type_name = NormalizeTypeName(comp.info.fullName)

    unsafe {
        let ptr = reinterpret<void?>(addr(comp.data[offset]))

        if (comp.info.basicType == Type.tStructure) {
            draw_struct_fields(ptr, type_name, id_key)
        } else {
            var ui : FieldUi
            ui.title = display_name
            draw_leaf(ptr, type_name, display_name, id_key, ui)
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
    query(eid) $(rel : Relationship) {
        text("parent:      [{rel.parent.id}]")
        if (rel.parent != INVALID_ENTITY_ID) {
            same_line()
            if (button(BT_GO, (text = "Go"))) {
                selected_eid = rel.parent
            }
        }
        text("first_child: [{rel.first_child.id}]")
        text("next:        [{rel.next.id}]")
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

        text("Entity [{selected_eid.id:d}]")
        let a = unsafe(reinterpret<uint64>(selected_eid));
        text("Entity2 [{a:d}]")

        // ---- ПОИСК ПО КОМПОНЕНТАМ ----
        text_filter(W_FILTER, "Search", 200f)

        separator()

        var processed_groups : table<string; bool>

        for (comp in decsState.allArchetypes[arch_idx].components) {
            if (comp.name == "eid" || !passes_filter(W_FILTER, comp.name)) {
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
                    collapsing_header(BASIC_CHR, (text = prefix, closable = false, flags = ImGuiTreeNodeFlags.DefaultOpen)) {
                        // Проверяем оверрайд для префикса всей структуры
                        if (key_exists(g_overrides, prefix)) {
                            g_overrides[prefix] |> invoke(selected_eid)
                        } elif (!draw_template_group(arch_idx, rec_index, prefix, "{prefix}{selected_eid.id}")) {
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
                    collapsing_header(BASIC_CHR, (text = comp.name, closable = false, flags = ImGuiTreeNodeFlags.DefaultOpen)) {
                        if (key_exists(g_overrides, comp.name)) {
                            g_overrides[comp.name] |> invoke(selected_eid)
                        } else {
                            draw_component_value(comp, rec_index, comp.name)
                        }
                    }
                }
            }
        }
        AddComponent(selected_eid)
    }
}

// ---------------------------------------------------------------------
// Popup "Add component"
// ---------------------------------------------------------------------
// Построчный ToggleState: [widget]-макрос авто-эмитит только синглтоны,
// для строк в цикле держим таблицу явно (как SH_ROW в selectable_hover.rst).
var g_add_cmp_rows : table<string; ToggleState>

def private AddComponent(target_eid : EntityId) {
    if (button(ADD_CMP, (text = "Add component"))) {
        clear(g_add_cmp_rows)
        ADD_CMP_POPUP.pending_open = true
    }

    popup(ADD_CMP_POPUP, (text = "Add component", flags = ImGuiWindowFlags.None)) {
        // Фильтр — как в инспекторе (text_filter + passes_filter)
        text_filter(W_ADD_CMP_FILTER, "Search", 220f)
        separator()

        child(ADD_CMP_LIST, (text = "child", size = float2(250f, 300f),
         child_flags = ImGuiChildFlags.AutoResizeY, window_flags = ImGuiWindowFlags.None)) {
            var shown = 0
            for (name, n in keys(ecs_component_table), values(ecs_component_table)) {
                if (!passes_filter(W_ADD_CMP_FILTER, name)) {
                    continue
                }
                shown++
                if (selectable(g_add_cmp_rows[name], (text = "{name} : {n.cmp_type}"))) {
                    n.cmp_creator(target_eid)
                    ADD_CMP_POPUP.pending_close = true
                }
            }
            if (shown == 0) {
                text("No matches")
            }
        }
    }
}
// ---------------------------------------------------------------------
// Аннотации полей, которые понимает инспектор (3-й аргумент apply):
//   @hide            — поле не рисуется;
//   @title = "..."   — подпись вместо имени поля;
//   @min / @max      — границы значения (можно задавать по одной);
//   @speed = 0.1     — шаг drag-виджета;
//   @readonly        — значение показывается текстом, без редактирования.
// Неизвестные аннотации игнорируются: их могут читать другие потребители.
// ---------------------------------------------------------------------
struct FieldUi {
    hidden    : bool
    readonly  : bool
    title     : string
    has_min   : bool
    has_max   : bool
    min_value : double
    max_value : double
    speed     : float
}

def private annotation_number(data : RttiValue; var out : double&) : bool {
    if (data is tFloat) {
        out = double(data as tFloat)
        return true
    } elif (data is tDouble) {
        out = data as tDouble
        return true
    } elif (data is tInt) {
        out = double(data as tInt)
        return true
    } elif (data is tInt64) {
        out = double(data as tInt64)
        return true
    } elif (data is tUInt64) {
        out = double(data as tUInt64)
        return true
    }
    return false
}

def private parse_field_ui(field_name : string; annotations : array<tuple<name : string; data : RttiValue>>) : FieldUi {
    var ui : FieldUi
    ui.title = field_name
    var has_min = false
    var has_max = false
    for (ann in annotations) {
        if (ann.name == "hide") {
            ui.hidden = ann.data is tBool ? (ann.data as tBool) : true
        } elif (ann.name == "readonly") {
            ui.readonly = ann.data is tBool ? (ann.data as tBool) : true
        } elif (ann.name == "title" && ann.data is tString) {
            ui.title = ann.data as tString
        } elif (ann.name == "speed") {
            var v : double
            if (annotation_number(ann.data, v)) {
                ui.speed = float(v)
            }
        } elif (ann.name == "min") {
            var v : double
            if (annotation_number(ann.data, v)) {
                has_min = true
                ui.min_value = v
            }
        } elif (ann.name == "max") {
            var v : double
            if (annotation_number(ann.data, v)) {
                has_max = true
                ui.max_value = v
            }
        }
    }
    ui.has_min = has_min
    ui.has_max = has_max
    return <- ui
}

//! Текстовое представление значения — для @readonly и неподдерживаемых типов.
def private value_text(ptr : void?; type_name : string) : string {
    if (ptr == null) {
        return "<null>"
    }
    unsafe {
        if (type_name == "float") return "{*reinterpret<float?>(ptr)}"
        elif (type_name == "double") return "{*reinterpret<double?>(ptr)}"
        elif (type_name == "int") return "{*reinterpret<int?>(ptr):d}"
        elif (type_name == "uint") return "{*reinterpret<uint?>(ptr):d}"
        elif (type_name == "int8") return "{*reinterpret<int8?>(ptr):d}"
        elif (type_name == "uint8") return "{*reinterpret<uint8?>(ptr):d}"
        elif (type_name == "int16") return "{*reinterpret<int16?>(ptr):d}"
        elif (type_name == "uint16") return "{*reinterpret<uint16?>(ptr):d}"
        elif (type_name == "int64") return "{*reinterpret<int64?>(ptr):d}"
        elif (type_name == "uint64") return "{*reinterpret<uint64?>(ptr):d}"
        elif (type_name == "bool") return "{*reinterpret<bool?>(ptr)}"
        elif (type_name == "string") return "*reinterpret<string?>(ptr)"
        elif (type_name == "float2") return "{*reinterpret<float2?>(ptr)}"
        elif (type_name == "float3") return "{*reinterpret<float3?>(ptr)}"
        elif (type_name == "float4") return "{*reinterpret<float4?>(ptr)}"
        elif (type_name == "int2") {
            let v = *reinterpret<int2?>(ptr)
            return "{v.x:d} {v.y:d}"
        } elif (type_name == "int3") {
            let v = *reinterpret<int3?>(ptr)
            return "{v.x:d} {v.y:d} {v.z:d}"
        } elif (type_name == "int4") {
            let v = *reinterpret<int4?>(ptr)
            return "{v.x:d} {v.y:d} {v.z:d} {v.w:d}"
        } elif (type_name == "uint2") {
            let v = *reinterpret<uint2?>(ptr)
            return "{v.x:d} {v.y:d}"
        } elif (type_name == "uint3") {
            let v = *reinterpret<uint3?>(ptr)
            return "{v.x:d} {v.y:d} {v.z:d}"
        } elif (type_name == "uint4") {
            let v = *reinterpret<uint4?>(ptr)
            return "{v.x:d} {v.y:d} {v.z:d} {v.w:d}"
        }
    }
    return "<{type_name}>"
}

// ---------------------------------------------------------------------
// Ограничения @min/@max. ImGui-обёртка edit_drag_scalar принимает границы
// только парой (v_min != v_max), а Ctrl+Click ввод не ограничивает вовсе,
// поэтому после изменения значения дожимаем его сами. Правим только при
// реальной правке: загруженные значения вне диапазона не трогаем.
// ---------------------------------------------------------------------
def private clamp_signed(var v : int64&; ui : FieldUi) {
    if (ui.has_min && v < int64(ui.min_value)) {
        v = int64(ui.min_value)
    }
    if (ui.has_max && v > int64(ui.max_value)) {
        v = int64(ui.max_value)
    }
}

def private clamp_unsigned(var v : uint64&; ui : FieldUi) {
    if (ui.has_min) {
        let lo = ui.min_value > 0.0lf ? uint64(ui.min_value) : 0ul
        if (v < lo) {
            v = lo
        }
    }
    if (ui.has_max) {
        let hi = ui.max_value > 0.0lf ? uint64(ui.max_value) : 0ul
        if (v > hi) {
            v = hi
        }
    }
}

def private clamp_number(ptr : void?; type_name : string; ui : FieldUi) {
    if (ptr == null || !(ui.has_min || ui.has_max)) {
        return
    }
    unsafe {
        if (type_name == "float") {
            var v = *reinterpret<float?>(ptr)
            if (ui.has_min && v < float(ui.min_value)) { v = float(ui.min_value) }
            if (ui.has_max && v > float(ui.max_value)) { v = float(ui.max_value) }
            *reinterpret<float?>(ptr) = v
        } elif (type_name == "double") {
            var v = *reinterpret<double?>(ptr)
            if (ui.has_min && v < ui.min_value) { v = ui.min_value }
            if (ui.has_max && v > ui.max_value) { v = ui.max_value }
            *reinterpret<double?>(ptr) = v
        } elif (type_name == "int") {
            var v = int64(*reinterpret<int?>(ptr))
            clamp_signed(v, ui)
            *reinterpret<int?>(ptr) = int(v)
        } elif (type_name == "uint") {
            var v = uint64(*reinterpret<uint?>(ptr))
            clamp_unsigned(v, ui)
            *reinterpret<uint?>(ptr) = uint(v)
        } elif (type_name == "int8") {
            var v = int64(*reinterpret<int8?>(ptr))
            clamp_signed(v, ui)
            *reinterpret<int8?>(ptr) = int8(v)
        } elif (type_name == "uint8") {
            var v = uint64(*reinterpret<uint8?>(ptr))
            clamp_unsigned(v, ui)
            *reinterpret<uint8?>(ptr) = uint8(v)
        } elif (type_name == "int16") {
            var v = int64(*reinterpret<int16?>(ptr))
            clamp_signed(v, ui)
            *reinterpret<int16?>(ptr) = int16(v)
        } elif (type_name == "uint16") {
            var v = uint64(*reinterpret<uint16?>(ptr))
            clamp_unsigned(v, ui)
            *reinterpret<uint16?>(ptr) = uint16(v)
        } elif (type_name == "int64") {
            var v = *reinterpret<int64?>(ptr)
            clamp_signed(v, ui)
            *reinterpret<int64?>(ptr) = v
        } elif (type_name == "uint64") {
            var v = *reinterpret<uint64?>(ptr)
            clamp_unsigned(v, ui)
            *reinterpret<uint64?>(ptr) = v
        }
    }
}

//! Виджет одного поля по имени типа daslang (его даёт apply-схема или CTypeInfo).
def private draw_leaf(ptr : void?; type_name : string; display_name : string; id_key : string; ui : FieldUi) {
    if (ui.readonly) {
        text("{display_name}: {value_text(ptr, type_name)}")
    } elif (ends_with(type_name, "?")) {
        text("{display_name}: pointer {type_name}")
    } elif (type_name == "float") {
        let speed = ui.speed > 0.0f ? ui.speed : 0.01f
        unsafe {
            var changed = false
            if (ui.has_min && ui.has_max) {
                var lo = float(ui.min_value)
                var hi = float(ui.max_value)
                changed = edit_drag_scalar(reinterpret<float?>(ptr), (id = "drag_float", text = display_name, speed = speed, v_min = lo, v_max = hi))
            } else {
                changed = edit_drag_scalar(reinterpret<float?>(ptr), (id = "drag_float", text = display_name, speed = speed))
            }
            if (changed) {
                clamp_number(ptr, type_name, ui)
            }
        }
    } elif (type_name == "double") {
        let speed = ui.speed > 0.0f ? ui.speed : 0.01f
        unsafe {
            if (edit_drag_scalar(reinterpret<double?>(ptr), (id = "drag_double", text = display_name, speed = speed))) {
                clamp_number(ptr, type_name, ui)
            }
        }
    } elif (type_name == "int") {
        let speed = ui.speed > 0.0f ? ui.speed : 1.0f
        unsafe {
            var changed = false
            if (ui.has_min && ui.has_max) {
                var lo = int(ui.min_value)
                var hi = int(ui.max_value)
                changed = edit_drag_scalar(reinterpret<int?>(ptr), (id = "drag_int", text = display_name, speed = speed, v_min = lo, v_max = hi))
            } else {
                changed = edit_drag_scalar(reinterpret<int?>(ptr), (id = "drag_int", text = display_name, speed = speed))
            }
            if (changed) {
                clamp_number(ptr, type_name, ui)
            }
        }
    } elif (type_name == "uint") {
        unsafe {
            if (edit_drag_scalar(reinterpret<uint?>(ptr), (id = "drag_uint", text = display_name, speed = 1.0f))) {
                clamp_number(ptr, type_name, ui)
            }
        }
    } elif (type_name == "int8") {
        unsafe {
            if (edit_drag_scalar(reinterpret<int8?>(ptr), (id = "drag_int8", text = display_name, speed = 1.0f))) {
                clamp_number(ptr, type_name, ui)
            }
        }
    } elif (type_name == "uint8") {
        unsafe {
            if (edit_drag_scalar(reinterpret<uint8?>(ptr), (id = "drag_uint8", text = display_name, speed = 1.0f))) {
                clamp_number(ptr, type_name, ui)
            }
        }
    } elif (type_name == "int16") {
        unsafe {
            if (edit_drag_scalar(reinterpret<int16?>(ptr), (id = "drag_int16", text = display_name, speed = 1.0f))) {
                clamp_number(ptr, type_name, ui)
            }
        }
    } elif (type_name == "uint16") {
        unsafe {
            if (edit_drag_scalar(reinterpret<uint16?>(ptr), (id = "drag_uint16", text = display_name, speed = 1.0f))) {
                clamp_number(ptr, type_name, ui)
            }
        }
    } elif (type_name == "int64") {
        // У ImGui формат по умолчанию для 64-битных — %lld/%llu. dasImgui подставляет
        // "%d"/"%u", и DragScalar печатает только младшие 32 бита, поэтому задаём явно.
        unsafe {
            if (edit_drag_scalar(reinterpret<int64?>(ptr), (id = "drag_int64", text = display_name, speed = 1.0f, v_min = 0l, v_max = 0l, format = "%lld"))) {
                clamp_number(ptr, type_name, ui)
            }
        }
    } elif (type_name == "uint64") {
        unsafe {
            if (edit_drag_scalar(reinterpret<uint64?>(ptr), (id = "drag_uint64", text = display_name, speed = 1.0f, v_min = 0ul, v_max = 0ul, format = "%llu"))) {
                clamp_number(ptr, type_name, ui)
            }
        }
    } elif (type_name == "float2") {
        unsafe {
            edit_drag_float2(reinterpret<float2?>(ptr), (id = "drag_float2", text = display_name, speed = 0.01f))
        }
    } elif (type_name == "float3") {
        unsafe {
            edit_drag_float3(reinterpret<float3?>(ptr), (id = "drag_float3", text = display_name, speed = 0.01f))
        }
    } elif (type_name == "float4") {
        unsafe {
            edit_drag_float4(reinterpret<float4?>(ptr), (id = "drag_float4", text = display_name, speed = 0.01f))
        }
    } elif (type_name == "int2") {
        unsafe {
            edit_drag_int2(reinterpret<int2?>(ptr), (id = "drag_int2", text = display_name, speed = 1.0f))
        }
    } elif (type_name == "int3") {
        unsafe {
            edit_drag_int3(reinterpret<int3?>(ptr), (id = "drag_int3", text = display_name, speed = 1.0f))
        }
    } elif (type_name == "int4") {
        unsafe {
            edit_drag_int4(reinterpret<int4?>(ptr), (id = "drag_int4", text = display_name, speed = 1.0f))
        }
    } elif (type_name == "bool") {
        unsafe {
            W_BOOL[id_key].value = *reinterpret<bool?>(ptr)
            if (checkbox(W_BOOL[id_key], (text = display_name))) {
                *reinterpret<bool?>(ptr) = W_BOOL[id_key].value
            }
        }
    } elif (type_name == "string") {
        unsafe {
            let current_val = *reinterpret<string?>(ptr)
            if (!key_exists(W_STRING, id_key) || W_STRING[id_key].value != current_val) {
                init_input_text(W_STRING[id_key], current_val)
            }
            if (input_text(W_STRING[id_key], (text = display_name, flags = ImGuiInputTextFlags.None))) {
                *reinterpret<string?>(ptr) = W_STRING[id_key].value
            }
        }
    } elif (type_name == "uint2" || type_name == "uint3" || type_name == "uint4") {
        // Виджетов под uint-векторы в dasImgui нет; показываем значение текстом.
        text("{display_name}: {value_text(ptr, type_name)}")
    } elif (type_name == "float3x3" || type_name == "float3x4" || type_name == "float4x4") {
        text("{display_name}: matrix editing is not implemented yet")
    } else {
        text("{display_name}: no editor for type {type_name}")
    }
}

// ---------------------------------------------------------------------
// Схема полей структуры: имена, типы, смещения и аннотации приходят из
// apply-сгенерированной таблицы, значения читаются/пишутся по указателю.
// ---------------------------------------------------------------------
def private draw_struct_fields(ptr : void?; type_name : string; id_prefix : string) {
    if (ptr == null) {
        return
    }

    var fields : array<InspectField>
    if (!DecsInspectFields(type_name, fields)) {
        text("{type_name}: no inspector schema, mark the struct [DecsSerializable]")
        return
    }

    for (field in fields) {
        var ui = parse_field_ui(field.name, field.annotations)
        if (ui.hidden) {
            continue
        }

        unsafe {
            let field_ptr = reinterpret<void?>(reinterpret<uint64>(ptr) + field.offset)
            draw_schema_field(field_ptr, field, ui, id_prefix)
        }
    }
}

//! Одно поле по описанию из apply-схемы: виджет, вложенная структура или текст.
def private draw_schema_field(ptr : void?; field : InspectField; ui : FieldUi; id_prefix : string) {
    with_id(field.name) {
        if (ui.readonly) {
            text("{ui.title}: {value_text(ptr, field.typeName)}")
        } elif (DecsHasInspectSchema(field.typeName)) {
            tree_node(BASIC_TREE, (text = ui.title, flags = ImGuiTreeNodeFlags.DefaultOpen)) {
                draw_struct_fields(ptr, field.typeName, "{id_prefix}_{field.name}")
            }
        } else {
            draw_leaf(ptr, field.typeName, ui.title, "{id_prefix}_{field.name}", ui)
        }
    }
}

// ---------------------------------------------------------------------
// Группа префиксных компонентов шаблона DECS (Transform_position, ...).
// Порядок полей, типы и аннотации берём из схемы шаблона; значение — из
// компонента "<префикс>_<поле>". false — схемы нет (рисуем старый фолбэк).
// ---------------------------------------------------------------------
def private draw_template_group(arch_idx : int; rec_index : int64; prefix : string; id_key : string) : bool {
    var fields : array<InspectField>
    if (!DecsInspectFields(prefix, fields)) {
        return false
    }

    for (field in fields) {
        var ui = parse_field_ui(field.name, field.annotations)
        if (ui.hidden) {
            continue
        }
        let ci = component_index(arch_idx, "{prefix}_{field.name}")
        if (ci < 0) {
            continue
        }
        unsafe {
            let ptr = reinterpret<void?>(component_value_ptr(arch_idx, rec_index, ci))
            draw_schema_field(ptr, field, ui, id_key)
        }
    }
    return true
}

def private component_index(arch_idx : int; comp_name : string) : int {
    for (i in range(length(decsState.allArchetypes[arch_idx].components))) {
        if (decsState.allArchetypes[arch_idx].components[i].name == comp_name) {
            return i
        }
    }
    return -1
}

def private component_value_ptr(arch_idx : int; rec_index : int64; comp_index : int) : uint64 {
    unsafe {
        var comp & = decsState.allArchetypes[arch_idx].components[comp_index]
        return reinterpret<uint64>(addr(comp.data[int(rec_index) * comp.stride]))
    }
}
