options gen2
options relaxed_assign = false
options gc

require tryeditor/ImportSettings
require TryEditor
require daslib/strings_boost
require daslib/enum_trait
require daslib/fio

require imgui
require imgui_app_headless
require imgui/imgui_boost_runtime
require imgui/imgui_containers_builtin
require imgui/imgui_widgets_builtin
require imgui/imgui_boost_v2

//! Import settings are a ConfigFormat document shared by the C++ importers and the editor
//! form. This checks both directions of that contract and renders the generated form in a
//! headless ImGui context: the fields are expanded by `apply`, and a widget call site that
//! renders twice per frame panics, so the render pass is the regression check for the form.

let kTextureMeta = "header: AssetMetaHeader = \{\n\tguid: u64 = 14429000468236573201\n\timporter_type: string = \"TextureImporter\"\n\tsub_assets: u64[] = [\n\t]\n\}\nsettings: TextureImportSettings = \{\n\tformat: string = \"rgba8_srgb\"\n\tgenerate_mips: bool = true\n\tmax_size: u32 = 0\n\tfilter: string = \"linear\"\n\twrap: string = \"repeat\"\n\}\n"

let kFontMeta = "header: AssetMetaHeader = \{\n\tguid: u64 = 7732110565556845333\n\timporter_type: string = \"FontImporter\"\n\tsub_assets: u64[] = [\n\t]\n\}\nsettings: FontBakeSettings = \{\n\tem_size: f32 = 40\n\tpx_range: f32 = 4\n\tpadding: u32 = 4\n\tatlas_width: u32 = 1024\n\tatlas_height: u32 = 1024\n\tpixel_dist_scale: f32 = 32\n\tonedge_value: u8 = 128\n\tranges: string = \"0x20-0x7E,0x400-0x4FF\"\n\}\n"

let kGltfMeta = "header: AssetMetaHeader = \{\n\tguid: u64 = 3903434602548670449\n\timporter_type: string = \"GltfImporter\"\n\tsub_assets: u64[] = [\n\t\t17036954862819815981\n\t\t12632691863048436524\n\t]\n\}\nsettings: GltfImportSettings = \{\n\textract_materials: bool = true\n\tup_axis: string = \"Z\"\n\}\n"

let kSlangMeta = "header: AssetMetaHeader = \{\n\tguid: u64 = 13732156250205846749\n\timporter_type: string = \"SlangImporter\"\n\tsub_assets: u64[] = [\n\t]\n\}\nsettings: SlangImportSettings = \{\n\}\n"

var g_ok = true

def private Check(condition : bool; what : string) {
    if (!condition) {
        print("FAIL: {what}\n")
        g_ok = false
    }
}

var g_texture <- ImportMeta()
var g_font <- ImportMeta()
var g_gltf <- ImportMeta()
var g_slang <- ImportMeta()

def private LoadOrFail(text : string; var meta : ImportMeta; what : string) : bool {
    var error = ""
    let loaded = LoadMeta(text, meta, error)
    Check(loaded, "{what} loads ({error})")
    return loaded
}

def private RoundTrip(var meta : ImportMeta; what : string) {
    let first = SaveMetaText(meta)
    Check(!empty(first), "{what} serializes")
    let second = SaveMetaText(meta)
    // Writing must not consume the form state: SaveMetaText runs every frame to detect edits.
    Check(first == second, "{what} serializes twice the same")

    var parsed <- ImportMeta()
    var error = ""
    if (LoadMeta(first, parsed, error)) {
        Check(SaveMetaText(parsed) == first, "{what} round-trips")
    } else {
        Check(false, "{what} re-parses ({error})")
    }
}

//! The committed `.meta` is written by the C++ codec (checked byte for byte by
//! `tools/agent/gltf-probe.sh`). Re-writing it through the daslang mirror must give the same
//! text: a field the mirror does not know would be silently dropped by the form on the next
//! save, so this is the check that keeps the C++ schema and the editor form in step.
def private CheckMirrorKeepsFile(path : string) {
    let raw = fread(path)
    if (empty(raw)) {
        Check(false, "{path} can be read")
        return
    }
    var loaded <- ImportMeta()
    var error = ""
    if (!LoadMeta(raw, loaded, error)) {
        Check(false, "{path} parses in the editor mirror ({error})")
        return
    }
    let written = SaveMetaText(loaded)
    if (written != raw) {
        print("  mirror wrote:\n{written}\n  file has:\n{raw}\n")
    }
    Check(written == raw, "{path} is written back unchanged by the editor mirror")
}

