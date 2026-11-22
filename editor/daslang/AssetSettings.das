options gen2
options relaxed_assign = false
options _allow_imgui_legacy = true

module AssetSettings shared public

//! Read-only view of the import settings of the asset selected in the Asset Browser.
//!
//! The .meta is the source of truth for an import: re-baking a texture or a font means editing
//! that file (format, mips, ranges, ...) so the import stamp no longer matches and the importer
//! runs again. This panel shows exactly what the importer will read, so "why is my texture BC7"
//! or "how many ranges did the font bake" is one click away. It edits nothing: a settings editor
//! needs a single-asset reimport path, which the asset pipeline does not expose yet.

require tryengine/Profiling
require tryeditor/AssetBrowser
require TryEditor
require math
require imgui
require imgui/imgui_widgets_builtin
require imgui/imgui_containers_builtin

var private g_meta_text : string = ""
var private g_meta_path : string = ""
var private g_reload : bool = true

[profile]
def public DrawAssetSettings() {
    window(ASSET_SETTINGS_WND, (text = "Import Settings", closable = false, flags = ImGuiWindowFlags.None)) {
        let selection = GetSelectedFile()
        if (!selection.valid) {
            text("Select a file in the Asset Browser.")
        } else {
            text("path: {selection.path}")
            if (button(ASSET_SETTINGS_RELOAD, (text = "Reload"))) {
                g_reload = true
            }

            if (g_reload || selection.path != g_meta_path) {
                g_reload = false
                g_meta_path = selection.path
                g_meta_text = DescribeAssetMeta(selection.path)
            }

            if (empty(g_meta_text)) {
                text("No .meta: an authoritative file (content/data) has no import settings.")
            } else {
                text_wrapped(ASSET_SETTINGS_BODY, (text = g_meta_text))
            }
        }
    }
}
