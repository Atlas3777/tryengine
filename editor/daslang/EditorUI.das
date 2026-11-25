options gen2
options relaxed_assign = false

module EditorUI shared public

require tryengine/Scene
require tryengine/Assets
require tryengine/AsyncTaskManager
require tryengine/Loader
require daslib/async_boost
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
require tryeditor/AssetSettings
require tryeditor/QuadTreeDebugWindow
require tryeditor/BuildManager
require tryeditor/Material
require tryeditor/PrefabAuthoring
require tryeditor/ProfilerWindow
require tryeditor/LogWindow
require tryeditor/Gizmo
require tryeditor/ProBuilder
require tryeditor/ColliderDebug
require tryeditor/DebugRender
require tryeditor/EditorActions
require tryeditor/EditorCommands
require tryeditor/UndoRedo

require imgui
require imgui/imgui_containers_builtin
require imgui/imgui_widgets_builtin
require imgui/imgui_docking_builtin
require imgui/imgui_boost_v2
require imgui/imgui_scope_builtin
require imgui/imgui_file_dialog

require TryEditor
require tryengine/Profiling
require tryengine/EcsMacro
require daslib/decs

//! Opens a `.scene` the user picked. The loader is the only reader, so the read and the
//! compile happen there; the apply after it is synchronous. A dialog returns an absolute path
//! and the file may have changed since it was last opened, so it is registered with the asset
//! API and its cached form is dropped before the request.
[async]
def private OpenSceneFile(path : string) : void {
    Assets::RegisterPath(path)

    // A request that failed keeps the loader active until its error is read; release it, or
    // every later open would be refused.
    if (Loader::IsFailed()) {
        print("[EDITOR] content load failed: {Loader::Error()}\n")
        Loader::Abort()
    }
    Scene::Invalidate(path)

    var request <- LoaderRequest()
    request.AddScene(path)
    await(Loader::Load(request))

    if (!Scene::Load(path)) {
        print("[EDITOR] scene '{path}' failed to open\n")
    }
}

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
                        StartTask(OpenSceneFile(path))
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
            // The menu names the step it would undo; the shortcut and the item run the same
            // command function the hotkey does.
            let undo_title = UndoRedo::UndoTitle()
            let redo_title = UndoRedo::RedoTitle()
            if (menu_label(EDIT_UNDO, (text = empty(undo_title) ? "Undo" : "Undo {undo_title}",
                                       shortcut = "Ctrl+Z", enabled = UndoRedo::CanUndo()))) {
                UndoCommand()
            }
            if (menu_label(EDIT_REDO, (text = empty(redo_title) ? "Redo" : "Redo {redo_title}",
                                       shortcut = "Ctrl+Y", enabled = UndoRedo::CanRedo()))) {
                RedoCommand()
            }
            separator(EDIT_SEP_1)
            if (menu_label(EDIT_CUT, (text = "Cut", shortcut = "Ctrl+X", enabled = CanModifySelection()))) {
                CutCommand()
            }
            if (menu_label(EDIT_COPY, (text = "Copy", shortcut = "Ctrl+C", enabled = CanModifySelection()))) {
                CopyCommand()
            }
            if (menu_label(EDIT_PASTE, (text = "Paste", shortcut = "Ctrl+V", enabled = UndoRedo::HasClipboard()))) {
                PasteCommand()
            }
            separator(EDIT_SEP_2)
            if (menu_label(EDIT_DELETE_ITEM, (text = "Delete Entity", shortcut = "Del",
                                              enabled = CanModifySelection()))) {
                DeleteCommand()
            }
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
            menu_label(WIN_LOG, (text = "Log", selected = true))
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
    // A failed reload keeps the old context alive, but starting Play on code that no longer
    // compiles would hide the error behind a running game. Fix the script and reload (F5).
    let healthy  = ScriptsHealthy()

    // 1. Считаем, сколько места займут кнопки в текущем режиме
    var total_w = 0.0f
    if (!is_playing) {
        total_w = CalcTextSize("Play").x + pad * 2.0f
        if (!healthy) {
            total_w += spacing + CalcTextSize("scripts broken - F5").x
        }
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
        with_disabled(!healthy) {
            if (button(PLAY_BTN, (text = "Play"))) { BeginPlay() }
        }
        if (!healthy) {
            same_line()
            text_disabled(PLAY_SCRIPTS_BROKEN, (text = "scripts broken - F5"))
        }
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
    // The debug line buffer is rebuilt once per frame: the collider overlay of the selected
    // entity and the ProBuilder preview both append to it. In Play the physics debug pass
    // draws the real bodies, so the Edit-mode overlay stays off.
    ClearDebugLines()
    if (!is_playing) {
        ColliderDebug::DrawSelectedCollider()
    }
    DrawProBuilder()
    DrawSceneViewPort()
    UpdateProBuilderPlacement()
    DrawGameSceneViewPort()
    DrawAssetSettings()
    DrawBuildManager()
    DrawMaterialEditor()
    DrawProfilerWindow()
    DrawLogWindow()
    DrawPlotWindow()
    DrawScopesWindow()
    // DrawQuadTreeDebug()

    ProcessFileDialog()

    end_of_frame()
}
