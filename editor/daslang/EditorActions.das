options gen2

module EditorActions shared public

require imgui/imgui_file_dialog
require strings

typedef FileDialogCallback = lambda<(path : string; context : string) : void>

var private g_on_confirmed : FileDialogCallback
// var private g_on_cancelled : FileDialogCallback? = null

def public open_editor_file_dialog(cfg : FileDialogConfig;
                                   var on_confirmed : FileDialogCallback)
                                //    var context : string = "";
                                //    var on_cancelled : FileDialogCallback? = null)
{
    g_on_confirmed = on_confirmed
    // g_on_cancelled = on_cancelled
    // g_context      = context
    file_dialog_open(cfg)
}

def public ProcessFileDialog() {
    let res = file_dialog_draw()
    if (res == FileDialogResult.confirmed) {
        let path = file_dialog_selected_path()
        invoke(g_on_confirmed, path, "some_ctx")
        // g_on_confirmed = null
        // g_on_cancelled = null
    }
    // elif (res == FileDialogResult.cancelled) {
    //     invoke(g_on_confirmed ?? @(path : string; context : string) {}, "", g_context)
    //     // g_on_confirmed = null
    //     g_on_cancelled = null
    // }
}