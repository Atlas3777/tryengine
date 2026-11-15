options gen2
options relaxed_assign = false

module Material shared public

// Материал — это данные, заполняющие шейдерный шаблон. Набор полей задаёт рефлексия
// Slang, поэтому здесь нет ни albedo_color, ни roughness: редактор получает схему
// `params` из C++ (GetMaterialSchema) и рисует поля по их типу.
//
// C++-зеркало сериализуемой структуры — engine_source/graphics/include/engine/graphics/MaterialConfig.hpp.
// Обе стороны пишут один и тот же ConfigFormat.

require tryengine/ConfigFormat
require tryengine/ConfigFormatBoost
require tryengine/Types
require tryengine/EcsMacro
require tryeditor/Hierarchy

require imgui
require imgui/imgui_widgets_builtin
require imgui/imgui_containers_builtin
require imgui/imgui_layout_builtin
require imgui/imgui_id_builtin
require imgui/imgui_boost_runtime
require imgui/imgui_boost_v2

require daslib/fio
require daslib/decs
require strings
require math

require TryEditor
require tryResources

struct MaterialConfig {
    name     : string
    shader   : uint64
    params   : table<string; StructField?>
    textures : table<string; StructField?>
}

struct MaterialParam {
    name       : string
    value_type : string
    kind       : string
    binding    : uint
    offset     : uint
    size       : uint
}

var private material_path = "game/data/materials/standard.mat"
var private material : MaterialConfig
var private schema : array<MaterialParam>
var private material_loaded = false
var private schema_pending = false
var private status = ""
var private material_guid : uint64 = 0ul
var private material_ref = ""
var private material_readonly = false
var private last_synced_eid : EntityId = INVALID_ENTITY_ID
var private follow_state = ToggleState(value = true)

var private MATERIAL_PATH <- InputTextState()
var private material_path_initialized = false
var private auto_load_done = false

def private InitInputText(var state : InputTextState; val : string) {
    state.value = val
    let len = length(val)
    state.capacity = max(256, len + 1)
    resize(state.buffer, state.capacity)
    for (j in range(len)) {
        state.buffer[j] = uint8(character_at(val, j))
    }
    state.buffer[len] = 0u8
}

// ---------------------------------------------------------------------------
// Доступ к объектам конфига
// ---------------------------------------------------------------------------

def private GetField(var obj : table<string; StructField?>; key : string) : StructField? {
    if (!key_exists(obj, key)) {
        return null
    }
    unsafe {
        return obj[key]
    }
}

def private GetFieldConst(obj : table<string; StructField?> const; key : string) : StructField const? {
    unsafe {
        return obj?[key] ?? null
    }
}

def private GetString(obj : table<string; StructField?> const; key : string; fallback : string) : string {
    let field : StructField const? = GetFieldConst(obj, key)
    if (field != null && field.value is _string) {
        return field.value as _string
    }
    return fallback
}

def private GetUInt64(obj : table<string; StructField?> const; key : string; fallback : uint64) : uint64 {
    let field : StructField const? = GetFieldConst(obj, key)
    if (field != null && field.value is _uint64) {
        return field.value as _uint64
    }
    return fallback
}

def private GetUInt(obj : table<string; StructField?> const; key : string; fallback : uint) : uint {
    let field : StructField const? = GetFieldConst(obj, key)
    if (field != null && field.value is _uint) {
        return field.value as _uint
    }
    return fallback
}

// ---------------------------------------------------------------------------
// Материал: разбор и запись
// ---------------------------------------------------------------------------

def private ParseMaterial(text : string; var out : MaterialConfig; var error : string&) : bool {
    error = ""
    var root <- read_config(text, error)
    if (length(error) > 0 || root == null) {
        return false
    }
    if (!(root.value is _object)) {
        error = "root is not an object"
        return false
    }

    var obj <- root.value as _object
    out.name = GetString(obj, "name", "")
    out.shader = GetUInt64(obj, "shader", 0ul)
    clear(out.params)
    clear(out.textures)

    // Две группы полей материала: uniform-параметры и сэмплируемые текстуры.
    var params = GetField(obj, "params")
    if (params != null && params.value is _object) {
        out.params <- params.value as _object
    }
    var textures = GetField(obj, "textures")
    if (textures != null && textures.value is _object) {
        out.textures <- textures.value as _object
    }
    return true
}

