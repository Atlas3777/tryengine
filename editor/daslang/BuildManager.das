options gen2

module BuildManager shared public

require imgui
require imgui/imgui_containers_builtin
require imgui/imgui_widgets_builtin
require imgui/imgui_file_dialog
require imgui/imgui_boost_v2

require strings
require daslib/async_boost
require daslib/coroutines
require daslib/linq

require tryengine/AsyncTaskManager
require tryengine/Scene
// require imgui/imgui_containers_builtin
// require imgui/imgui_layout_builtin
// require imgui/imgui_id_builtin
// require imgui/imgui_boost_runtime

require TryEditor
require tryeditor/EditorActions
require tryeditor/FileSystemUtils

struct BuildDependent {
    scenes : array<SceneHeader>
}

var private build : BuildDependent

var private cache_scenes = array<AssetItem>()
var private build_dir : string

var private CHECKBOX_STATE : table<int; ToggleState -const>

def DrawBuildManager(){
    window(BUILD_MAN, (text = "BuildSettings", closable = false, flags = ImGuiWindowFlags.None)){
        if (button(UPDATE_SCENE, (text = "Update Scene List"))){
            cache_scenes <- FindFilesByExt(".scene")
        }
        if (!build_dir.empty()){
            text("{build_dir}");
        }
        if (button(SELECT_BUILD_DIR, (text = "SELECT BUILD DIR"))){
            open_editor_file_dialog(
                FileDialogConfig(
                    title = "SELECT BUILD DIR",
                    mode = FileDialogMode.pick_dir
                ),
                @(path : string, ctx : string) : void {
                    build_dir = path
                })
        }

        for (i in range(length(cache_scenes))) {
            assume scene = cache_scenes[i]
            checkbox(CHECKBOX_STATE[i], (text = "{scene.name}"))
        }

        if (button(BUILD_PREVIEW, (text = "Build preview"))){
            var arr : array<string>

            let str = build_string()$(var writer){
                for (i in range(length(cache_scenes))){
                    assume scene = cache_scenes[i]
                    if (CHECKBOX_STATE[i].value){
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

        if (button(BUILD_FINAL, (text = "Create Build"))){
            if (build_dir.empty()){
                print("select build_dir! \n");
                return
            }
            CreateBuild(build_dir, build.scenes)
        }
    }
}


def CrearBuildSettigs() {
    build.scenes.clear()
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