def private RenderForm(var meta : ImportMeta; frames : int) {
    for (frame in range(frames)) {
        begin_frame()
        NewFrame()
        window(TEST_FORM_WIN, (text = "import settings", closable = false, flags = ImGuiWindowFlags.None)) {
            DrawMetaSettings("test", meta)
        }
        Render()
        end_of_frame()
    }
}

[export]
def main() : int {
    if (LoadOrFail(kTextureMeta, g_texture, "texture meta")) {
        Check(g_texture.header.guid == 14429000468236573201ul, "texture meta keeps the guid")
        Check(g_texture.texture.format == "rgba8_srgb", "texture meta keeps the format")
        Check(g_texture.texture.generate_mips, "texture meta keeps the mip policy")
        Check(g_texture.texture.wrap == "repeat", "texture meta keeps the wrap")
        RoundTrip(g_texture, "texture meta")
    }
    if (LoadOrFail(kFontMeta, g_font, "font meta")) {
        Check(g_font.font.em_size == 40.0f, "font meta keeps the em size")
        Check(g_font.font.atlas_width == 1024u, "font meta keeps the atlas width")
        Check(g_font.font.onedge_value == 128u8, "font meta keeps onedge")
        Check(!empty(g_font.font.ranges), "font meta keeps the ranges")
        RoundTrip(g_font, "font meta")
    }
    if (LoadOrFail(kGltfMeta, g_gltf, "gltf meta")) {
        Check(g_gltf.gltf.extract_materials, "gltf meta keeps extract_materials")
        Check(g_gltf.gltf.up_axis == GltfUpAxis.Z, "gltf meta keeps the up axis")
        Check(length(g_gltf.header.sub_assets) == 2, "gltf meta keeps the sub-assets")
        let saved = SaveMetaText(g_gltf)
        Check(contains(saved, "17036954862819815981"), "saved gltf meta keeps the first sub-asset")
        Check(contains(saved, "up_axis: string = \"Z\""), "saved gltf meta keeps the up axis")
        RoundTrip(g_gltf, "gltf meta")
    }
    if (LoadOrFail(kSlangMeta, g_slang, "shader meta")) {
        Check(g_slang.header.importer_type == "SlangImporter", "shader meta keeps the importer")
        let saved = SaveMetaText(g_slang)
        Check(contains(saved, "settings: SlangImportSettings = \{"), "shader meta writes an empty settings block")
        RoundTrip(g_slang, "shader meta")
    }

    // Broken documents are rejected with a reason, never half-edited.
    var broken <- ImportMeta()
    var broken_error = ""
    Check(!LoadMeta("settings: GltfImportSettings = \{\n\}\n", broken, broken_error), "a meta without a header is rejected")
    Check(!empty(broken_error), "the rejection names the reason")
    Check(!LoadMeta("header: AssetMetaHeader = \{\n\tguid: u64 = 1\n\timporter_type: string = \"FutureImporter\"\n\tsub_assets: u64[] = [\n\t]\n\}\n", broken, broken_error), "an unknown importer is rejected")
    Check(KnownImporter("TextureImporter") && KnownImporter("FontImporter") && KnownImporter("GltfImporter") && KnownImporter("SlangImporter"), "all importers have a schema")
    Check(!KnownImporter("FutureImporter"), "an unknown importer has no schema")

    // The form itself: every type renders without a duplicate widget path, and a click on the
    // enum combo writes the value back (the regression this check exists for).
    CreateContext(null)
    imgui_headless_init_fonts()
    imgui_headless_set_display_size(520.0f, 320.0f)
    RenderForm(g_texture, 3)
    RenderForm(g_font, 3)
    RenderForm(g_gltf, 3)
    RenderForm(g_slang, 3)

    // Every committed .meta must round-trip through the mirror unchanged.
    CheckMirrorKeepsFile("game/assets/lance.glb.meta")
    CheckMirrorKeepsFile("game/assets/TestWeapon/9_mm.glb.meta")
    CheckMirrorKeepsFile("game/assets/ui/logo.png.meta")
    CheckMirrorKeepsFile("game/assets/fonts/GoogleSans-VariableFont_GRAD,opsz,wght.ttf.meta")
    CheckMirrorKeepsFile("engine_content/shaders/Standard.slang.meta")

    // The combo's labels come from the enum itself, so the panel needs no per-field list.
    var labels : array<string>
    for (axis in each(GltfUpAxis.Y)) {
        labels |> push(string(axis))
    }
    Check(length(labels) == 3, "the up-axis enum has three values")
    Check(labels[0] == "Y" && labels[1] == "Z" && labels[2] == "X", "the up-axis labels are the .meta names")
    DestroyContext(null)

    if (!g_ok) {
        return 1
    }
    print("IMPORT SETTINGS OK\n")
    return 0
}
