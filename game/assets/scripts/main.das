options gen2
options rtti = true

require daslib/decs
require tryengine/Types
require tryengine/Input
require tryResources
require tryengine/Engine
require tryengine/Render

[export]
def GameStart(){
    create_entity() @(eid, cmp){
        cmp.tag := "Camera"
        apply_decs_template(cmp, Transform(
            position = float3(0.0f, 0.0f, 2.0f),
            rotation = float4(0f, 0f, 0f, 1f),
            scale = float3(1.0f, 1.0f, 1.0f)
        ))
        apply_decs_template(cmp, Camera(
            fov = 60.0f,
            near_plane = 0.01f,
            far_plane = 100.0f
        ))
        apply_decs_template(cmp, Relationship())
    }

    commit()
}

[export]
def GameUpdate() {

    query()$(camera : Camera, var t : Transform) {
        // t.position.z += 0.01
    }

    query()$(var t : Transform, var inputState : float3){
        inputState = float3(0, 0, 0)
        if (is_down(Key.I)) { inputState.y =  1.0f }
        if (is_down(Key.K)) { inputState.y = -1.0f }
        if (is_down(Key.J)) { inputState.x = -1.0f }
        if (is_down(Key.L)) { inputState.x =  1.0f }
    }

    query()$(var t : Transform, inputState : float3, speed : float){
        t.position += inputState * 0.08 * speed;
    }

    if (pressed(Key.H)){
        print("request mesh \n")
        RequestLoadMesh(mesh_id)
        RequestLoadMaterial(mat_id)
    }

    if (pressed(Key.B))
        Spawn()
}

let mesh_id = 3081795527557720915ul
let mat_id = 17036954862819815981ul

def Spawn() {
    print("Spawn \n")

    let test_eid = create_entity() @(eid, cmp) {
        cmp.tag := "teeest"
        apply_decs_template(cmp, Transform(
            position = float3(0.0f, 0.0f, 0.0f),
            rotation = float4(0.0f, 0.0f, 0.0f, 1.0f),
            scale = float3(1.0f, 1.0f, 1.0f)
        ))
        cmp.inputState := float3(0, 0, 0)
        cmp.speed := float(3)

        cmp.meshGuid := mesh_id
        cmp.materialGuid := mat_id
        apply_decs_template(cmp, Relationship())
    }

    create_mesh_entity("Right 1", float3(5.0f, 5.0f, 0.0f))
    create_mesh_entity("Right 2", float3(5.0f, 10.0f, 0.0f))
    create_mesh_entity("Left 1",  float3(-5.0f, 5.0f, 0.0f))
    create_mesh_entity("Left 2",  float3(-5.0f, 10.0f, 0.0f))

    commit()
}

def create_mesh_entity(tag : string, pos : float3) {
    return create_entity() @(eid, cmp) {
        cmp.tag := tag
        apply_decs_template(cmp, Transform(
            position = pos,
            rotation = float4(0.0f, 0.0f, 0.0f, 1.0f),
            scale = float3(1.0f, 1.0f, 1.0f)
        ))
        cmp.meshGuid := mesh_id
        cmp.materialGuid := mat_id
        apply_decs_template(cmp, Relationship())
    }
}