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
// поиск сущности в decsState
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
// Таблицы состояний для ImGui (теперь все типы изолированы в таблицах)
// ---------------------------------------------------------------------
var W_FLOAT  : table<string; InputStateFloat>
var W_FLOAT2 : table<string; InputStateFloat2>
var W_FLOAT3 : table<string; InputStateFloat3>
var W_FLOAT4 : table<string; InputStateFloat4>
var W_INT    : table<string; InputStateInt>
var W_BOOL   : table<string; ToggleState>
var W_STRING : table<string; InputTextState>

var W_FILTER : TextFilterState

// Переменная для отслеживания смены фокуса
var last_selected_eid : EntityId = INVALID_ENTITY_ID

def clear_inspector_states() {
    clear(W_FLOAT)
    clear(W_FLOAT2)
    clear(W_FLOAT3)
    clear(W_FLOAT4)
    clear(W_INT)
    clear(W_BOOL)
    clear(W_STRING)
    // print("clear_inspector_states \n")
}

// ---------------------------------------------------------------------
// Отрисовка дефолтных типов
// ---------------------------------------------------------------------
def draw_component_value(comp : Component; index : int64, display_name : string) {
    let offset = int(index) * comp.stride
    // Уникальный ключ для конкретного свойства конкретной сущности
    let i = "{selected_eid.id}_{comp.name}"

    unsafe {
        let ptr = addr(comp.data[offset])

        if (comp.info.basicType == Type.tFloat) {
            if (!key_exists(W_FLOAT, i)) {
                W_FLOAT[i].value = *reinterpret<float?>(ptr)
            }
            input_float(W_FLOAT[i], (text = display_name, step = 0.01f, step_fast = 1.0f))
            *reinterpret<float?>(ptr) = W_FLOAT[i].value

        } elif (comp.info.basicType == Type.tFloat2) {
            if (!key_exists(W_FLOAT2, i)) {
                W_FLOAT2[i].value = *reinterpret<float2?>(ptr)
            }
            // Убрали step и step_fast
            input_float2(W_FLOAT2[i], (text = display_name, format = "%.3f"))
            *reinterpret<float2?>(ptr) = W_FLOAT2[i].value

        } elif (comp.info.basicType == Type.tFloat3) {
            if (!key_exists(W_FLOAT3, i)) {
                W_FLOAT3[i].value = *reinterpret<float3?>(ptr)
            }
            // Убрали step и step_fast
            input_float3(W_FLOAT3[i], (text = display_name, format = "%.3f"))
            *reinterpret<float3?>(ptr) = W_FLOAT3[i].value

        } elif (comp.info.basicType == Type.tFloat4) {
            if (!key_exists(W_FLOAT4, i)) {
                W_FLOAT4[i].value = *reinterpret<float4?>(ptr)
            }
            // Убрали step и step_fast
            input_float4(W_FLOAT4[i], (text = display_name, format = "%.3f"))
            *reinterpret<float4?>(ptr) = W_FLOAT4[i].value
        } elif (comp.info.basicType == Type.tInt) {
            if (!key_exists(W_INT, i)) {
                W_INT[i].value = *reinterpret<int?>(ptr)
            }
            input_int(W_INT[i], (text = display_name, step = 1, step_fast = 10))
            *reinterpret<int?>(ptr) = W_INT[i].value

        } elif (comp.info.basicType == Type.tBool) {
            if (!key_exists(W_BOOL, i)) {
                W_BOOL[i].value = *reinterpret<bool?>(ptr)
            }
            checkbox(W_BOOL[i], (text = display_name))
            *reinterpret<bool?>(ptr) = W_BOOL[i].value

        } elif (comp.info.basicType == Type.tString) {
            if (!key_exists(W_STRING, i)) {
                // Инициализируем и буфер, и строковое зеркало за один раз
                init_input_text(W_STRING[i], *reinterpret<string?>(ptr))
            }
            input_text(W_STRING[i], (text = display_name, flags = ImGuiInputTextFlags.None))
            *reinterpret<string?>(ptr) = W_STRING[i].value

        } else {
            // text("{comp.name}: {comp.info.dumper(voidptr(ptr))}")
            text("{display_name}: not found drawer for this type")
        }
    }
}

// Вспомогательная функция для безопасной инициализации InputTextState
def init_input_text(var state : InputTextState; val : string) {
    state.value = val
    let len = length(val)
    // Выделяем память с запасом (минимум 256 байт или длина строки + null-терминатор)
    state.capacity = max(256, len + 1)
    resize(state.buffer, state.capacity)

    // Посимвольно копируем строку в буфер байт
    for (j in range(len)) {
        state.buffer[j] = uint8(character_at(val, j))
    }
    state.buffer[len] = 0u8 // Не забываем про null-терминатор для C-строки
}

// ---------------------------------------------------------------------
// оверрайды под конкретные компоненты
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
// окно инспектора
// ---------------------------------------------------------------------
def private split_component_name(name : string; var out_prefix : string&; var out_field : string&) : bool {
    // Если decs склеил структуру как transform.position, ищем точку. 
    // Если у тебя используется другой разделитель (например, подчёркивание), поменяй тут на "_"
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

        // Сюда будем складывать имена уже отрисованных групп (структур), чтобы не рисовать их дважды
        var processed_groups : table<string; bool>

        for (comp in decsState.allArchetypes[arch_idx].components) {
            if (comp.name == "eid") {
                continue
            }

            // Пропускаем компонент, если он не подходит под поисковый фильтр
            if (!passes_filter(W_FILTER, comp.name)) {
                continue
            }

            var prefix = ""
            var field_name = ""

            if (split_component_name(comp.name, prefix, field_name)) {
                // --- СЛУЧАЙ 1: Это поле структуры (например, transform.position) ---
                if (key_exists(processed_groups, prefix)) {
                    continue // Эту группу мы уже отрисовали целиком на одной из прошлых итераций
                }
                processed_groups[prefix] = true

                // Создаем один большой collapsing_header для всей структуры
                with_id("{prefix}{selected_eid}") {
                    collapsing_header(BASIC_CHR, (text = prefix, closable = false, flags = ImGuiTreeNodeFlags.None)) {

                        // Внутренним циклом собираем ВСЕ поля, которые принадлежат этому префиксу
                        for (sub_comp in decsState.allArchetypes[arch_idx].components) {
                            var sub_prefix = ""
                            var sub_field = ""
                            if (split_component_name(sub_comp.name, sub_prefix, sub_field) && sub_prefix == prefix) {
                                // Отрисовываем поле внутри группы, передавая только чистый хвост имени (например, "position")
                                with_id(sub_field) {
                                    draw_component_value(sub_comp, rec_index, sub_field)
                                }
                            }
                        }
                    }
                }
            } else {
                // --- СЛУЧАЙ 2: Обычный плоский компонент (например, tag) ---
                with_id("{comp.name}{selected_eid}") {
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