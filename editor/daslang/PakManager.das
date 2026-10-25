options gen2

module PakManager shared public

require imgui
require imgui/imgui_widgets_builtin
require imgui/imgui_containers_builtin
require imgui/imgui_layout_builtin
require imgui/imgui_id_builtin
require imgui/imgui_boost_runtime
require imgui/imgui_file_dialog

require daslib/random

require tryengine/Types
require tryeditor/FileSystemUtils
require tryeditor/EditorActions
require strings
require tryeditor

enum public PakType {
    FolderPak   // Создан из папки с ресурсами
    ScenePakM   // Сформирован из .scene
}

[safe_when_uninitialized]
struct public PakPackage {
    id : uint64                   // Уникальный идентификатор
    name : string                 // Понятное имя
    folder_path : string          // Исходная папка ресурсов
    pak_type : PakType = PakType.FolderPak
    scene_path : string = ""      // Заполняется только если в паке есть сцена (Максимум 1 сцена!)
    is_standalone : bool = false  // Если true — копирует все ресурсы внутрь своего пака
    guid_mesh : array<uint64>
    guid_mat : array<uint64>
    dependencies : array<string> // ID других паков, от которых зависит этот пак
}

var public g_paks : array<PakPackage>
var private g_selected_pak_id : uint64 = 0

def public find_pak(pak_id : uint64) : PakPackage? {
    for (pak in g_paks) {
        if (pak.id == pak_id) {
            return unsafe(addr(pak))
        }
    }
    return null
}
// Вызывается при регистрации папки как PAK
def public register_folder_as_pak(let folder : UiFolder&) {

    assume folder_path = folder.path

    for (pak in g_paks) {
        if (pak.folder_path == folder_path) {
            return
        }
    }

    var seed = random_seed(12345)

    var new_pak : PakPackage
    new_pak.id = uint64(random_uint(seed))
    new_pak.name = get_file_name_without_extension(folder_path)
    new_pak.folder_path = folder_path
    new_pak.pak_type = PakType.FolderPak
    new_pak.is_standalone = false

    let scenes = find_files_by_ext_in_dir(get_game_folders(), folder_path, ".scene")
    if (length(scenes) > 1) {
        print("Warning: PAK folder '{folder_path}' contains more than 1 scene! Only '{scenes[0].path}' will be assigned. \n")
        new_pak.scene_path = scenes[0].path
    } elif (length(scenes) == 1) {
        new_pak.scene_path = scenes[0].path
    }

    g_paks |> emplace(new_pak)
}

def DrawPakManager() {
    window(PakManagerWin, (text = "Pak Manager", closable = false, flags = ImGuiWindowFlags.None)) {
        text("Registered PAK Packages: {length(g_paks)}")
        separator(PAK_SEP_1)

        if (length(g_paks) == 0) {
            text_wrapped("No PAKs registered yet. Right-click any folder in AssetBrowser and select 'Mark Folder as PAK'.\n")
            return
        }

        for (pak in g_paks) {
            let is_sel = (pak.id == g_selected_pak_id)
            let scene_info = empty(pak.scene_path) ? "No Scene" : "Scene: {pak.scene_path}"
            if (button(PAK_LIST_ITEM, (text = "[{pak.pak_type == PakType.FolderPak ? "Folder PAK" : "PAKM"}] {pak.name} ({scene_info})\n"))) {
                g_selected_pak_id = pak.id
            }
        }

        separator(PAK_SEP_2)

        var selected_pak = find_pak(g_selected_pak_id)
        if (selected_pak != null) {
            text("PAK Inspector: {selected_pak.name}")
            text("Source Folder: {selected_pak.folder_path}")

            if (empty(selected_pak.scene_path)) {
                text("Scene: None (Pure Asset PAK)")
            } else {
                text("Bound Scene: {selected_pak.scene_path} (1/1 Max)")
            }

            if (button(BTN_TOGGLE_STANDALONE, (text = "Mode: {selected_pak.is_standalone ? "Standalone (Self-Contained)" : "Shared (Refers to other PAKs)"}"))) {
                selected_pak.is_standalone = !selected_pak.is_standalone
            }

            separator(PAK_SEP_3)

            text("Pak Dependencies ({length(selected_pak.dependencies)}):")
            if (length(selected_pak.dependencies) == 0) {
                text("  (No direct dependencies)")
            } else {
                for (dep in selected_pak.dependencies) {
                    text("  -> Dep: {dep}")
                }
            }

            separator(PAK_SEP_3)
            text("Meshes GUIDs ({length(selected_pak.guid_mesh)}):")
            for (mesh_guid in selected_pak.guid_mesh) {
                text("  - Mesh: {mesh_guid}")
            }

            text("Materials GUIDs ({length(selected_pak.guid_mat)}):")
            for (mat_guid in selected_pak.guid_mat) {
                text("  - Mat: {mat_guid}")
            }
        }
    }
}