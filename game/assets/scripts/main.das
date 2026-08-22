options gen2

require daslib/decs
require tryengine/Types
require tryeditor/Hierarchy
require tryeditor/Mover
require daslib/math_boost
require tryengine/Input

[export]
def start(){
    // let chair_eid = create_entity() @(eid, cmp) {
    //     cmp.tag := "Sun"
    //     apply_decs_template(cmp, Relationship())
    //     apply_decs_template(cmp, Transform(
    //         position = float3(0.0f, 100.0f, 0.0f),
    //         rotation = float4(0.0f, 0.0f, 0.0f, 1.0f),
    //         scale = float3(1.0f, 1.0f, 1.0f)
    //     ))
    //     apply_decs_template(cmp, PointLight(
    //         intensity = 1,
    //         color = float3(2, 1, 1),
    //         radius = 150
    //     ))
    // }

    let laptop_eid = create_entity() @(eid, cmp) {
        cmp.tag := "PointLight"
        apply_decs_template(cmp, Relationship())
        apply_decs_template(cmp, Transform(
            position = float3(10.0f, 0.0f, 0.0f),
            rotation = float4(0.0f, 0.0f, 0.0f, 1.0f),
            scale = float3(1.0f, 1.0f, 1.0f)
        ))
         apply_decs_template(cmp, PointLight(
            intensity = 1,
            color = float3(0, 0, 1),
            radius = 30
        ))
    }

    let camera_erd = create_entity() @(eid, cmp){
        cmp.tag := "Camera"
        apply_decs_template(cmp, Transform(
            position = float3(0.0f, 2.0f, 5.0f),
            rotation = float4(0, 0, 0, 1),
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

def Spawn(){
    let test_eid = create_entity() @(eid, cmp) {
        cmp.tag := "teest"
        apply_decs_template(cmp, Transform(
            position = float3(0.0f, 0.0f, 0.0f),
            rotation = float4(0.0f, 0.0f, 0.0f, 1.0f),
            scale = float3(1.0f, 1.0f, 1.0f)
        ))
        cmp.meshGuid := 2069228533742692363ul
        cmp.materialGuid := 12330106561749381653ul
        apply_decs_template(cmp, Relationship())
    }
    commit()
}

[export]
def EditorUpdate(dt : float){
    if (pressed(Key.K))
        Spawn()
    Move(dt)
}

[export]
def update(time : float) {
    query()$(var t : Transform, var l : PointLight) {
        t.position.y += sin(time)
    }

}