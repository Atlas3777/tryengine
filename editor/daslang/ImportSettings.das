options gen2
options relaxed_assign = false

module ImportSettings shared public

//! Import settings of an asset, as a ConfigFormat document.
//!
//! `.meta` is the source of truth for an import: re-baking a texture or a font means editing
//! `settings` (format, mips, ranges, ...). Both sides read the same field names, so the C++
//! importers and the editor form are mirrors of one schema:
//!
//!   The C++ side is editor/include/editor/import/*.hpp: each settings struct carries its
//!   MetaSettingsCodec specialization there, and this file mirrors the field names.
//!
//! The form is generated from the struct with `apply`, so a field added here shows up in the
//! panel without new widget code. Annotations the form understands:
//!   @caption = "Max size" — field caption, defaults to the field name;
//!   @choices = "a,b,c"    — draw a combo instead of a free-text string;
//!   @multiline            — a taller input for long strings (font codepoint ranges);
//!   @hide                 — do not draw the field at all.
//!
//! An enum-typed field needs no annotation at all: the form draws it as a combo of the value
//! names it reads from the type itself, and the picked value is written back as the enum. That is
//! why `GltfUpAxis` is bound as a native enum in the editor module instead of being a string.
//!
//! The C++ codec stays the schema. `tools/tests/import_settings.das` re-writes every committed
//! `.meta` through this mirror and requires the same text back, so a field added in C++ and
//! forgotten here fails the test instead of being dropped by the next save.
//!
//! Identity is not editable: `guid`, `importer_type` and `sub_assets` are the asset's address
//! in materials, scenes and the pak.

require tryengine/ConfigFormat
require tryengine/ConfigFormatBoost

// Native enum of the glTF import: bound in editor/src/Module_TryEditor.cpp.
require TryEditor

require imgui
require imgui/imgui_widgets_builtin
require imgui/imgui_containers_builtin
require imgui/imgui_layout_builtin
require imgui/imgui_id_builtin
require imgui/imgui_boost_runtime
require imgui/imgui_boost_v2

require daslib/apply
require daslib/enum_trait
require daslib/rtti
require strings
require daslib/strings_boost
require math

struct public AssetMetaHeader {
    guid          : uint64
    importer_type : string
    sub_assets    : array<uint64>
}

// Mirrors of tryeditor::TextureImportSettings.
struct public TextureImportSettings {
    @caption = "Format"
    @choices = "rgba8,rgba8_srgb,r8,rg8,bc1,bc1_srgb,bc3,bc3_srgb,bc7,bc7_srgb"
    format        : string = "rgba8_srgb"
    @caption = "Generate mips"
    generate_mips : bool = true
    @caption = "Max size (0 = source)"
    max_size      : uint = 0u
    @caption = "Filter"
    @choices = "linear,nearest"
    filter        : string = "linear"
    @caption = "Wrap"
    @choices = "repeat,clamp,mirror"
    wrap          : string = "repeat"
}

// Mirrors of tryeditor::FontBakeSettings.
struct public FontBakeSettings {
    @caption = "Em size (px)"
    em_size          : float = 40.0f
    @caption = "Distance range (px)"
    px_range         : float = 4.0f
    @caption = "Glyph padding (px)"
    padding          : uint = 4u
    @caption = "Atlas width"
    atlas_width      : uint = 1024u
    @caption = "Atlas height"
    atlas_height     : uint = 1024u
    @caption = "Distance scale"
    pixel_dist_scale : float = 32.0f
    @caption = "On-edge value"
    onedge_value     : uint8 = 128u8
    @caption = "Codepoint ranges"
    @multiline
    ranges           : string = "0x20-0x7E,0xA0-0xFF,0x400-0x4FF,0x2010-0x2027,0x2030,0x2039,0x203A,0x20AC"
}

// Mirrors of tryeditor::GltfImportSettings.
struct public GltfImportSettings {
    @caption = "Extract materials"
    extract_materials : bool = false
    // Axis of the source file that points up. glTF is Y-up while the engine is Z-up, so Y (the
    // glTF norm) turns a model upright, Z keeps a file that is already Z-up and X converts a
    // file whose up axis is X. The values come from the native `GltfUpAxis` enum, and the form
    // draws any enum field as a combo of its value names, so a new axis needs no widget work.
    @caption = "Up axis"
    up_axis           : GltfUpAxis = GltfUpAxis.Y
}

struct private TextureMetaDoc {
    header   : AssetMetaHeader
    settings : TextureImportSettings = TextureImportSettings()
}

struct private FontMetaDoc {
    header   : AssetMetaHeader
    settings : FontBakeSettings = FontBakeSettings()
}

struct private GltfMetaDoc {
    header   : AssetMetaHeader
    settings : GltfImportSettings = GltfImportSettings()
}

