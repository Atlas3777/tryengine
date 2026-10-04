options gen2

module PakManager shared public

require imgui
require imgui/imgui_widgets_builtin
require imgui/imgui_containers_builtin
require imgui/imgui_layout_builtin
require imgui/imgui_id_builtin
require imgui/imgui_boost_runtime
require imgui/imgui_file_dialog

struct Pak {
    version : uint
}

var all_packs : array<Pak>

var private g_status = "(nothing selected yet)"

def private das_filters() : array<FileDialogFilter> {
    return <- file_filters("daslang (*.das)|das|Images (*.png *.jpg)|png;jpg;jpeg|All files|")
}

def DrawBuildManager(){
    window(BuildManager, (text = "BuildManager", closable = false, flags = ImGuiWindowFlags.None)){
        text("Pick scene")
        // Выпадающий список сцен
        text(LEAD, (text = "Open the native file dialog in each mode:"))
        if (button(BTN_OPEN, (text = "Open..."))) {
            file_dialog_open(FileDialogConfig(title = "Open File", start_dir = "",
                filters <- das_filters(), mode = FileDialogMode.open, max_selection = 0))
        }
        same_line()
        if (button(BTN_SAVE, (text = "Save As..."))) {
            // default_name pre-fills the filename field
            file_dialog_open(FileDialogConfig(title = "Save File As", start_dir = "",
                filters <- das_filters(), mode = FileDialogMode.save, max_selection = 1,
                default_name = "untitled.das"))
        }
        same_line()
        if (button(BTN_CREATE, (text = "New..."))) {
            file_dialog_open(FileDialogConfig(title = "New File", start_dir = "",
                filters <- das_filters(), mode = FileDialogMode.create, max_selection = 1))
        }
        same_line()
        if (button(BTN_PICKDIR, (text = "Pick Folder..."))) {
            file_dialog_open(FileDialogConfig(title = "Select Folder", start_dir = "",
                mode = FileDialogMode.pick_dir))
        }
        separator(SEP)
        text(RESULT, (text = g_status))

        if (button(PakCreate, (text = "Build"))){

        }

        let r = file_dialog_draw()
        if (r == FileDialogResult.confirmed) {
            var sel <- file_dialog_selection()
            g_status = "picked {length(sel)} item(s): {file_dialog_selected_path()}"
            delete sel
        } elif (r == FileDialogResult.cancelled) {
            g_status = "cancelled"
        }
    }
}

def DrawPakManager(){
    window(PakManager, (text = "PakManager", closable = false, flags = ImGuiWindowFlags.None)){
        if (button(PakCreate, (text = "Create"))){
            all_packs |> push(Pak())
        }
    }
}