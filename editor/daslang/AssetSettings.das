options gen2
options relaxed_assign = false

module AssetSettings shared public

//! Import settings of the asset selected in the Asset Browser.
//!
//! The .meta is the source of truth for an import: re-baking a texture or a font means editing
//! `settings` (format, mips, ranges, ...). The panel draws that block field by field from the
//! daslang mirror in tryeditor/ImportSettings, saves the whole document back as ConfigFormat,
//! and can trigger a single-asset reimport without restarting the editor. Reimport is
//! asynchronous: the asset pipeline writes the new artifacts in the background and posts an
//! event, then EditorApp invalidates the loaded resources.
//!
//! Identity is protected: `guid`, `importer_type` and `sub_assets` are read-only, because
//! materials, scenes and the pak address assets by them. An edited document is validated by the
//! importer's own codec before the file is written.

require tryengine/Profiling
require tryeditor/AssetBrowser
require tryeditor/ImportSettings
require TryEditor
require math
require strings
require imgui
require imgui/imgui_widgets_builtin
require imgui/imgui_containers_builtin
require imgui/imgui_layout_builtin
require imgui/imgui_scope_builtin
require imgui/imgui_boost_runtime
require imgui/imgui_boost_v2

var private g_meta <- ImportMeta()
var private g_meta_path : string = ""
var private g_saved_text : string = ""
var private g_load_error : string = ""
var private g_status : string = ""
var private g_has_meta : bool = false
var private g_reload : bool = true
var private g_generation : uint64 = 0ul

def private ReloadMeta(path : string) {
    g_meta_path = path
    g_load_error = ""
    g_status = ""
    ClearFormState()

    let raw = ReadAssetMeta(path)
    g_has_meta = !empty(raw)
    if (g_has_meta) {
        var loaded <- ImportMeta()
        if (LoadMeta(raw, loaded, g_load_error)) {
            g_meta <- loaded
            // The saved baseline is the file itself: a hand edit that only changes formatting
            // shows up as an unsaved change and Save normalizes it, instead of the panel
            // silently rewriting the file on the next unrelated edit.
            g_saved_text = raw
        } else {
            g_saved_text = ""
        }
    } else {
        g_saved_text = ""
    }
}

//! Writes the form back. `reimport` queues a re-bake of the asset right after the save.
def private Save(path : string; reimport : bool) {
    if (empty(g_saved_text)) {
        g_status = "nothing to save"
        return
    }
    let text_now = SaveMetaText(g_meta)
    if (empty(text_now)) {
        g_status = "cannot serialize this .meta"
        return
    }
    let error = SaveAssetMeta(path, text_now)
    if (!empty(error)) {
        g_status = "save failed: {error}"
        return
    }
    g_saved_text = text_now
    if (!reimport) {
        g_status = "saved {path}.meta"
        return
    }
    if (RequestReimport(path)) {
        g_status = "saved; reimport queued"
    } else {
        g_status = "saved; reimport rejected"
    }
}

[profile]
def public DrawAssetSettings() {
    window(ASSET_SETTINGS_WND, (text = "Import Settings", closable = false, flags = ImGuiWindowFlags.None)) {
        let selection = GetSelectedFile()
        if (!selection.valid) {
            text("Select a file in the Asset Browser.")
        } else {
            // A reimport may have finished in the background: refresh the document unless the
            // user is in the middle of an edit.
            let generation = AssetReimportGeneration()
            if (generation != g_generation) {
                g_generation = generation
                if (g_meta_path == selection.path && SaveMetaText(g_meta) == g_saved_text) {
                    g_reload = true
                }
            }

            text("path: {selection.path}")

            if (g_reload || selection.path != g_meta_path) {
                g_reload = false
                ReloadMeta(selection.path)
            }

            if (!g_has_meta) {
                text("No .meta: an authoritative file (content/data) has no import settings.")
            } elif (!empty(g_load_error)) {
                text("Cannot edit this .meta: {g_load_error}")
            } else {
                text("guid: {g_meta.header.guid:d}")
                same_line()
                text("  importer: {g_meta.header.importer_type}")
                if (length(g_meta.header.sub_assets) > 0) {
                    text("sub-assets: {length(g_meta.header.sub_assets)}")
                }

                separator()
                DrawMetaSettings(selection.path, g_meta)
                separator()

                let text_now = SaveMetaText(g_meta)
                let dirty = empty(g_saved_text) || text_now != g_saved_text

                with_disabled(!dirty) {
                    if (button(ASSET_SETTINGS_SAVE, (text = "Save"))) {
                        Save(selection.path, false)
                    }
                    same_line()
                    if (button(ASSET_SETTINGS_SAVE_REIMPORT, (text = "Save & Reimport"))) {
                        Save(selection.path, true)
                    }
                }
                same_line()
                if (button(ASSET_SETTINGS_REIMPORT, (text = "Reimport"))) {
                    if (dirty) {
                        g_status = "save the changes first: reimport reads the .meta from disk"
                    } elif (RequestReimport(selection.path)) {
                        g_status = "reimport queued"
                    } else {
                        g_status = "reimport rejected"
                    }
                }
                same_line()
                if (button(ASSET_SETTINGS_RELOAD, (text = "Reload"))) {
                    g_reload = true
                }

                if (dirty) {
                    text_disabled(ASSET_SETTINGS_DIRTY, (text = "unsaved changes"))
                }
            }

            if (!empty(g_status)) {
                separator()
                text("{g_status}")
            }
        }
    }
}
