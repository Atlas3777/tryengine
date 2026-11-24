options gen2
options relaxed_assign = false

module EditorUI shared public

require tryengine/Scene
require tryengine/Assets
require tryengine/AsyncTaskManager
require strings
require math

require tryeditor/Editor
require tryeditor/PlayMode
require trygame/Game
require tryeditor/HotReload

require tryeditor/Hierarchy
require tryeditor/Inspector
require tryeditor/AssetBrowser
require tryeditor/SceneViewPort
require tryeditor/UiScreens
require tryeditor/AssetSettings
require tryeditor/QuadTreeDebugWindow
require tryeditor/BuildManager
require tryeditor/Material
require tryeditor/PrefabAuthoring
require tryeditor/ProfilerWindow
require tryeditor/Gizmo
require tryeditor/ProBuilder
require tryeditor/EditorActions

require imgui
require imgui/imgui_containers_builtin
require imgui/imgui_widgets_builtin
require imgui/imgui_docking_builtin
require imgui/imgui_boost_v2
require imgui/imgui_file_dialog

require TryEditor
require tryengine/Profiling
require tryengine/EcsMacro
require daslib/decs

[profile]
def DrawMainMenu() {
    main_menu_bar(MAIN_MENU_BAR) {
        menu(FILE_MENU, (text = "File", enabled = true)) {
            if (menu_label(FILE_NEW, (text = "New Scene", shortcut = "Ctrl+N"))) {
                Scene::Create("")
            }
            if (menu_label(FILE_OPEN, (text = "Open Scene...", shortcut = "Ctrl+O"))) {
                open_editor_file_dialog(
                    FileDialogConfig(
                        title = "Open Scene",
                        mode = FileDialogMode.open,
                        start_dir = "game/content",
                        filters <- file_filters("Scene Files|scene|All Files|")
                    ),
                    @(path : string; ctx : string) : void {
                        // A dialog returns an absolute path; the asset API is path-id based,
                        // so register it before the scene reader asks for it.
                        Assets::RegisterPath(path)
                        StartTask(Scene::LoadAsync(path))
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
                            start_dir = "game/content",
                            filters <- file_filters("Scene Files|scene|All Files|")
                        ),
                        @(path : string; ctx : string) : void {
                            SetScenePath(path)
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
            if (menu_label(FILE_SAVE_PREFAB, (text = "Save Selection as Prefab..."))) {
                SaveSelectionAsPrefab()
            }
            if (menu_label(FILE_RELOAD_SCRIPTS, (text = "Reload Scripts", shortcut = "F5"))) {
                ReloadScripts()
            }

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
            menu_label(WIN_MATERIAL, (text = "Material", selected = true))
            menu_label(WIN_PROFILER, (text = "Profiler", selected = true))
            menu_label(WIN_PROBUILDER, (text = "ProBuilder", selected = true))
            menu_label(WIN_PAK_MGR, (text = "Pak Manager", selected = true))
        }

        menu(GIZMO_MENU, (text = "Gizmo", enabled = true)) {
            if (menu_label(GIZMO_MOVE_ITEM,
                           (text = "Move", shortcut = "W", selected = gizmo_mode == GizmoMode.Translate))) {
                SetGizmoMode(GizmoMode.Translate)
            }
            if (menu_label(GIZMO_ROTATE_ITEM,
                           (text = "Rotate", shortcut = "E", selected = gizmo_mode == GizmoMode.Rotate))) {
                SetGizmoMode(GizmoMode.Rotate)
            }
            if (menu_label(GIZMO_SCALE_ITEM,
                           (text = "Scale", shortcut = "R", selected = gizmo_mode == GizmoMode.Scale))) {
                SetGizmoMode(GizmoMode.Scale)
            }
            separator(GIZMO_SEP_1)
            if (menu_label(GIZMO_LOCAL_ITEM,
                           (text = "Local Space", shortcut = "X", selected = gizmo_space == GizmoSpace.Local))) {
                ToggleGizmoSpace()
            }
            if (menu_label(GIZMO_DISTANCE_ITEM,
                           (text = "Scale With Distance", shortcut = "V",
                            selected = gizmo_scale_with_distance))) {
                ToggleGizmoScaleWithDistance()
            }
        }

        separator(MAIN_MENU_SEP_1)
        if (button(RELOAD_SCRIPTS_BTN, (text = "Reload Scripts (F5)"))) {
            ReloadScripts()
        }
        same_line()
        DrawPlayControls()
    }
}

def DrawDockSpace() {

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
        if (button(PLAY_BTN, (text = "Play"))) { BeginPlay() }
    } else {
        if (button(PAUSE_BTN, (text = is_paused ? "Continue" : "Pause"))) {
            if (is_paused) { ResumePlay() } else { PausePlay() }
        }
        same_line()
        if (button(STEP_BTN, (text = "Step frame"))) { StepPlay() }
        same_line()
        if (button(STOP_BTN, (text = "Stop"))) { EndPlay() }
    }
}

[export, profile]
def DrawEditor() {
    unsafe {
        let ctx_address = GetEditorImGuiContext()
        if (ctx_address == null){ print("imgui is null")}
        let host_ctx = reinterpret<ImGuiContext?>(ctx_address)
        host_ctx.SetCurrentContext()
    }
    begin_frame()
    DrawMainMenu()
    dockspace(MAIN_DOCK_SPACE, (flags = ImGuiDockNodeFlags.None)) {}

    DrawHierarchy()
    DrawInspector()
    DrawAssetBrowser()
    DrawProBuilder()
    DrawSceneViewPort()
    UpdateProBuilderPlacement()
    DrawGameSceneViewPort()
    DrawUiScreens(is_playing)
    DrawAssetSettings()
    DrawBuildManager()
    DrawMaterialEditor()
    DrawProfilerWindow()
    DrawPlotWindow()
    DrawScopesWindow()
    // DrawQuadTreeDebug()

    ProcessFileDialog()

    end_of_frame()
}
