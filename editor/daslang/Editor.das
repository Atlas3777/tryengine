options gen2
options relaxed_assign = false

module Editor shared public

require tryeditor/Mover
require tryeditor/DebugRender
require tryeditor/EditorHotkeys
require tryeditor/Hierarchy
require tryeditor/Gizmo
require tryeditor/ProBuilder

require trygame/Game

require math
require tryengine/EcsMacro
require tryengine/Model
require tryengine/Scene
require tryengine/Profiling
require tryengine/Time
require tryengine/TransformSystem
require tryengine/Ui
require tryengine/Types
require tryengine/Input
require tryengine/Live
require tryengine/AsyncTaskManager
require tryeditor/QuadTreeDebugWindow
require tryeditor/UndoRedo

require tryResources
require TryEditor

require imgui
require daslib/decs
require daslib/decs_boost

var is_playing : bool = false
var is_paused  : bool = false

// Reimporting an asset changes the contents of materials/meshes but not their id. By this
// point EditorApp has already dropped the loaded resources; here entity pointers are
// re-resolved, same as after Play/Stop. While the new resource loads, the old pointer keeps
// drawing, so the entity never disappears.
var private last_asset_generation : uint64 = 0ul

// Editor state. Declared below as a component, but transient and hidden: it never reaches a
// scene or a prefab and the Inspector does not draw it.
struct private EditorCameraTag {
    enabled : bool = true
}

%ecs~("editorCameraTag", type<EditorCameraTag>, hidden = true, transient = true) %%

// The editor owns its clock and hangs it off the engine root: editor tools (camera, gizmos)
// keep running while the game is paused, and play mode never slows them down. The tag is
// `@live` so a reload does not reset the camera movement delta to zero.
struct EditorClock : ClockTag {}

var @live editor_clock = EditorClock()

def private OnSceneLoaded(name : string; path : string; empty : bool) {
    print("Загрузка\n")
    // A scene load replaces the whole DECS heap, so an id from the previous scene is no
    // longer even in range of the lookup table. Panels that query `selected_eid` would
    // index past it; clear the selection at the one place every load goes through.
    selected_eid = INVALID_ENTITY_ID
    // The history refers to entities of the world that has just been thrown away.
    UndoRedo::ClearHistory()

    // Play runs GameStart unchanged; the editor camera is editor state layered on top.
    // It is tagged `editorCameraTag`, so the game camera query and GameUpdate ignore it,
    // while Mover and the Scene viewport keep working during Play.
    create_entity() @(eid : EntityId; var cmp : ComponentMap) {
        cmp.tag := "EditorCamera"
        cmp |> set("editorCameraTag", EditorCameraTag(enabled = true))
        ApplyEntityTransform(cmp, float3(0, 0, 5), float4(0, 0, 0, 1), float3(1, 1, 1))
        apply_decs_template(cmp, Camera())
        cmp |> set(RELATIONSHIP_FIELD, Relationship())
    }
    decs::commit()
}

def private OnSceneUnloaded() {
    print("Выгрузка\n")
    UndoRedo::ClearHistory()
    // A scene load replaces the whole DECS heap, so an id from the previous scene is no
    // longer even in range of the lookup table. Panels that query `selected_eid` would
    // index past it; clear the selection at the one place every load goes through.
    selected_eid = INVALID_ENTITY_ID

    // Play runs GameStart unchanged; the editor camera is editor state layered on top.
    // It is tagged `editorCameraTag`, so the game camera query and GameUpdate ignore it,
    // while Mover and the Scene viewport keep working during Play.
    query() $[REQUIRE(editorCameraTag)] (eid : EntityId, transform : Transform, camera : Camera) {
        delete_entity(eid)
    }
    decs::commit()
}

[export]
def EditorStart(){
    TimeAttach(editor_clock)
    init_quadtree_debug()
    RegisterSceneLoadedListener(@@OnSceneLoaded)
    RegisterSceneUnloadedListener(@@OnSceneUnloaded)
    LoadEmptyScene()
    ProBuilderStart()
}

[export, profile]
def EditorUpdate(){
    let asset_generation = AssetReimportGeneration()
    if (asset_generation != last_asset_generation) {
        last_asset_generation = asset_generation
        Model::ReapplyPointers()
    } elif (Model::HasPendingReapply()) {
        Model::ReapplyPointers()
    }

    // Play runs with a game world and its changes are thrown away on Stop, and the hotkeys are
    // gone with it: undo only records and replays edits of the scene being authored.
    UndoRedo::SetSuspended(is_playing)
    ProcessHotkeys()
    ProcessGizmoHotkeys()
    ProcessProBuilderHotkeys()
    if (is_playing) {
        StopProBuilder()
    }
    Move(TimeDelta(editor_clock))
    // The gizmo state is built before the frame is rendered; interaction happens later in
    // DrawSceneViewPort, so the next frame draws the handle the user is already dragging.
    UpdateGizmoDrawState()

    // Play: the UI is a peer of gameplay, not a part of it, but the editor pause freezes the
    // whole game. With no frame declared the pass re-draws the last command list, so the screens
    // stay on screen frozen and the pointer reaches nothing. Edit mode has no gameplay at all:
    // an empty frame below clears the list, so no HUD from a previous Play survives.
    if (is_playing) {
        if (TimeIsAdvancing(game_clock)) {
            GameUiFrame()
        }
    } else {
        // Edit mode has no game UI. An empty frame clears the command list, so a HUD from a
        // previous Play session cannot stay on the viewport.
        Ui::Begin()
        Ui::End()
    }

    // A paused game clock is a zero delta, and a system that integrates time has no
    // business running on a zero delta at all.
    if (is_playing && TimeIsAdvancing(game_clock)){
        GameUpdate()
    }
}
