options gen2
options relaxed_assign = false
options _allow_imgui_legacy = true

module Material shared public

// Материал — это данные, заполняющие шейдерный шаблон. Набор полей задаёт рефлексия
// Slang, поэтому здесь нет ни albedo_color, ни roughness: редактор получает схему
// `params` из C++ (GetMaterialSchema) и рисует поля по их типу.
//
// C++-зеркало сериализуемой структуры — engine_source/graphics/include/engine/graphics/MaterialConfig.hpp.
// Обе стороны пишут один и тот же ConfigFormat.

require tryengine/ConfigFormat
require tryengine/ConfigFormatBoost
require tryengine/Model
require tryengine/Types
require tryengine/Profiling
require tryengine/EcsMacro
require tryeditor/Hierarchy
require tryeditor/AssetPicker
require tryeditor/AssetBrowser
require tryeditor/FileSystemUtils
require tryeditor/MaterialCompat

require imgui
require imgui/imgui_widgets_builtin
require imgui/imgui_containers_builtin
require imgui/imgui_layout_builtin
require imgui/imgui_scope_builtin
require imgui/imgui_id_builtin
require imgui/imgui_boost_runtime
require imgui/imgui_boost_v2

require daslib/fio
require daslib/decs
require strings
require daslib/strings_boost
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

var private g_tex_pick : table<string; ClickState>
var private g_tex_clear : table<string; ClickState>
var private g_tex_preview : table<string; ImageState>

var private material_path = ""
var private material : MaterialConfig
var private schema : array<MaterialParam>
var private schema_attributes : uint = 0u
var private material_loaded = false
var private schema_pending = false
var private status = ""
var private material_guid : uint64 = 0ul
var private material_ref = ""
var private material_readonly = false
var private last_synced_eid : EntityId = INVALID_ENTITY_ID
var private last_synced_ref = ""
var private follow_state = ToggleState(value = true)
var private last_asset_generation : uint64 = 0ul
// Выбор в Asset Browser — отдельный вход в панель: serial растёт на каждый клик, поэтому
// повторный выбор того же .mat снова его открывает.
var private last_browser_serial : uint64 = 0ul

