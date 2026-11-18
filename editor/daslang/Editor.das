options gen2
options relaxed_assign = false

module Editor shared public

require tryeditor/Mover
require tryeditor/DebugRender
require tryeditor/EditorHotkeys

require trygame/Game

require tryengine/Scene
require tryengine/Profiling
require tryengine/Time
require tryengine/Types
require tryengine/Input
require tryengine/AsyncTaskManager
require tryeditor/QuadTreeDebugWindow

require tryResources

require imgui
require daslib/decs

let mesh_id = 3081795527557720915ul
let mat_id = 17036954862819815981ul

var is_playing : bool = false
var is_paused  : bool = false

// The editor owns its clock and hangs it off the engine root: editor tools (camera, gizmos)
// keep running while the game is paused, and play mode never slows them down.
struct EditorClock : ClockTag {}

var editor_clock = EditorClock()

def private OnSceneLoaded(name : string; path : string; empty : bool) {
    EnsureEditorCamera()
}

[export]
def EditorStart(){
    TimeAttach(editor_clock)
    init_quadtree_debug()
    RegisterSceneLoadedListener(@@OnSceneLoaded)
    LoadEmptyScene()
}

[export, profile]
def EditorUpdate(){
    ProcessHotkeys()
    Move(TimeDelta(editor_clock))

    // A paused game clock is a zero delta, and a system that integrates time has no
    // business running on a zero delta at all.
    if (is_playing && TimeIsAdvancing(game_clock)){
        GameUpdate()
    }
}

// def create_mesh_entity(tag : string, pos : float3) {
//     return create_entity() @(eid, cmp) {
//         cmp.tag := tag
//         apply_decs_template(cmp, Transform(
//             position = pos,
//             rotation = float4(0.0f, 0.0f, 0.0f, 1.0f),
//             scale = float3(1.0f, 1.0f, 1.0f)
//         ))
//         cmp.meshGuid := mesh_id
//         cmp.materialGuid := mat_id
//         apply_decs_template(cmp, Relationship())
//     }
// }

// def Spawn() {
//     print("Spawn \n")

//     let test_eid = create_entity() @(eid, cmp) {
//         cmp.tag := "teest"
//         apply_decs_template(cmp, Transform(
//             position = float3(0.0f, 0.0f, 0.0f),
//             rotation = float4(0.0f, 0.0f, 0.0f, 1.0f),
//             scale = float3(1.0f, 1.0f, 1.0f)
//         ))
//         cmp.inputState := float3(0, 0, 0)
//         cmp.speed := float(3)

//         cmp.meshGuid := mesh_id
//         cmp.materialGuid := mat_id
//         apply_decs_template(cmp, Relationship())
//     }

//     create_mesh_entity("Right 1", float3(5.0f, 5.0f, 0.0f))
//     create_mesh_entity("Right 2", float3(5.0f, 10.0f, 0.0f))
//     create_mesh_entity("Left 1",  float3(-5.0f, 5.0f, 0.0f))
//     create_mesh_entity("Left 2",  float3(-5.0f, 10.0f, 0.0f))

//     commit()
// }
    // if (pressed(Key.H)){
    //     print("request mesh \n")
    //     RequestLoadMesh(mesh_id)
    //     print("request mat \n")
    //     RequestLoadMaterial(mat_id)
    // }

    // if (pressed(Key.B))
    //     Spawn()
    // AddLine(float3(0, 0, 0), float3(1, 0, 0), Color.Red)
    // AddLine(float3(0, 0, 0), float3(0, 1, 0), Color.Green)
    // AddLine(float3(0, 0, 0), float3(0, 0, 1), Color.Blue)