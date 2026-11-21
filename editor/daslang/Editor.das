options gen2
options relaxed_assign = false

module Editor shared public

require tryeditor/Mover
require tryeditor/DebugRender
require tryeditor/EditorHotkeys
require tryeditor/Hierarchy
require tryeditor/Gizmo

require trygame/Game

require math
require tryengine/Scene
require tryengine/Profiling
require tryengine/Time
require tryengine/TransformSystem
require tryengine/Types
require tryengine/Input
require tryengine/AsyncTaskManager
require tryeditor/QuadTreeDebugWindow

require tryResources

require imgui
require daslib/decs
require daslib/decs_boost

var is_playing : bool = false
var is_paused  : bool = false

// Editor state. A plain struct has no writer, so the marker never reaches a scene or a
// prefab and the Inspector does not draw it.
struct private EditorCameraTag {
    enabled : bool = true
}

// The editor owns its clock and hangs it off the engine root: editor tools (camera, gizmos)
// keep running while the game is paused, and play mode never slows them down.
struct EditorClock : ClockTag {}

var editor_clock = EditorClock()

def private OnSceneLoaded(name : string; path : string; empty : bool) {
    print("Загрузка\n")
    // A scene load replaces the whole DECS heap, so an id from the previous scene is no
    // longer even in range of the lookup table. Panels that query `selected_eid` would
    // index past it; clear the selection at the one place every load goes through.
    selected_eid = INVALID_ENTITY_ID

    // Play runs GameStart unchanged; the editor camera is editor state layered on top.
    // It is tagged `editorCameraTag`, so the game camera query and GameUpdate ignore it,
    // while Mover and the Scene viewport keep working during Play.
    create_entity() @(eid : EntityId; var cmp : ComponentMap) {
        cmp.tag := "EditorCamera"
        cmp |> set("editorCameraTag", EditorCameraTag(enabled = true))
        ApplyEntityTransform(cmp, float3(0, 0, 0), float4(0, 0, 0, 1), float3(1, 1, 1))
        apply_decs_template(cmp, Camera())
        cmp |> set(RELATIONSHIP_FIELD, Relationship())
    }
    decs::commit()
}

def private OnSceneUnloaded() {
    print("Выгрузка\n")
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
}

[export, profile]
def EditorUpdate(){
    ProcessHotkeys()
    ProcessGizmoHotkeys()
    Move(TimeDelta(editor_clock))
    // The gizmo state is built before the frame is rendered; interaction happens later in
    // DrawSceneViewPort, so the next frame draws the handle the user is already dragging.
    UpdateGizmoDrawState()

    // A paused game clock is a zero delta, and a system that integrates time has no
    // business running on a zero delta at all.
    if (is_playing && TimeIsAdvancing(game_clock)){
        GameUpdate()
    }
}