def private WriteMaterial(var m : MaterialConfig) : string {
    var root : table<string; StructField?>
    var name_field = CF(m.name)
    if (name_field != null) {
        name_field.name = "name"
        root["name"] = name_field
    }
    var shader_field = CF(m.shader)
    if (shader_field != null) {
        shader_field.name = "shader"
        root["shader"] = shader_field
    }

    if (length(m.params) > 0) {
        var params_cf = CF(m.params)
        if (params_cf != null) {
            params_cf.name = "params"
            params_cf.typeName = "Params"
            root["params"] = params_cf
        }
    }
    if (length(m.textures) > 0) {
        var textures_cf = CF(m.textures)
        if (textures_cf != null) {
            textures_cf.name = "textures"
            textures_cf.typeName = "Textures"
            root["textures"] = textures_cf
        }
    }
    return write_config(CF(root))
}

// ---------------------------------------------------------------------------
// Схема из C++
// ---------------------------------------------------------------------------

def private ParseSchema(text : string; var out : array<MaterialParam>) : bool {
    clear(out)
    if (empty(text)) {
        return false
    }

    var error = ""
    var root <- read_config(text, error)
    if (length(error) > 0 || root == null || !(root.value is _object)) {
        return false
    }

    var obj <- root.value as _object
    var params = GetField(obj, "params")
    if (params == null || !(params.value is _array)) {
        return false
    }

    for (elem in params.value as _array) {
        if (elem == null || !(elem.value is _object)) {
            continue
        }
        assume item = elem.value as _object

        var p : MaterialParam
        p.name = GetString(item, "name", "")
        p.value_type = GetString(item, "type", "")
        p.kind = GetString(item, "kind", "")
        p.binding = GetUInt(item, "binding", 0u)
        p.offset = GetUInt(item, "offset", 0u)
        p.size = GetUInt(item, "size", 0u)
        if (!p.name.empty()) {
            out |> push(p)
        }
    }
    return true
}

// ---------------------------------------------------------------------------
// Значения по умолчанию
// ---------------------------------------------------------------------------

def private NewValue(type_name : string) : ConfigValue {
    if (type_name == "f32") {
        return <- ConfigValue(_float = 0.0f)
    } elif (type_name == "i32") {
        return <- ConfigValue(_int = 0)
    } elif (type_name == "u32") {
        return <- ConfigValue(_uint = 0u)
    } elif (type_name == "i64") {
        return <- ConfigValue(_int64 = 0l)
    } elif (type_name == "u64") {
        return <- ConfigValue(_uint64 = 0ul)
    } elif (type_name == "f32x2") {
        return <- ConfigValue(_float2 = float2(0.0f))
    } elif (type_name == "f32x3") {
        return <- ConfigValue(_float3 = float3(0.0f))
    } elif (type_name == "f32x4") {
        return <- ConfigValue(_float4 = float4(0.0f))
    } elif (type_name == "i32x2") {
        return <- ConfigValue(_int2 = int2(0))
    } elif (type_name == "i32x3") {
        return <- ConfigValue(_int3 = int3(0))
    } elif (type_name == "i32x4") {
        return <- ConfigValue(_int4 = int4(0))
    } elif (type_name == "u32x2") {
        return <- ConfigValue(_uint2 = uint2(0u))
    } elif (type_name == "u32x3") {
        return <- ConfigValue(_uint3 = uint3(0u))
    } elif (type_name == "u32x4") {
        return <- ConfigValue(_uint4 = uint4(0u))
    } elif (type_name == "f32m3x3") {
        return <- ConfigValue(_float3x3 = float3x3())
    } elif (type_name == "f32m3x4") {
        return <- ConfigValue(_float3x4 = float3x4())
    } elif (type_name == "f32m4x4") {
        return <- ConfigValue(_float4x4 = float4x4())
    } elif (type_name == "texture") {
        return <- ConfigValue(_uint64 = 0ul)
    }
    return <- ConfigValue(_float = 0.0f)
}

def private EnsureParamIn(var target : table<string; StructField?>; p : MaterialParam) {
    if (key_exists(target, p.name)) {
        return
    }
    var new_value <- NewValue(p.value_type)
    target[p.name] = new StructField(name = p.name, typeName = p.value_type, value <- new_value)
}

// Редактор раскладывает поля по двум таблицам: `kind` из схемы говорит, текстура это
// или uniform-параметр.
def private EnsureParam(p : MaterialParam) {
    if (p.kind == "texture") {
        EnsureParamIn(material.textures, p)
    } else {
        EnsureParamIn(material.params, p)
    }
}

def private GetParamField(p : MaterialParam) : StructField? {
    if (p.kind == "texture") {
        return GetField(material.textures, p.name)
    }
    return GetField(material.params, p.name)
}

