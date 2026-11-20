options gen2
options relaxed_assign = false

module PrefabWindow shared public

// Prefab browser: lists every cached .prefab under game/content, shows its entity
// tree, and spawns it into the active DECS scene. The cache itself lives in
// tryengine/Prefab; this panel only reads it. Rows are drag sources with the same
// "Asset" payload as AssetBrowser, so dropping a prefab into the viewport is handled
// by SceneViewPort without a second spawn path.

require imgui
require imgui/imgui_widgets_builtin
require imgui/imgui_containers_builtin
require imgui/imgui_layout_builtin
require imgui/imgui_id_builtin
require imgui/imgui_boost_runtime
require imgui/imgui_boost_v2
require imgui/imgui_file_dialog
require strings
require math
require daslib/algorithm
require daslib/async_boost
require daslib/decs
require daslib/decs_boost

require tryengine/Prefab
require tryengine/ConfigFormatBoost
require tryengine/AsyncTaskManager
require tryengine/Profiling
require tryengine/Types

require TryEditor
require tryeditor/AssetBrowser
require tryeditor/EditorActions
require tryeditor/FileSystemUtils
require tryeditor/Hierarchy

var PREFAB_FILTER : TextFilterState
var PREFAB_ENTITY_NODE : TreeNodeState
var private g_rows : table<string; ClickState>
// Component rows are repeated at the same call site: one NarrativeState per
// (entity, component) keeps dasImgui's single-global render check happy.
var private PREFAB_COMPONENT_TEXT : table<string; NarrativeState>

var private g_selected_path : string
var private g_status : string
var private g_initial_refresh : bool
var private g_save_selection_eid : EntityId = INVALID_ENTITY_ID

[async]
def private RefreshPrefabsAsync(rebuild_assets : bool) : void {
    // New files reach the registry only after the trees are rescanned; RefreshContent
    // reads the manifest of registered content, so the rescan goes first.
    if (rebuild_assets) {
        RefreshAssetTrees()
    }
    await(Prefab::RefreshContent())
    let cached_count = Prefab::PrefabCount()
    g_status = "Cached {cached_count} prefab(s)"

    if (empty(g_selected_path)) {
        Prefab::ForEachPrefab() $(path, data) {
            if (empty(g_selected_path)) {
                g_selected_path = path
            }
        }
    }
}

[async]
def private LoadPrefabAsync(path : string) : void {
    let loaded : bool <- await(Prefab::LoadPrefabFile(path))
    if (loaded) {
        g_status = "Reloaded {path}"
    } else {
        g_status = "Reload failed: {path}"
    }
}

[async]
def private SaveSelectionAsync(eid : EntityId; path : string) : void {
    let saved : bool <- await(Prefab::SavePrefabFromEntity(eid, path))
    if (saved) {
        RefreshAssetTrees()
        g_selected_path = Prefab::ResolvePrefabPath(path)
        g_status = "Saved {path}"
    } else {
        g_status = "Save failed: {path}"
    }
}

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

def private EntityTag(fields : table<string; StructField?> const) : string {
    let tag_cf = fields?["tag"] ?? null
    if (tag_cf == null) {
        return ""
    }
    return from_CF(tag_cf, type<string>)
}