struct private HeaderMetaDoc {
    header : AssetMetaHeader
}

//! One .meta, with the settings block decoded into the mirror of its importer type.
struct public ImportMeta {
    header  : AssetMetaHeader
    texture : TextureImportSettings = TextureImportSettings()
    font    : FontBakeSettings = FontBakeSettings()
    gltf    : GltfImportSettings = GltfImportSettings()
}

// Empty settings block of a shader. A separate type keeps `settings: SlangImportSettings = { }`
// in the document, which is what the C++ codec expects.
struct private SlangMetaSettings {}

struct private FieldStyle {
    caption   : string
    choices   : string
    multiline : bool
    hidden    : bool
}

var private g_bool : table<string; ToggleState>
var private g_text : table<string; InputTextState>
var private g_combo : table<string; ComboState>

//! True for an importer the panel knows how to draw. An unknown one keeps its .meta intact:
//! the panel refuses to edit it instead of silently dropping fields it does not understand.
def public KnownImporter(importer_type : string) : bool {
    if (importer_type == "TextureImporter" || importer_type == "FontImporter" || importer_type == "GltfImporter") {
        return true
    }
    return importer_type == "SlangImporter"
}

//! True when the importer has no settings block (a shader still owns its identity).
def public ImporterHasSettings(importer_type : string) : bool {
    return !(importer_type == "SlangImporter")
}

//! Clears widget state so a reloaded .meta does not show stale buffers of the previous asset.
def public ClearFormState() {
    clear(g_bool)
    clear(g_text)
    clear(g_combo)
}