// Замена значения целиком: ConfigValue — вариант с таблицами, его нельзя копировать,
// но новый StructField с новым значением создать можно.
def private SetParamIn(var target : table<string; StructField?>; p : MaterialParam; var value : ConfigValue) {
    target[p.name] = new StructField(name = p.name, typeName = p.value_type, value <- value)
}

def private SetParam(p : MaterialParam; var value : ConfigValue) {
    if (p.kind == "texture") {
        SetParamIn(material.textures, p, value)
    } else {
        SetParamIn(material.params, p, value)
    }
    LiveApply()
}

// ---------------------------------------------------------------------------
// Отрисовка
// ---------------------------------------------------------------------------

def private DrawParam(p : MaterialParam) {
    EnsureParam(p)
    let field = GetParamField(p)
    if (field == null) {
        with_id(p.name) {
            text("{p.name}: unsupported type {p.value_type}")
        }
        return
    }

    let title = "{p.name}  [{p.value_type}]"

    // Каждый параметр живёт под своим with_id: виджеты (в том числе text) привязаны к
    // call-site, и один и тот же text в цикле по параметрам иначе отрисуется дважды.
    with_id(p.name) {
        if (material_readonly) {
            text("{title} = {ValueToString(field.value)}")
        } elif (p.kind == "texture") {
            var v = 0ul
            if (field.value is _uint64) { v = field.value as _uint64 }
            var changed = false
            unsafe { changed = edit_drag_scalar(addr(v), (id = "tex", text = title, speed = 1.0f, v_min = 0ul, v_max = 0ul, format = "%llu")) }
            if (changed) { SetParam(p, ConfigValue(_uint64 = v)) }

        } elif (p.value_type == "f32") {
            var v = 0.0f
            if (field.value is _float) { v = field.value as _float }
            var changed = false
            unsafe { changed = edit_drag_scalar(addr(v), (id = "f32", text = title, speed = 0.01f)) }
            if (changed) { SetParam(p, ConfigValue(_float = v)) }

        } elif (p.value_type == "i32") {
            var v = 0
            if (field.value is _int) { v = field.value as _int }
            var changed = false
            unsafe { changed = edit_drag_scalar(addr(v), (id = "i32", text = title, speed = 1.0f)) }
            if (changed) { SetParam(p, ConfigValue(_int = v)) }

        } elif (p.value_type == "u32") {
            var v = 0u
            if (field.value is _uint) { v = field.value as _uint }
            var changed = false
            unsafe { changed = edit_drag_scalar(addr(v), (id = "u32", text = title, speed = 1.0f)) }
            if (changed) { SetParam(p, ConfigValue(_uint = v)) }

        } elif (p.value_type == "i64") {
            var v = 0l
            if (field.value is _int64) { v = field.value as _int64 }
            var changed = false
            unsafe { changed = edit_drag_scalar(addr(v), (id = "i64", text = title, speed = 1.0f, v_min = 0l, v_max = 0l, format = "%lld")) }
            if (changed) { SetParam(p, ConfigValue(_int64 = v)) }

        } elif (p.value_type == "u64") {
            var v = 0ul
            if (field.value is _uint64) { v = field.value as _uint64 }
            var changed = false
            unsafe { changed = edit_drag_scalar(addr(v), (id = "u64", text = title, speed = 1.0f, v_min = 0ul, v_max = 0ul, format = "%llu")) }
            if (changed) { SetParam(p, ConfigValue(_uint64 = v)) }

        } elif (p.value_type == "f32x2") {
            var v = float2(0.0f)
            if (field.value is _float2) { v = field.value as _float2 }
            var changed = false
            unsafe { changed = edit_drag_float2(addr(v), (id = "f32x2", text = title, speed = 0.01f)) }
            if (changed) { SetParam(p, ConfigValue(_float2 = v)) }

        } elif (p.value_type == "f32x3") {
            var v = float3(0.0f)
            if (field.value is _float3) { v = field.value as _float3 }
            var changed = false
            unsafe { changed = edit_drag_float3(addr(v), (id = "f32x3", text = title, speed = 0.01f)) }
            if (changed) { SetParam(p, ConfigValue(_float3 = v)) }

        } elif (p.value_type == "f32x4") {
            var v = float4(0.0f)
            if (field.value is _float4) { v = field.value as _float4 }
            var changed = false
            unsafe { changed = edit_drag_float4(addr(v), (id = "f32x4", text = title, speed = 0.01f)) }
            if (changed) { SetParam(p, ConfigValue(_float4 = v)) }

        } elif (p.value_type == "i32x2") {
            var v = int2(0)
            if (field.value is _int2) { v = field.value as _int2 }
            var changed = false
            unsafe { changed = edit_drag_int2(addr(v), (id = "i32x2", text = title, speed = 1.0f)) }
            if (changed) { SetParam(p, ConfigValue(_int2 = v)) }

        } elif (p.value_type == "i32x3") {
            var v = int3(0)
            if (field.value is _int3) { v = field.value as _int3 }
            var changed = false
            unsafe { changed = edit_drag_int3(addr(v), (id = "i32x3", text = title, speed = 1.0f)) }
            if (changed) { SetParam(p, ConfigValue(_int3 = v)) }

        } elif (p.value_type == "i32x4") {
            var v = int4(0)
            if (field.value is _int4) { v = field.value as _int4 }
            var changed = false
            unsafe { changed = edit_drag_int4(addr(v), (id = "i32x4", text = title, speed = 1.0f)) }
            if (changed) { SetParam(p, ConfigValue(_int4 = v)) }

        } elif (p.value_type == "f32m3x3" || p.value_type == "f32m3x4" || p.value_type == "f32m4x4") {
            text("{title}: matrix editing is not implemented yet")
        } elif (p.value_type == "u32x2" || p.value_type == "u32x3" || p.value_type == "u32x4") {
            text("{title}: uint vector editing is not implemented yet")
        } else {
            text("{title}: no editor for this type")
        }
    }
}

