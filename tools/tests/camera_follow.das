options gen2
options relaxed_assign = false

//! Headless check for the camera query used by game/scripts/Player.das: only entities with a
//! Camera template and without the editor-camera tag are moved. Runs without physics.

require math
require daslib/decs
require daslib/decs_boost
require tryengine/EcsMacro
require tryengine/TransformSystem
require tryengine/Types

struct private EditorCameraTag {
    enabled : bool = true
}

%ecs~("editorCameraTag", type<EditorCameraTag>, hidden = true, transient = true) %%

var g_failures : int

def private Check(cond : bool; what : string) {
    if (cond) {
        print("  ok   {what}\n")
    } else {
        print("  FAIL {what}\n")
        g_failures++
    }
}

def private PositionOf(eid : EntityId) : float3 {
    return get_component(eid, "transform", Transform()).position
}

[export]
def main() : int {
    let game_camera = create_entity() @(e, cmp) {
        cmp.tag := "GameCamera"
        ApplyEntityTransform(cmp, float3(0.0f, 0.0f, 1.0f), float4(0.0f, 0.0f, 0.0f, 1.0f), float3(1.0f, 1.0f, 1.0f))
        apply_decs_template(cmp, Camera())
    }
    let editor_camera = create_entity() @(e, cmp) {
        cmp.tag := "EditorCamera"
        ApplyEntityTransform(cmp, float3(0.0f, 0.0f, 5.0f), float4(0.0f, 0.0f, 0.0f, 1.0f), float3(1.0f, 1.0f, 1.0f))
        cmp |> set("editorCameraTag", EditorCameraTag())
        apply_decs_template(cmp, Camera())
    }
    let plain = create_entity() @(e, cmp) {
        cmp.tag := "Plain"
        ApplyEntityTransform(cmp, float3(9.0f, 9.0f, 9.0f), float4(0.0f, 0.0f, 0.0f, 1.0f), float3(1.0f, 1.0f, 1.0f))
    }
    commit()

    let eye = float3(2.0f, 3.0f, 4.0f)
    query() $[REQUIRE_NOT(editorCameraTag)] (var transform : Transform; camera : Camera) {
        transform.position = eye
        transform.rotation = float4(0.0f, 0.0f, 0.0f, 1.0f)
    }

    Check(PositionOf(game_camera) == eye, "the game camera is moved to the eye")
    Check(PositionOf(editor_camera) == float3(0.0f, 0.0f, 5.0f), "the editor camera is left alone")
    Check(PositionOf(plain) == float3(9.0f, 9.0f, 9.0f), "a non-camera entity is left alone")

    print("-- failures: {g_failures} --\n")
    return g_failures == 0 ? 0 : 1
}
