options gen2

module BuildManager shared public

require imgui
require imgui/imgui_containers_builtin
require imgui/imgui_widgets_builtin

require strings
require daslib/async_boost
require daslib/coroutines

require tryeditor/PakManager
require tryengine/AsyncTaskManager
require tryengine/Scene
// require imgui/imgui_containers_builtin
// require imgui/imgui_layout_builtin
// require imgui/imgui_id_builtin
// require imgui/imgui_boost_runtime

require tryeditor/FileSystemUtils
require daslib/linq
require tryeditor

struct BuildDependent {
    scenes : array<SceneHeader>
}

var private build : BuildDependent

var private cache_scenes = array<AssetItem>()

var private RADIO_BUTTON : table<int; ToggleState -const>

def DrawBuildManager(){
    window(BUILD_MAN, (text = "BuildSettings", closable = false, flags = ImGuiWindowFlags.None)){
        if (button(UPDATE_SCENE, (text = "Update Scene List"))){
            cache_scenes <- FindFilesByExt(".scene")
        }

        for (i in range(length(cache_scenes))) {
            assume scene = cache_scenes[i]
            checkbox(RADIO_BUTTON[i], (text = "{scene.name}"))
        }

        if (button(BUILD_PREVIEW, (text = "Build preview"))){
            var arr : array<string>

            let str = build_string()$(var writer){
                for (i in range(length(cache_scenes))){
                    assume scene = cache_scenes[i]
                    if (RADIO_BUTTON[i].value){
                        writer.write(scene.name)
                        writer.write(" ")
                        writer.write(scene.guid)
                        writer.write(" ")
                        arr.push(scene.path)
                    }
                }
            }

            StartTask(FindPaks(arr))
        }
        DrawBuildPreview()

        if (button(BUILD_FINAL, (text = "Create Build"))){
            CreateBuild("/home/artem/trybuild", build.scenes)
        }
    }
}

def DrawBuildPreview() {
    // text("included paks")
    // for (pak in build.need_pak){
    //     text("{pak.name}")
    // }
    // text("standalone_meshs:")
    // for (mesh in build.standalone_meshs){
    //     text("- {mesh:d}")
    // }
    // text("standalone_mat:")
    // for (mat in build.standalone_mat){
    //     text("- {mat:d}")
    // }
}


def CrearBuildSettigs() {
    build.scenes.clear()
    // build.need_pak.clear()
    // build.standalone_mat.clear()
    // build.standalone_meshs.clear()
}


[async]
def FindPaks(var paths : array<string>&) : void {
    CrearBuildSettigs()

    for (path in paths) {
        let header : SceneHeader <- await(Scene::GetSceneHeader(path))
        for (a in header.requireMeshs){
            print("das Req Mesh {a:d}\n")
        }
        for (b in header.requireMaterials){
            print("das Req Mat {b:d}\n")
        }
        build.scenes |> emplace(header)
    }
}