// ---------------------------------------------------------------------------
// Действия
// ---------------------------------------------------------------------------

def private RequestSchema() {
    if (material.shader == 0ul) {
        schema_pending = false
        return
    }

    // null/пустая строка — шейдер ещё грузится, повторим на следующем кадре.
    let schema_text = GetMaterialSchema(material.shader)
    if (empty(schema_text)) {
        schema_pending = true
        status = "waiting for shader {material.shader:d}"
        return
    }

    if (ParseSchema(schema_text, schema)) {
        for (p in schema) {
            EnsureParam(p)
        }
        status = "schema: {length(schema)} params"
    } else {
        status = "failed to parse material schema"
    }
    schema_pending = false
}

// Открывает материал по пути. guid — id, под которым материал живёт в кеше ресурсов;
// asset_ref — авторская ссылка, которая уйдёт в компонент при Apply: путь для авторитетного
// файла, "guid:<id>" для импортированного.
def private LoadMaterialAt(path : string; guid : uint64; asset_ref : string) {
    material_path = path
    InitInputText(MATERIAL_PATH, path)

    let text = fio::fread(path)
    if (empty(text)) {
        status = "not found: {path}"
        material_loaded = false
        return
    }

    var error = ""
    if (!ParseMaterial(text, material, error)) {
        status = "parse error: {error}"
        material_loaded = false
        return
    }

    material_loaded = true
    material_readonly = IsImportedAsset(guid)
    material_guid = guid
    material_ref = asset_ref
    clear(schema)
    RequestSchema()
    if (material_readonly) {
        status = "loaded {path} (imported, read-only)"
    } else {
        status = "loaded {path}"
    }
}

def private LoadMaterial() {
    let asset_ref = material_path
    LoadMaterialAt(asset_ref, RegisterMaterialFile(asset_ref), asset_ref)
}

// Материал выбранной сущности: компонент materialRef -> id и путь из реестра -> файл.
// false — путь ещё не известен (реестр ассетов заполняется асинхронно на старте),
// панель повторит попытку на следующем кадре.
def private SyncFromSelected() : bool {
    if (selected_eid == INVALID_ENTITY_ID) {
        return true
    }

    var asset_ref = ""
    var has_material = false
    query(selected_eid) $(let materialRef : string) {
        asset_ref = materialRef
        has_material = true
    }
    if (!has_material || empty(asset_ref)) {
        status = "entity [{selected_eid.id:d}] has no material"
        return true
    }

    let guid = ResolveAssetRef(asset_ref)
    if (guid == 0ul) {
        // Реестр заполняется асинхронно: не считаем ссылку битой, повторим на следующем кадре.
        status = "material '{asset_ref}': unresolved reference"
        return false
    }

    // Канонизируем ссылку: авторитетный материал — путь, импортированный — guid.
    // Так Apply переводит старый guid на путь, а не тиражирует его дальше.
    let canonical = AssetRefOf(guid)
    var resolved_ref = asset_ref
    if (!empty(canonical)) {
        resolved_ref = canonical
    }

    let path = GetAssetFilePath(guid)
    if (empty(path)) {
        status = "material {guid:d}: path is unknown"
        return false
    }
    if (path == material_path && guid == material_guid && material_loaded) {
        material_ref = resolved_ref
        return true
    }
    LoadMaterialAt(path, guid, resolved_ref)
    return true
}

