options gen2

module Editor shared public

require tryeditor/Mover
require tryeditor/DebugRender

require trygame/Game

require tryengine/Scene
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

[export]
def EditorStart(){
    init_quadtree_debug()
}

[export]
def EditorUpdate(){
    Move(0.03)

    if (is_playing && !is_paused){
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