// Диалог создания материала.
var private create_popup = PopupState(open = false)
var private create_name <- InputTextState()
var private create_name_text = ""
var private create_shader_names : array<string>
var private create_shader_guids : array<uint64>
var private create_shader_combo <- ComboState()
var private create_status = ""

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

    // CF для таблицы забирает её по move. Без промежуточной копии WriteMaterial
    // опустошал бы material.params/material.textures, и следующий кадр создавал бы
    // поля заново через EnsureParam — с дефолтными значениями.
    if (length(m.params) > 0) {
        var params_copy : table<string; StructField?>
        params_copy := m.params
        var params_cf = CF(params_copy)
        if (params_cf != null) {
            params_cf.name = "params"
            params_cf.typeName = "Params"
            root["params"] = params_cf
        }
    }
    if (length(m.textures) > 0) {
        var textures_copy : table<string; StructField?>
        textures_copy := m.textures
        var textures_cf = CF(textures_copy)
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

def private ParseSchemaText(text : string; var out : array<MaterialParam>; var attributes : uint&) : bool {
    clear(out)
    attributes = 0u
    if (empty(text)) {
        return false
    }

    var error = ""
    var root <- read_config(text, error)
    if (length(error) > 0 || root == null || !(root.value is _object)) {
        return false
    }

    var obj <- root.value as _object
    attributes = GetUInt(obj, "attributes", 0u)

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

def private ParseSchema(text : string; var out : array<MaterialParam>) : bool {
    var attributes = 0u
    let parsed = ParseSchemaText(text, out, attributes)
    schema_attributes = attributes
    return parsed
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

// A texture is not a number in a field: show a preview, the authored path and a picker from
// the asset tree. The .mat still stores a guid (the pak has no path table), but the user
// never types it.
def private DrawTextureParam(p : MaterialParam) {
    EnsureParam(p)
    let field = GetParamField(p)
    if (field == null) {
        with_id(p.name) {
            text("{p.name}: unsupported type {p.value_type}")
        }
        return
    }

    var guid = 0ul
    if (field.value is _uint64) { guid = field.value as _uint64 }

    let owner = "mat_tex_{p.name}"
    with_id(p.name) {
        text("{p.name}")
        same_line()
        if (guid != 0ul) {
            let preview = GetTexturePreview(guid)
            if (preview != null) {
                image(g_tex_preview[p.name], ImTextureRef(intptr(preview)), float2(32.0f, 32.0f))
                same_line()
            }
        }

        var source_path = ""
        if (guid != 0ul) {
            source_path = GetAssetSourcePath(guid)
        }
        if (empty(source_path)) {
            text("none")
        } else {
            text("{source_path}")
        }

        if (!material_readonly) {
            same_line()
            if (button(g_tex_pick[p.name], (text = "Choose..."))) {
                OpenTexturePicker(p.name, source_path, owner)
            }
            same_line()
            if (button(g_tex_clear[p.name], (text = "Clear"))) {
                SetParam(p, ConfigValue(_uint64 = 0ul))
            }
            if (DrawAssetPickerPopup(owner)) {
                SetParam(p, ConfigValue(_uint64 = AssetPickerSelectionGuid()))
            }
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

// Открывает .mat по авторскому пути: регистрирует файл в реестре (PathId) и читает его.
def private OpenMaterial(path : string) {
    LoadMaterialAt(path, RegisterMaterialFile(path), path)
}

// Авторская ссылка, которой сущность реально рисуется: оверрайд из `model.material`, а если
// он пуст — материал по умолчанию из артефакта меша. Импортированный материал отдаётся как
// `guid:<id>`: его можно смотреть и править живьём, но не назначать другой сущности.
// Панель замечает не только смену выделения, но и выбор материала через инспектор.
def private SelectedMaterialRef() : string {
    if (!is_alive(selected_eid)) {
        return ""
    }
    var asset_ref = ""
    query(selected_eid) $(let model : ModelRender) {
        asset_ref = model.material
        if (empty(asset_ref) && model.mesh != 0ul) {
            let default_material = MeshDefaultMaterial(model.mesh)
            if (default_material != 0ul) {
                let canonical = AssetRefOf(default_material)
                if (!empty(canonical)) {
                    asset_ref = canonical
                }
            }
        }
    }
    return asset_ref
}

// Материал выбранной сущности: `model` -> id (оверрайд или дефолт меша) -> путь из реестра -> файл.
// false — путь ещё не известен (реестр ассетов заполняется асинхронно на старте),
// панель повторит попытку на следующем кадре.
def private SyncFromSelected() : bool {
    if (!is_alive(selected_eid)) {
        return true
    }

    let asset_ref = SelectedMaterialRef()
    if (empty(asset_ref)) {
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

def private SelectedMeshGuid() : uint64 {
    if (!is_alive(selected_eid)) {
        return 0ul
    }
    var mesh_guid = 0ul
    query(selected_eid) $(let model : ModelRender) {
        mesh_guid = model.mesh
    }
    return mesh_guid
}

def private ApplyMaterial() {
    if (!material_loaded) {
        status = "load a material first"
        return
    }
    if (!is_alive(selected_eid)) {
        status = "select an entity in the hierarchy first"
        return
    }

    if (material_readonly) {
        // Производный материал не адресуется из юзерспейса: он приходит через дефолт меша,
        // а чтобы переиспользовать его на другой модели, материал сохраняют в data/.
        status = "read-only: an imported material is reached through the mesh default; save it under data/ to override"
        return
    }

    let mesh_guid = SelectedMeshGuid()
    if (!MaterialCompat::IsCompatible(material_path, mesh_guid)) {
        status = "not compatible with the selected mesh: missing {MaterialCompat::MissingAttributesText(material_path, mesh_guid)}"
        return
    }

    update_entity(selected_eid) @(eid, cmp) {
        var model = decs::get(cmp, Model::MODEL_FIELD, ModelRender())
        model.material = material_ref
        cmp |> set(Model::MODEL_FIELD, model)
    }
    commit()

    // Указатели ставит резолвер модели: ленивой загрузки в кадре больше нет.
    if (Model::ApplyToEntity(selected_eid)) {
        status = "applied to entity [{selected_eid.id:d}], ref {material_ref}"
    } else {
        status = "applied to entity [{selected_eid.id:d}], but the material is not loaded yet"
    }
}

// ---------------------------------------------------------------------------
// Создание материала
// ---------------------------------------------------------------------------

def private MaterialName(raw : string) : string {
    var name = trim(raw)
    if (ends_with(name, ".mat")) {
        name = trim(slice(name, 0, length(name) - 4))
    }
    return name
}

def private ShaderChoiceLabel(file_name : string; attributes : uint) : string {
    if (attributes == 0xFFFFFFFFu) {
        return "{file_name} (shader loading)"
    }
    if (attributes == 0u) {
        return "{file_name} (position only)"
    }
    return "{file_name} ({DescribeMeshAttributes(attributes)})"
}

def private AddShaderChoice(domain : StorageDomain; ref : AssetFileRef) {
    let file = TreeFile(domain, ref)
    let attributes = GetShaderRequiredAttributes(file.guid)
    if (attributes == 0xFFFFFFFEu) {
        // UI/debug pass shader: a material built on it would never draw a mesh.
        return
    }
    create_shader_names |> push(ShaderChoiceLabel(file.name, attributes))
    create_shader_guids |> push(file.guid)
}

def private RebuildShaderChoices() {
    clear(create_shader_names)
    clear(create_shader_guids)
    let slang = asset_ext_key(".slang")
    ForEachFile(StorageDomain.GameShaders, slang) $(ref) {
        AddShaderChoice(StorageDomain.GameShaders, ref)
    }
    ForEachFile(StorageDomain.EngineShaders, slang) $(ref) {
        AddShaderChoice(StorageDomain.EngineShaders, ref)
    }
    create_shader_combo.value = 0
}

def private OpenCreateDialog() {
    RebuildShaderChoices()
    create_name_text = ""
    InitInputText(create_name, "")
    create_status = ""
    open_popup(create_popup)
}

// Параметры нового материала приходят из схемы шейдера, поэтому файл сразу содержит все
// поля, которые шейдер просит, со значениями по умолчанию.
def private CreateMaterial() {
    let name = MaterialName(create_name_text)
    if (empty(name)) {
        create_status = "enter a material name"
        return
    }
    if (contains(name, "/") || contains(name, "\\") || contains(name, "\"")) {
        create_status = "a name cannot contain path separators or quotes"
        return
    }
    if (length(create_shader_guids) == 0) {
        create_status = "no shader found: import a .slang shader first"
        return
    }
    create_shader_combo.value = clamp(create_shader_combo.value, 0, length(create_shader_guids) - 1)
    let shader_guid = create_shader_guids[create_shader_combo.value]
    let path = "game/data/materials/{name}.mat"
    if (!empty(fio::fread(path))) {
        create_status = "material '{path}' already exists"
        return
    }

    var created : MaterialConfig
    created.name = name
    created.shader = shader_guid

    var inscope new_schema : array<MaterialParam>
    var attributes = 0u
    if (ParseSchemaText(GetMaterialSchema(shader_guid), new_schema, attributes)) {
        if (attributes == MaterialCompat::NOT_MESH) {
            create_status = "this shader is not a mesh material"
            return
        }
        for (p in new_schema) {
            if (p.kind == "texture") {
                EnsureParamIn(created.textures, p)
            } else {
                EnsureParamIn(created.params, p)
            }
        }
    }

    if (!fio::fwrite(path, WriteMaterial(created))) {
        create_status = "cannot write {path}"
        return
    }

    // There is no file watcher: the new .mat is in the browser only after a rescan. Selecting it
    // also marks the browser event handled, so the panel does not load the file twice.
    RefreshAssetTrees()
    SelectAssetPath(path)
    let selection = GetSelectedFile()
    if (selection.valid) {
        last_browser_serial = selection.serial
    }
    close_current_popup(create_popup)
    OpenMaterial(path)
}

def private DrawCreateDialog() {
    popup_modal(create_popup, (text = "New Material", closable = true, flags = ImGuiWindowFlags.None)) {
        input_text(create_name, (text = "Name", flags = ImGuiInputTextFlags.None))
        create_name_text = create_name.value
        if (length(create_shader_names) == 0) {
            text("No mesh shader found under game/shaders or engine_content/shaders.")
        } else {
            combo(create_shader_combo, (text = "Shader", items = create_shader_names))
            text("file: game/data/materials/{MaterialName(create_name_text)}.mat")
        }
        separator()
        with_disabled(empty(MaterialName(create_name_text)) || length(create_shader_guids) == 0) {
            if (button(CREATE_MATERIAL_OK, (text = "Create"))) {
                CreateMaterial()
            }
        }
        same_line()
        if (button(CREATE_MATERIAL_CANCEL, (text = "Cancel"))) {
            close_current_popup(create_popup)
        }
        if (!empty(create_status)) {
            text("{create_status}")
        }
    }
}

// ---------------------------------------------------------------------------
// Панель
// ---------------------------------------------------------------------------

[profile]
def DrawMaterialEditor() {
    // Reimporting a texture/shader drops the loaded materials in EditorApp. The panel
    // re-reads its .mat for the same reason: otherwise live edits would go to a retired
    // material.
    let asset_generation = AssetReimportGeneration()
    if (asset_generation != last_asset_generation) {
        last_asset_generation = asset_generation
        if (material_loaded && !material_readonly) {
            let reloaded_guid = ReloadMaterialFile(material_path)
            if (reloaded_guid != 0ul) {
                LoadMaterialAt(material_path, reloaded_guid, material_ref)
            }
        }
    }

    window(MATERIAL_EDITOR_WIN, (text = "Material", closable = false, flags = ImGuiWindowFlags.None)) {
        // Picking a .mat in the Asset Browser opens it here. `serial` counts every click, so
        // choosing the same file again re-opens it after Follow selection moved the panel away.
        let selection = GetSelectedFile()
        if (selection.valid && selection.serial != last_browser_serial && ends_with(selection.path, ".mat")) {
            last_browser_serial = selection.serial
            OpenMaterial(selection.path)
        }

        if (button(MATERIAL_NEW_BTN, (text = "New..."))) {
            OpenCreateDialog()
        }
        same_line()
        with_disabled(!material_loaded || material_readonly) {
            if (button(MATERIAL_SAVE_BTN, (text = "Save"))) {
                SaveMaterial()
            }
        }
        same_line()
        with_disabled(!material_loaded) {
            if (button(MATERIAL_APPLY_BTN, (text = "Apply to selected"))) {
                ApplyMaterial()
            }
        }
        same_line()
        checkbox(follow_state, "Follow selection")

        if (follow_state.value) {
            let current_ref = SelectedMaterialRef()
            if (selected_eid != last_synced_eid || current_ref != last_synced_ref) {
                if (SyncFromSelected()) {
                    last_synced_eid = selected_eid
                    last_synced_ref = current_ref
                }
            }
        }

        if (schema_pending) {
            RequestSchema()
        }

        separator()
        if (material_loaded) {
            text("file: {material_path}")
            text("name: {material.name}")
            let shader_path = GetAssetSourcePath(material.shader)
            if (empty(shader_path)) {
                text("shader: {material.shader:d}")
            } else {
                text("shader: {shader_path}")
            }
            if (material_readonly) {
                text("read-only: generated by the asset importer (artifacts/)")
            }

            // Compatibility with the selected entity's mesh: the same contract the
            // Inspector uses to filter its material list. Show the reason, not just a flag.
            let mesh_guid = SelectedMeshGuid()
            if (schema_attributes == MaterialCompat::NOT_MESH) {
                text("mesh: not a mesh material")
            } elif (mesh_guid != 0ul && GetMeshPointer(mesh_guid) == null) {
                text("mesh: loading")
            } elif (mesh_guid != 0ul) {
                let missing = schema_attributes & (~GetMeshAttributeFlags(mesh_guid))
                if (missing != 0u) {
                    text("mesh: missing {DescribeMeshAttributes(missing)}")
                } else {
                    text("mesh: compatible ({DescribeMeshAttributes(schema_attributes)})")
                }
            }

            var param_count = 0
            var texture_count = 0
            for (p in schema) {
                if (p.kind == "texture") {
                    texture_count++
                } else {
                    param_count++
                }
            }

            separator_text(MAT_PARAMS_SECTION, (text = "Parameters ({param_count})"))
            for (p in schema) {
                if (p.kind != "texture") {
                    DrawParam(p)
                }
            }
            if (param_count == 0) {
                text("No uniform parameters")
            }

            separator_text(MAT_TEXTURES_SECTION, (text = "Textures ({texture_count})"))
            for (p in schema) {
                if (p.kind == "texture") {
                    DrawTextureParam(p)
                }
            }
            if (texture_count == 0) {
                text("No textures")
            }
        } else {
            text("No material selected.")
            text("Pick a .mat in the Asset Browser, or create one with New...")
        }

        if (!status.empty()) {
            separator()
            text("{status}")
        }

        DrawCreateDialog()
    }
}
