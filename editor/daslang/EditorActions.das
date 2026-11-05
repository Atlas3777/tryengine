options gen2

module EditorActions shared public

require imgui/imgui_file_dialog
require strings

typedef FileDialogCallback = lambda<(path : string; context : string) : void>

var private g_on_confirmed : FileDialogCallback

def public open_editor_file_dialog(cfg : FileDialogConfig;
                                   var on_confirmed : FileDialogCallback)
{
    g_on_confirmed = on_confirmed
    file_dialog_open(cfg)
}

def public ProcessFileDialog() {
    let res = file_dialog_draw()
    if (res == FileDialogResult.confirmed) {
        let path = file_dialog_selected_path()
        invoke(g_on_confirmed, path, "some_ctx")
    }
}