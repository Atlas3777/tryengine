options gen2
options relaxed_assign = false

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

require TryEditor
require tryeditor/EditorActions
require tryeditor/FileSystemUtils
require tryengine/FileReader

struct BuildDependent {
    scenes : array<SceneHeader>
}

var private build : BuildDependent

// Сцены держим ссылками на дерево, а не копиями AssetItem: строки AssetItem заимствованы
// из дерева, и пересборка (Refresh) их освобождает. Ревизия — та, при которой список снят.
var private cache_scenes : array<AssetFileRef>
var private cache_scene_revision : uint64 = 0ul
var private build_dir : string
var private build_status : string
var private create_status : string

let private SCRIPT_ENTRY = "./game/scripts/Game.das"

var private CHECKBOX_STATE : table<int; ToggleState -const>


// Список сцен следует за деревом content: кнопка обновляет его принудительно, а ревизия —
// сама, поэтому пересборка дерева не оставляет в списке висячие ссылки.
def private EnsureSceneList() {
    let revision = get_tree_revision(StorageDomain.Content)
    if (revision == cache_scene_revision) {
        return
    }
    cache_scene_revision = revision
    cache_scenes <- FindFiles(StorageDomain.Content, EXT_SCENE)
}

def private ScriptPakPath() : string {
    if (build_dir.empty()) {
        return ""
    }
    var dir = build_dir
    if (ends_with(dir, "/")) {
        dir = slice(dir, 0, length(dir) - 1)
    }
    return "{dir}/script_pak"
}

def private BuildScripts() : bool {
    let path = ScriptPakPath()
    if (path.empty()) {
        build_status = "First, Choose build dir"
        return false
    }
    if (!BuildScriptPak(SCRIPT_ENTRY, path)) {
        build_status = "Build Error script_pak"
        return false
    }
    build_status = "script_pak: {path}"
    return true
}

def private TestScripts() : bool {
    let path = ScriptPakPath()
    if (path.empty()) {
        build_status = "choose build dir"
        return false
    }
    build_status = "test-compile: {path}"
    StartTask(TestScriptPakTask(path))
    return true
}

[async]
def private TestScriptPakTask(path : string) : void {
    let file_size : uint64 <- await(FileReader::GetFileSize(path))
    if (file_size == 0ul) {
        build_status = "script_pak не найден: {path}"
    } else {
        let bytes : array<uint8> <- await(FileReader::ReadFile(path, 0ul, uint(file_size)))
        if (TestCompileScriptPak(bytes)) {
            build_status = "test-compile: OK"
        } else {
            build_status = "test-compile: error (see. log)"
        }
    }
}


// Шаги сборки: пакеты ассетов (по сценам, выбранным в этом окне) и script_pak
// с тест-компиляцией. Create Build прогоняет их по порядку; упавший шаг останавливает сборку.
def private AssetPaksStep(dir : string) : bool {
    if (empty(build.scenes)) {
        build_status = "нет выбранных сцен: сначала Build preview"
        return false
    }
    CreateBuild(dir, build.scenes)
    return true
}

def private ScriptPakStep() : bool {
    if (!BuildScripts()) {
        return false
    }
    return TestScripts()
}

def private SelectBuildDir(dir : string) {
    build_dir = dir
    build_status = ""
    create_status = ""
}

def private CreateBuildAction() {
    if (build_dir.empty()) {
        create_status = "choose build dir"
        return
    }
    build_status = ""
    create_status = ""

    if (!AssetPaksStep(build_dir)) {
        create_status = "Build: остановлен на шаге 'Asset paks'"
        return
    }
    if (!ScriptPakStep()) {
        create_status = "Build: остановлен на шаге 'Script pak'"
        return
    }
    create_status = "Build: OK"
}


def DrawBuildManager(){
    window(BUILD_MAN, (text = "BuildSettings", closable = false, flags = ImGuiWindowFlags.None)){
        EnsureSceneList()
        if (!build_dir.empty()){
            text("{build_dir}");
        }
        text("Build root: script_pak и content/");
        if (button(SELECT_BUILD_DIR, (text = "SELECT BUILD DIR"))){
            open_editor_file_dialog(
                FileDialogConfig(
                    title = "SELECT BUILD DIR",
                    mode = FileDialogMode.pick_dir
                ),
                @(path : string, ctx : string) : void {
                    SelectBuildDir(path)
                })
        }

        if (button(BUILD_CREATE, (text = "Create Build"))) { CreateBuildAction() }
        if (!create_status.empty()) {
            text("{create_status}");
        }

        separator()
        if (button(BUILD_SCRIPTS, (text = "Rebuild Script Pak"))) { BuildScripts() }
        same_line()
        if (button(TEST_SCRIPTS, (text = "Test Compile Scripts"))) { TestScripts() }
        if (!build_status.empty()) {
            text("{build_status}");
        }

        for (i in range(length(cache_scenes))) {
            let scene <- TreeFile(StorageDomain.Content, cache_scenes[i])
            checkbox(CHECKBOX_STATE[i], (text = "{scene.name}"))
        }

        if (button(BUILD_PREVIEW, (text = "Build preview"))){
            var arr : array<string>

            let str = build_string()$(var writer){
                for (i in range(length(cache_scenes))){
                    let scene <- TreeFile(StorageDomain.Content, cache_scenes[i])
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