// Правка применяется в живой Material сразу: SetParamRaw пишет в UBO без пересоздания
// GPU-ресурсов, поэтому изменения видны в этом же кадре.
def private LiveApply() {
    if (!material_loaded || material_readonly || material_guid == 0ul) {
        return
    }
    if (!ApplyMaterialConfigText(material_guid, WriteMaterial(material))) {
        status = "live apply: material ids not loaded yet"
    }
}

// Значения read-only материала показываем текстом, без виджетов.
def private ValueToString(val : ConfigValue) : string {
    if (val is _float) { return "{val as _float}" }
    if (val is _int) { return "{val as _int}" }
    if (val is _uint) { return "{val as _uint:d}" }
    if (val is _int64) { return "{val as _int64:d}" }
    if (val is _uint64) { return "{val as _uint64:d}" }
    if (val is _float2) {
        let v = val as _float2
        return "{v.x} {v.y}"
    }
    if (val is _float3) {
        let v = val as _float3
        return "{v.x} {v.y} {v.z}"
    }
    if (val is _float4) {
        let v = val as _float4
        return "{v.x} {v.y} {v.z} {v.w}"
    }
    if (val is _int2) {
        let v = val as _int2
        return "{v.x} {v.y}"
    }
    if (val is _int3) {
        let v = val as _int3
        return "{v.x} {v.y} {v.z}"
    }
    if (val is _int4) {
        let v = val as _int4
        return "{v.x} {v.y} {v.z} {v.w}"
    }
    return "<complex>"
}

def private SaveMaterial() {
    if (!material_loaded) {
        status = "nothing to save"
        return
    }
    if (material_readonly) {
        status = "read-only: imported material is generated from the source asset"
        return
    }
    let text = WriteMaterial(material)
    if (!fio::fwrite(material_path, text)) {
        status = "write failed: {material_path}"
        return
    }
    // Материал уже живой (LiveApply), перезагружать ресурс не нужно: иначе указатель в
    // компоненте сущности указывал бы на уничтоженный Material.
    LiveApply()
    status = "saved {material_path}"
}

def private ApplyMaterial() {
    if (!material_loaded) {
        status = "load a material first"
        return
    }
    if (selected_eid == INVALID_ENTITY_ID) {
        status = "select an entity in the hierarchy first"
        return
    }

    // Указатель на материал мог остаться от прежнего материала: сбрасываем его, чтобы
    // Render.start_load подставил материал по новому guid.
    var has_pointer = false
    query(selected_eid) $(let material : TryMat?) {
        has_pointer = true
    }

    update_entity(selected_eid) @(eid, cmp) {
        cmp |> set("materialRef", material_ref)
        if (has_pointer) {
            decs::remove(cmp, "material")
        }
    }
    commit()
    status = "applied to entity [{selected_eid.id:d}], ref {material_ref}"
}

// ---------------------------------------------------------------------------
// Панель
// ---------------------------------------------------------------------------

def DrawMaterialEditor() {
    window(MATERIAL_EDITOR_WIN, (text = "Material", closable = false, flags = ImGuiWindowFlags.None)) {
        if (!material_path_initialized) {
            InitInputText(MATERIAL_PATH, material_path)
            material_path_initialized = true
        }

        input_text(MATERIAL_PATH, (text = "File", flags = ImGuiInputTextFlags.None))
        material_path = MATERIAL_PATH.value

        if (!auto_load_done) {
            auto_load_done = true
            LoadMaterial()
        }

        if (button(MATERIAL_LOAD_BTN, (text = "Load"))) {
            LoadMaterial()
        }
        same_line()
        if (button(MATERIAL_SAVE_BTN, (text = "Save"))) {
            SaveMaterial()
        }
        same_line()
        if (button(MATERIAL_APPLY_BTN, (text = "Apply to selected"))) {
            ApplyMaterial()
        }
        same_line()
        checkbox(follow_state, "Follow selection")

        if (follow_state.value && selected_eid != last_synced_eid) {
            if (SyncFromSelected()) {
                last_synced_eid = selected_eid
            }
        }

        if (schema_pending) {
            RequestSchema()
        }

        separator()
        if (material_loaded) {
            text("name: {material.name}")
            text("shader: {material.shader:d}")
            text("params: {length(material.params)}")
            if (material_readonly) {
                text("read-only: generated by the asset importer (artifacts/)")
            }
            separator()
            for (p in schema) {
                DrawParam(p)
            }
            if (schema.empty()) {
                text("shader has no material parameters or the schema is still loading")
            }
        } else {
            text("load a .mat file")
        }

        if (!status.empty()) {
            separator()
            text("{status}")
        }
    }
}