def private DrawToolbar() {
    if (button(PREFAB_REFRESH, (text = "Refresh"))) {
        StartTask(RefreshPrefabsAsync(true))
    }
    same_line()

    let has_selection = (selected_eid != INVALID_ENTITY_ID)
    if (button(PREFAB_SAVE_SELECTION, (text = "Save Selection as Prefab..."))) {
        if (!has_selection) {
            g_status = "Select an entity first"
        } else {
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
    }

    same_line()
    let cached_count = Prefab::PrefabCount()
    text("{cached_count} cached")
    if (!empty(g_status)) {
        same_line()
        text_disabled("{g_status}")
    }
}

def private DrawPrefabList() {
    var shown = 0
    Prefab::ForEachPrefab() $(path, data) {
        if (!passes_filter(PREFAB_FILTER, "{data.name} {path}")) {
            return
        }
        shown++

        let is_selected = (g_selected_path == path)
        let row_label = "{data.name}  ({length(data.entities)})"

        with_id("prefab_row_{path}") {
            if (selectable_label(g_rows[path], row_label, is_selected)) {
                g_selected_path = path
            }

            drag_drop_source(PREFAB_DRAG_SRC, (flags = ImGuiDragDropFlags.None)) {
                PAYLOAD_VALUE.name = "{data.name}"
                PAYLOAD_VALUE.path = path
                PAYLOAD_VALUE.guid = 0ul
                PAYLOAD_VALUE.ext = asset_ext_key(".prefab")
                unsafe {
                    SetDragDropPayload("Asset", addr(PAYLOAD_VALUE), uint64(typeinfo sizeof(PAYLOAD_VALUE)),
                        ImGuiCond.Once)
                }
                text("Dragging prefab: {data.name}")
            }
        }
    }

    if (shown == 0) {
        text_disabled("No prefabs. Put .prefab files under game/content and press Refresh.")
    }
}

def private DrawPrefabDetails() {
    if (empty(g_selected_path)) {
        text_disabled("Select a prefab")
        return
    }

    let data = Prefab::GetPrefabData(g_selected_path)
    if (data == null) {
        text("Not cached: {g_selected_path}")
        if (button(PREFAB_LOAD_SELECTED, (text = "Load"))) {
            StartTask(LoadPrefabAsync(g_selected_path))
        }
        return
    }

    text("{data.name}")
    text_disabled("{data.path}")
    text("Entities: {length(data.entities)}")

    if (button(PREFAB_SPAWN, (text = "Spawn at origin"))) {
        let eid = Prefab::Spawn(g_selected_path, float3(0.0f, 0.0f, 0.0f))
        if (eid != INVALID_ENTITY_ID) {
            selected_eid = eid
        }
    }
    same_line()
    if (button(PREFAB_RELOAD, (text = "Reload"))) {
        StartTask(LoadPrefabAsync(g_selected_path))
    }

    separator()

    for (entity_index in range(length(data.entities))) {
        let entity = data.entities[entity_index]
        assume fields = entity.config.value as _object

        var entity_label = "[{entity.local_id}]"
        let tag = EntityTag(fields)
        if (!empty(tag)) {
            entity_label = "{entity_label} {tag}"
        }

        with_id("prefab_entity_{entity_index}") {
            var flags = ImGuiTreeNodeFlags.OpenOnArrow | ImGuiTreeNodeFlags.SpanAvailWidth | ImGuiTreeNodeFlags.DefaultOpen
            let opened = tree_node_ex(PREFAB_ENTITY_NODE, (text = entity_label, flags = flags))
            if (opened) {
                var names : array<string>
                for (name in keys(fields)) {
                    if (name == "localId" || name == "parentLocalId") {
                        continue
                    }
                    names |> push(name)
                }
                sort(names)
                for (name in names) {
                    let component_key = "{entity_index}/{name}"
                    text(PREFAB_COMPONENT_TEXT[component_key], (text = "{name}"))
                }
                tree_pop()
            }
        }
    }
}

[profile]
def public DrawPrefabWindow() {
    if (!g_initial_refresh) {
        g_initial_refresh = true
        StartTask(RefreshPrefabsAsync(false))
    }

    window(PREFAB_WIN, (text = "Prefabs", closable = false, flags = ImGuiWindowFlags.None)) {
        DrawToolbar()
        separator()
        text_filter(PREFAB_FILTER, "Search", 260.0f)

        columns_open(2, "prefab_columns", true)
        child(PREFAB_LIST, (text = "prefab_list", size = float2(320.0f, 0.0f),
         child_flags = ImGuiChildFlags.Borders, window_flags = ImGuiWindowFlags.None)) {
            DrawPrefabList()
        }
        next_col()
        child(PREFAB_DETAILS, (text = "prefab_details", size = float2(0.0f, 0.0f),
         child_flags = ImGuiChildFlags.Borders, window_flags = ImGuiWindowFlags.None)) {
            DrawPrefabDetails()
        }
        columns_close()
    }
}
