options gen2

require daslib/decs

require tryengine/Types
require tryengine/Scene
require tryengine/Input
require tryengine/Render
require tryeditor/QuadTreeDebugWindow

[export]
def Start(){
    update_input()
    init_quadtree_debug()

    let chair_eid = create_entity() @(eid, cmp) {
        cmp.tag := "Sun"
        apply_decs_template(cmp, Relationship())
        apply_decs_template(cmp, Transform(
            position = float3(0.0f, 100.0f, 0.0f),
            rotation = float4(0.0f, 0.0f, 0.0f, 1.0f),
            scale = float3(1.0f, 1.0f, 1.0f)
        ))
        apply_decs_template(cmp, PointLight(
            intensity = 1.0f,
            color = float3(2f, 1f, 1f),
            radius = 150.0f
        ))
    }

    let laptop_eid = create_entity() @(eid, cmp) {
        cmp.tag := "PointLight"
        apply_decs_template(cmp, Relationship())
        apply_decs_template(cmp, Transform(
            position = float3(10.0f, 0.0f, 0.0f),
            rotation = float4(0.0f, 0.0f, 0.0f, 1.0f),
            scale = float3(1.0f, 1.0f, 1.0f)
        ))
         apply_decs_template(cmp, PointLight(
            intensity = 1.0f,
            color = float3(0f, 0f, 1f),
            radius = 30.0f
        ))
    }

    let camera_erd = create_entity() @(eid, cmp){
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
        cmp |> set("dsf", TTT(gp = 2))
        apply_decs_template(cmp, Relationship())
    }

    commit()
}

struct TTT{
    gp : int
}

[export]
def Update() {

    query()$(camera : Camera, var t : Transform) {
        t.position.z += 0.01
    }

    query()$(var t : Transform, var inputState : float3){
        inputState = float3(0, 0, 0)
        if (is_down(Key.I)) { inputState.y =  1.0f }
        if (is_down(Key.K)) { inputState.y = -1.0f }
        if (is_down(Key.J)) { inputState.x = -1.0f }
        if (is_down(Key.L)) { inputState.x =  1.0f }
    }

    query()$(var t : Transform, let inputState : float3, let speed : float){
        t.position += inputState * 0.08 * speed;
    }
}
