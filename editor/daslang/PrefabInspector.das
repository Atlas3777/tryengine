options gen2
options relaxed_assign = false

module PrefabInspector shared public

// Prefab inspector. The Asset Browser owns discovery and selection, so this panel never
// lists or scans content: there is exactly one place that answers "what prefabs exist".
// It shows the selected `.prefab` document, its entity tree and the actions that make
// sense for a document (spawn, reload). Saving a scene selection is authoring, not
// inspection; it lives in `tryeditor/PrefabAuthoring`.

require imgui
require imgui/imgui_widgets_builtin
require imgui/imgui_containers_builtin
require imgui/imgui_layout_builtin
require imgui/imgui_id_builtin
require imgui/imgui_boost_runtime
require imgui/imgui_boost_v2
require daslib/async_boost
require daslib/decs
require daslib/decs_boost

require tryengine/AsyncTaskManager
require tryengine/ConfigFormatBoost
require tryengine/Prefab
require tryengine/Profiling
require tryengine/Types

require TryEditor
require tryeditor/AssetBrowser
require tryeditor/FileSystemUtils
require tryeditor/Hierarchy

var PREFAB_ENTITY_NODE : TreeNodeState
// Component rows are repeated at the same call site: one NarrativeState per
// (entity, component) keeps dasImgui's single-global render check happy.
var private PREFAB_COMPONENT_TEXT : table<string; NarrativeState>

var private g_selected_path : string
// Path whose on-demand load was already requested. It is not cleared on completion, so an
// invalid prefab is not re-read every frame; the Reload button is the explicit retry.
var private g_requested_path : string
var private g_status : string

def private SelectedPrefabPath() : string {
    let selected = AssetBrowser::GetSelectedFile()
    if (!selected.valid || selected.ext != asset_ext_key(".prefab")) {
        return ""
    }
    return selected.path
}

[async]
def private LoadSelectedAsync(path : string) : void {
    let loaded : bool <- await(Prefab::LoadPrefabFile(path))
    if (g_selected_path == path) {
        g_status = loaded ? "Loaded {path}" : "Load failed: {path}"
    }
}

// Follows the Asset Browser selection. A newly selected prefab is loaded on demand; an
// already cached one (for example from the running game's request) is shown immediately.
def private SyncSelection() {
    let selected = SelectedPrefabPath()
    if (empty(selected) || selected == g_selected_path) {
        return
    }

    g_selected_path = selected
    g_status = ""
    if (!Prefab::IsPrefabCached(g_selected_path) && g_requested_path != g_selected_path) {
        g_requested_path = g_selected_path
        StartTask(LoadSelectedAsync(g_selected_path))
    }
}

def private DrawToolbar() {
    if (empty(g_selected_path)) {
        text_disabled("Select a .prefab in the Asset Browser")
        return
    }

    if (Prefab::IsPrefabCached(g_selected_path)) {
        if (button(PREFAB_SPAWN, (text = "Spawn at origin"))) {
            let eid = Prefab::Spawn(g_selected_path, float3(0.0f, 0.0f, 0.0f))
            if (eid != INVALID_ENTITY_ID) {
                selected_eid = eid
            }
        }
        same_line()
        if (button(PREFAB_RELOAD, (text = "Reload"))) {
            g_requested_path = g_selected_path
            StartTask(LoadSelectedAsync(g_selected_path))
        }
    } else {
        if (button(PREFAB_LOAD_SELECTED, (text = "Load"))) {
            g_requested_path = g_selected_path
            StartTask(LoadSelectedAsync(g_selected_path))
        }
    }

    if (!empty(g_status)) {
        same_line()
        text_disabled("{g_status}")
    }
}

def private EntityTag(fields : table<string; StructField?> const) : string {
    let tag_cf = fields?["tag"] ?? null
    if (tag_cf == null) {
        return ""
    }
    return from_CF(tag_cf, type<string>)
}

def private DrawDetails() {
    if (empty(g_selected_path)) {
        text_disabled("No prefab selected")
        return
    }

    let data = Prefab::GetPrefabData(g_selected_path)
    if (data == null) {
        text("Not loaded: {g_selected_path}")
        return
    }

    text("{data.name}")
    text_disabled("{data.path}")
    text("Entities: {length(data.entities)}")
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
            let flags = ImGuiTreeNodeFlags.OpenOnArrow | ImGuiTreeNodeFlags.SpanAvailWidth | ImGuiTreeNodeFlags.DefaultOpen
            let opened = tree_node_ex(PREFAB_ENTITY_NODE, (text = entity_label, flags = flags))
            if (opened) {
                var inscope names : array<string>
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
def public DrawPrefabInspector() {
    SyncSelection()

    window(PREFAB_WIN, (text = "Prefab Inspector", closable = false, flags = ImGuiWindowFlags.None)) {
        DrawToolbar()
        separator()
        DrawDetails()
    }
}
