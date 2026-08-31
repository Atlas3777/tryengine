options gen2

module Editor shared public

require tryeditor/Hierarchy
require tryeditor/Inspector
require tryeditor/AssetBrowser
require tryeditor/SceneViewPort

require imgui
require imgui/imgui_boost_runtime
require tryeditor

require daslib/decs

require tryeditor/Mover

require tryengine/Scene
require tryengine/Types
require tryengine/Input

[export]
def EditorUpdate(){
    tick_async_tasks()

    if (pressed(Key.Y))
        start_task(SaveSceneAsync("TestScene"))

    if (pressed(Key.P))
        start_task(LoadSceneAsync("TestScene"))


    if (pressed(Key.B))
        Spawn()
    Move(0.08)
}

def Spawn(){
    let test_eid = create_entity() @(eid, cmp) {
        cmp.tag := "teest"
        apply_decs_template(cmp, Transform(
            position = float3(0.0f, 0.0f, 0.0f),
            rotation = float4(0.0f, 0.0f, 0.0f, 1.0f),
            scale = float3(1.0f, 1.0f, 1.0f)
        ))
        cmp.inputState := float3(0, 0, 0)
        cmp.speed := float(3)

        cmp.meshGuid := 2069228533742692363ul
        cmp.materialGuid := 12330106561749381653ul
        apply_decs_template(cmp, Relationship())
    }

        create_entity() @(eid, cmp) {
        cmp.tag := "Right 1"
        apply_decs_template(cmp, Transform(
            position = float3(5.0f, 5.0f, 0.0f),
            rotation = float4(0.0f, 0.0f, 0.0f, 1.0f),
            scale = float3(1.0f, 1.0f, 1.0f)
        ))
        cmp.meshGuid := 2069228533742692363ul
        cmp.materialGuid := 12330106561749381653ul
        apply_decs_template(cmp, Relationship())
    }
    create_entity() @(eid, cmp) {
        cmp.tag := "Right 2"
        apply_decs_template(cmp, Transform(
            position = float3(5.0f, 10.0f, 0f),
            rotation = float4(0.0f, 0.0f, 0.0f, 1.0f),
            scale = float3(1.0f, 1.0f, 1.0f)
        ))
        cmp.meshGuid := 2069228533742692363ul
        cmp.materialGuid := 12330106561749381653ul
        apply_decs_template(cmp, Relationship())
    }
    create_entity() @(eid, cmp) {
        cmp.tag := "Left 1"
        apply_decs_template(cmp, Transform(
            position = float3(-5.0f, 5.0f, 0.0f),
            rotation = float4(0.0f, 0.0f, 0.0f, 1.0f),
            scale = float3(1.0f, 1.0f, 1.0f)
        ))
        cmp.meshGuid := 2069228533742692363ul
        cmp.materialGuid := 12330106561749381653ul
        apply_decs_template(cmp, Relationship())
    }

     create_entity() @(eid, cmp) {
        cmp.tag := "Left 2"
        apply_decs_template(cmp, Transform(
            position = float3(-5.0f, 10.0f, 0),
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
def draw_editor() {
    unsafe {
        let ctx_address = get_editor_imgui_context()
        let host_ctx = reinterpret<ImGuiContext?>(ctx_address)
        SetCurrentContext(host_ctx)
    }
    begin_frame()


    DrawHierarchy()
    DrawInspector()
    DrawAssetBrowser()
    DrawSceneViewPort()

    end_of_frame()
}