def private InitText(var state : InputTextState; val : string) {
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
// Document
// ---------------------------------------------------------------------------

def public LoadMeta(text : string; var meta : ImportMeta; var error : string&) : bool {
    error = ""
    if (empty(text)) {
        error = "the .meta is empty"
        return false
    }

    var root <- read_config(text, error)
    if (!empty(error)) {
        return false
    }
    if (root == null || !(root.value is _object)) {
        error = "the root of .meta is not an object"
        return false
    }

    var header_doc <- from_CF(root, default<HeaderMetaDoc>)
    if (header_doc.header.guid == 0ul || empty(header_doc.header.importer_type)) {
        error = "header.guid and header.importer_type are required"
        return false
    }
    meta.header <- header_doc.header

    let importer = meta.header.importer_type
    if (importer == "TextureImporter") {
        var doc <- from_CF(root, default<TextureMetaDoc>)
        meta.texture <- doc.settings
    } elif (importer == "FontImporter") {
        var doc <- from_CF(root, default<FontMetaDoc>)
        meta.font <- doc.settings
    } elif (importer == "GltfImporter") {
        var doc <- from_CF(root, default<GltfMetaDoc>)
        meta.gltf <- doc.settings
    } elif (importer != "SlangImporter") {
        error = "no editor schema for importer '{importer}'"
        return false
    }
    return true
}

//! The whole .meta as ConfigFormat. Every field the editor knows is written, so a value that
//! differs from the struct default is never left implicit. Settings fields the mirror does not
//! know are dropped: the schema is the mirror, and C++ and daslang must be updated together.
def private PutSettings(var root : table<string; StructField?>; var meta : ImportMeta) : bool {
    if (meta.header.importer_type == "TextureImporter") {
        var settings = CF(meta.texture)
        if (settings == null) {
            return false
        }
        settings.name = "settings"
        settings.typeName = "TextureImportSettings"
        root["settings"] = settings
        return true
    }
    if (meta.header.importer_type == "FontImporter") {
        var settings = CF(meta.font)
        if (settings == null) {
            return false
        }
        settings.name = "settings"
        settings.typeName = "FontBakeSettings"
        root["settings"] = settings
        return true
    }
    if (meta.header.importer_type == "GltfImporter") {
        var settings = CF(meta.gltf)
        if (settings == null) {
            return false
        }
        settings.name = "settings"
        settings.typeName = "GltfImportSettings"
        root["settings"] = settings
        return true
    }
    if (meta.header.importer_type == "SlangImporter") {
        var emptySettings : SlangMetaSettings
        var settings = CF(emptySettings)
        if (settings == null) {
            return false
        }
        settings.name = "settings"
        settings.typeName = "SlangImportSettings"
        root["settings"] = settings
        return true
    }
    return false
}

def public SaveMetaText(var meta : ImportMeta) : string {
    var root : table<string; StructField?>

    var header = CF(meta.header)
    if (header == null) {
        return ""
    }
    header.name = "header"
    header.typeName = "AssetMetaHeader"
    root["header"] = header

    if (!PutSettings(root, meta)) {
        return ""
    }
    return write_config(CF(root))
}

// ---------------------------------------------------------------------------
// Form
// ---------------------------------------------------------------------------

def private ParseStyle(name : string; annotations : array<tuple<name : string; data : RttiValue>>) : FieldStyle {
    var style : FieldStyle
    style.caption = name
    for (ann in annotations) {
        if (ann.name == "caption" && ann.data is tString) {
            style.caption = ann.data as tString
        } elif (ann.name == "choices" && ann.data is tString) {
            style.choices = ann.data as tString
        } elif (ann.name == "multiline") {
            style.multiline = ann.data is tBool ? (ann.data as tBool) : true
        } elif (ann.name == "hide") {
            style.hidden = ann.data is tBool ? (ann.data as tBool) : true
        }
    }
    return <- style
}

[generic]
def private DrawField(key : string; style : FieldStyle; var field : auto(TT)&) {
    static_if (typeinfo stripped_typename(type<TT>) == "bool") {
        g_bool[key].value = field
        if (checkbox(g_bool[key], (text = style.caption))) {
            field = g_bool[key].value
        }
    } static_elif (typeinfo stripped_typename(type<TT>) == "string") {
        if (!empty(style.choices)) {
            var inscope items : array<string>
            items := split(style.choices, ",")
            for (i in range(length(items))) {
                items[i] = trim(items[i])
            }

            var selected = 0
            var found = false
            for (i in range(length(items))) {
                if (items[i] == field) {
                    selected = i
                    found = true
                }
            }
            g_combo[key].value = selected
            if (combo(g_combo[key], (text = style.caption, items = items))) {
                field = items[g_combo[key].value]
            }
            if (!found) {
                text("'{field}' is not one of the allowed values")
            }
        } else {
            if (!key_exists(g_text, key) || g_text[key].value != field) {
                InitText(g_text[key], field)
            }
            var edited = false
            if (style.multiline) {
                edited = input_text_multiline(g_text[key], (text = style.caption, size = float2(0.0f, 64.0f),
                                                            flags = ImGuiInputTextFlags.AllowTabInput))
            } else {
                edited = input_text(g_text[key], (text = style.caption, flags = ImGuiInputTextFlags.None))
            }
            if (edited) {
                field = g_text[key].value
            }
        }
    } static_elif (typeinfo is_enum(type<TT>)) {
        // Enum-поле — комбо из имён значений: подписи и порядок берутся у самого типа, поэтому
        // новое значение не требует ни строки в форме, ни `@choices` в структуре.
        var inscope labels : array<string>
        var selected = 0
        var index = 0
        for (value in each(field)) {
            if (value == field) {
                selected = index
            }
            labels |> push(string(value))
            index++
        }
        g_combo[key].value = selected
        if (combo(g_combo[key], (text = style.caption, items = labels))) {
            var picked = 0
            for (value in each(field)) {
                if (picked == g_combo[key].value) {
                    field = value
                }
                picked++
            }
        }
    } else {
        var speed = 1.0f
        static_if (typeinfo stripped_typename(type<TT>) == "float" || typeinfo stripped_typename(type<TT>) == "double") {
            speed = 0.01f
        }
        static_if (typeinfo stripped_typename(type<TT>) == "uint64") {
            unsafe {
                edit_drag_scalar(addr(field), (id = "drag", text = style.caption, speed = speed,
                                               v_min = 0ul, v_max = 0ul, format = "%llu"))
            }
        } elif (typeinfo stripped_typename(type<TT>) == "uint") {
            unsafe {
                edit_drag_scalar(addr(field), (id = "drag", text = style.caption, speed = speed,
                                               v_min = 0u, v_max = 0u, format = "%u"))
            }
        } else {
            unsafe {
                edit_drag_scalar(addr(field), (id = "drag", text = style.caption, speed = speed))
            }
        }
    }
}

[generic]
def private DrawFields(prefix : string; var value : auto(TT)&) {
    apply(value) $(name : string; var field; annotations) {
        var style <- ParseStyle(name, annotations)
        if (!style.hidden) {
            with_id(name) {
                DrawField("{prefix}/{name}", style, field)
            }
        }
    }
}

//! Draws the settings block of `meta` as widgets. `prefix` keeps widget state apart when the
//! panel shows different assets over its lifetime.
def public DrawMetaSettings(prefix : string; var meta : ImportMeta) {
    let importer = meta.header.importer_type
    if (importer == "TextureImporter") {
        DrawFields("{prefix}/texture", meta.texture)
    } elif (importer == "FontImporter") {
        DrawFields("{prefix}/font", meta.font)
    } elif (importer == "GltfImporter") {
        DrawFields("{prefix}/gltf", meta.gltf)
    } elif (importer == "SlangImporter") {
        text("A shader has no import settings.")
    } else {
        text("No editor schema for importer '{importer}'.")
    }
}
