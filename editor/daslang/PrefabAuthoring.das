options gen2
options relaxed_assign = false

module PrefabAuthoring shared public

// Prefab authoring actions that start from the edited scene, not from a prefab document:
// "Save Selection as Prefab". They live outside the inspector so the inspector stays a
// read-only view of the selected prefab.

require imgui/imgui_file_dialog
require strings
require daslib/async_boost
require daslib/decs
require daslib/decs_boost

require tryengine/AsyncTaskManager
require tryengine/Prefab
require tryengine/Profiling
require tryengine/Types

require tryeditor/EditorActions
require tryeditor/FileSystemUtils
require tryeditor/Hierarchy

var private g_save_selection_eid : EntityId = INVALID_ENTITY_ID

def private SelectedEntityTag() : string {
    var tag = ""
    if (is_alive(selected_eid)) {
        query(selected_eid) $(value : string) {
            tag = value
        }
    }
    return tag
}

def private SanitizePrefabName(name : string) : string {
    var result = replace(name, " ", "_")
    result = replace(result, "/", "_")
    result = replace(result, "\\", "_")
    if (empty(result)) {
        return "prefab"
    }
    return result
}

[async]
def private SaveSelectionAsync(eid : EntityId; path : string) : void {
    let saved : bool <- await(Prefab::SavePrefabFromEntity(eid, path))
    if (saved) {
        // A new file becomes visible in the Asset Browser only after the trees rescan.
        RefreshAssetTrees()
        print("[PREFAB] saved selection to '{path}'\n")
    } else {
        print("[PREFAB ERROR] failed to save selection to '{path}'\n")
    }
}

//! Opens a save dialog for the current selection and writes it as a pre-order prefab.
def public SaveSelectionAsPrefab() {
    if (selected_eid == INVALID_ENTITY_ID) {
        print("[PREFAB ERROR] select an entity first\n")
        return
    }

    g_save_selection_eid = selected_eid
    let default_name = "{SanitizePrefabName(SelectedEntityTag())}.prefab"
    open_editor_file_dialog(
        FileDialogConfig(
            title = "Save Selection as Prefab",
            mode = FileDialogMode.save,
            default_name = default_name,
            start_dir = "game/content",
            filters <- file_filters("Prefab Files|prefab|All Files|")
        ),
        @(path : string; ctx : string) : void {
            if (g_save_selection_eid != INVALID_ENTITY_ID) {
                StartTask(SaveSelectionAsync(g_save_selection_eid, path))
            }
        }
    )
}
