options gen2
options _allow_imgui_legacy = true

module Editor shared public

require tryeditor/Hierarchy
require tryeditor/Inspector
require tryeditor/AssetBrowser
require tryeditor/SceneViewPort
require tryeditor/QuadTreeDebugWindow

require imgui
require imgui/imgui_widgets_builtin
require imgui/imgui_containers_builtin
require imgui/imgui_boost_runtime
require imgui/imgui_docking_builtin

require tryeditor
require tryResources

require daslib/decs

require tryeditor/Mover
require tryeditor/DebugRender

require tryengine/Scene
require tryengine/Types
require tryengine/Input


let mesh_id = 3081795527557720915ul
let mat_id = 17036954862819815981ul

[export]
def EditorUpdate(){
    tick_async_tasks()

    if (pressed(Key.Y))
        start_task(SaveSceneAsync("TestScene"))

    if (pressed(Key.P))
        start_task(LoadSceneAsync("TestScene"))

    if (pressed(Key.H)){
        print("request mesh \n")
        RequestLoadMesh(mesh_id)
        RequestLoadMaterial(mat_id)
    }
    if (pressed(Key.B))
        Spawn()
    Move(0.03)
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

def Spawn() {
    print("Spawn \n")

    let test_eid = create_entity() @(eid, cmp) {
        cmp.tag := "teest"
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

def DrawDockSpace() {
    dockspace(MAIN_DOCK_SPACE, (flags = ImGuiDockNodeFlags.None)) {
        // Контейнер автоматически разворачивается на весь вьюпорт
    }
}

[export]
def DrawEditor() {
    unsafe {
        let ctx_address = get_editor_imgui_context()
        if (ctx_address == null)
            print("imgui is null")
        let host_ctx = reinterpret<ImGuiContext?>(ctx_address)
        host_ctx.SetCurrentContext()
    }
    begin_frame()

    DrawDockSpace()

    DrawHierarchy()

    DrawInspector()
    DrawAssetBrowser()
    DrawSceneViewPort()
    DrawQuadTreeDebug()

    AddLine(float3(0, 0, 0), float3(1, 0, 0), Color.Red)
    AddLine(float3(0, 0, 0), float3(0, 1, 0), Color.Green)
    AddLine(float3(0, 0, 0), float3(0, 0, 1), Color.Blue)

    end_of_frame()
}