options gen2

module EditorUI shared public

require tryengine/Scene
require tryengine/AsyncTaskManager
require strings
require math

require tryeditor/Editor
require trygame/Game

require tryeditor/Hierarchy
require tryeditor/Inspector
require tryeditor/AssetBrowser
require tryeditor/SceneViewPort
require tryeditor/QuadTreeDebugWindow
require tryeditor/BuildManager
require tryeditor/EditorActions

require imgui
require imgui/imgui_containers_builtin
require imgui/imgui_widgets_builtin
require imgui/imgui_docking_builtin
require imgui/imgui_boost_v2
require imgui/imgui_file_dialog

require TryEditor

require daslib/decs
require daslib/archive

def DrawMainMenu() {
    main_menu_bar(MAIN_MENU_BAR) {
        menu(FILE_MENU, (text = "File", enabled = true)) {
            if (menu_label(FILE_NEW, (text = "New Scene", shortcut = "Ctrl+N"))) {
                StartTask(CreateScene("Untitled", ""))
            }
            if (menu_label(FILE_OPEN, (text = "Open Scene...", shortcut = "Ctrl+O"))) {
                open_editor_file_dialog(
                    FileDialogConfig(
                        title = "Open Scene",
                        mode = FileDialogMode.open,
                        start_dir = "game/assets",
                        filters = file_filters("Scene Files|scene|All Files|")
                    ),
                    @(path : string; ctx : string) : void {
                        let name = get_file_name_without_extension(path)
                        StartTask(LoadScene(name, path))
                    }
                )
            }

            if (menu_label(FILE_SAVE, (text = "Save Scene", shortcut = "Ctrl+S"))) {
                if (GetScenePath() == "") {
                    open_editor_file_dialog(
                        FileDialogConfig(
                            title = "Save Scene",
                            mode = FileDialogMode.save,
                            default_name = "new_scene.scene",
                            start_dir = "game/assets",
                            filters = file_filters("Scene Files|scene|All Files|")
                        ),
                        @(path : string; ctx : string) : void {
                            SetScenePath(get_file_name_without_extension(path), path)
                            StartTask(SaveCurrentScene())
                        }
                    )
                } else {
                    StartTask(SaveCurrentScene())
                }
            }
            // if (menu_label(FILE_SAVE_AS, (text = "Save Scene As...", shortcut = "Ctrl+Shift+S"))) {
            //     open_editor_file_dialog(
            //         FileDialogConfig(
            //             title = "Save Scene As",
            //             mode = FileDialogMode.save,
            //             default_name = GetScenePath() != "" ? GetScenePath() : "new_scene.scene",
            //             filters = file_filters("Scene Files|scene|All Files|")
            //         ),
            //     )
            // }
            separator(FILE_SEP_1)
            menu_item(FILE_AUTOSAVE, (text = "Auto-Save"))
            separator(FILE_SEP_2)
            menu_label(FILE_EXIT, (text = "Exit", shortcut = "Alt+F4"))

        }

        menu(EDIT_MENU, (text = "Edit", enabled = true)) {
            menu_label(EDIT_UNDO, (text = "Undo", shortcut = "Ctrl+Z"))
            menu_label(EDIT_REDO, (text = "Redo", shortcut = "Ctrl+Y"))
            separator(EDIT_SEP_1)
            menu_label(EDIT_CUT, (text = "Cut", shortcut = "Ctrl+X"))
            menu_label(EDIT_COPY, (text = "Copy", shortcut = "Ctrl+C"))
            menu_label(EDIT_PASTE, (text = "Paste", shortcut = "Ctrl+V"))
        }

        menu(WINDOW_MENU, (text = "Window", enabled = true)) {
            menu_label(WIN_HEADER, (text = "(Layout Panels)", enabled = false))
            menu_label(WIN_HIERARCHY, (text = "Hierarchy", selected = true))
            menu_label(WIN_INSPECTOR, (text = "Inspector", selected = true))
            menu_label(WIN_ASSET_BROWSER, (text = "Asset Browser", selected = true))
            menu_label(WIN_SCENE_VIEW, (text = "Scene Viewport", selected = true))
            menu_label(WIN_QUADTREE, (text = "QuadTree Debug", selected = true))
            menu_label(WIN_BUILD_MGR, (text = "Build Manager", selected = true))
            menu_label(WIN_PAK_MGR, (text = "Pak Manager", selected = true))
        }

        separator(MAIN_MENU_SEP_1)
        DrawPlayControls()
    }
}

def DrawDockSpace() {

}

def get_file_name_without_extension(path : string) : string {
    var name = path
    let last_slash = max(rfind(name, "/"), rfind(name, "\\"))
    if (last_slash >= 0) {
        name = slice(name, last_slash + 1)
    }
    let last_dot = rfind(name, ".")
    if (last_dot > 0) {
        name = slice(name, 0, last_dot)
    }
    return name
}

def DrawPlayControls() {
    assume style    = GetStyle()
    let pad      = style.FramePadding.x
    let spacing  = style.ItemSpacing.x

    // 1. Считаем, сколько места займут кнопки в текущем режиме
    var total_w = 0.0f
    if (!is_playing) {
        total_w = CalcTextSize("Play").x + pad * 2.0f
    } else {
        let pause_txt = is_paused ? "Continue" : "Pause"
        total_w = CalcTextSize(pause_txt).x    + pad * 2.0f + spacing + CalcTextSize("One frame").x  + pad * 2.0f + spacing + CalcTextSize("Stop").x       + pad * 2.0f
    }

    // 2. Ставим курсор так, чтобы кнопки прижались к правому краю с отступом
    let win_w  = GetWindowWidth()
    let margin = 12.0f
    SetCursorPosX(win_w - total_w - margin)

    // 3. Рисуем кнопки как раньше
    if (!is_playing) {
        if (button(PLAY_BTN, (text = "Play"))) { StartPlay() }
    } else {
        if (button(PAUSE_BTN, (text = is_paused ? "Continue" : "Pause"))) {
            is_paused = !is_paused
            if (is_paused) { PausePlay() } else { ResumePlay() }
        }
        same_line()
        if (button(STEP_BTN, (text = "Step frame"))) { StepPlay() }
        same_line()
        if (button(STOP_BTN, (text = "Stop"))) { StopPlay() }
    }
}

def StartPlay() {
    // save_decs_state()
    let data <- mem_archive_save(decsState)
    live_store_bytes("__decs_state", data)

    is_playing = true
    is_paused  = false
    // TODO: снимок состояния редактора
    GameStart()
}

def StopPlay() {
    is_playing = false
    is_paused  = false
    // restore_decs_state()
    var data : array<uint8>
    if (live_load_bytes("__decs_state", data)) {
        data |> mem_archive_load(decsState)
    }

    // TODO: восстановить состояние
    // TODO: сообщить движку «выйти из play-mode»
}

def PausePlay()  { is_paused = true}
def ResumePlay() { is_paused = false }
def StepPlay()   { /* TODO */ }

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
    DrawMainMenu()
    dockspace(MAIN_DOCK_SPACE, (flags = ImGuiDockNodeFlags.None)) {}

    DrawHierarchy()
    DrawInspector()
    DrawAssetBrowser()
    DrawSceneViewPort()
    DrawGameSceneViewPort()
    DrawBuildManager()
    // DrawQuadTreeDebug()

    ProcessFileDialog()

    end_of_frame()
}