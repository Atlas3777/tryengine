options gen2
options rtti = true

require daslib/rtti

require daslib/decs

require tryengine/Types
require tryengine/Scene
require tryengine/Input
require tryengine/Render
require tryeditor/QuadTreeDebugWindow
require tryResources
require tryengine/Serializable
require tryengine/ConfigFormat
require tryengine/ConfigFormatBoost

// [Serializable]
// struct TestSer {
//     name : string
//     somee : uint64
//     pp : CastPlayer
// }

// struct CastPlayer{
//     hp : int
//     speed : float
// }

[export]
def Start(){
    //var test = TestSer(name = "", somee = 43ul, pp = CastPlayer(hp = 3, speed = 3f))

    //let a = write_config(CF(test))
    //print("TTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTt : {a}")

    update_input()

    create_entity() @(eid, cmp){
        cmp.tag := "EditorCamera"
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
