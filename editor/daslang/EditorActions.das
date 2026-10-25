options gen2

module EditorActions shared public

require math
require imgui/imgui_file_dialog
require strings

enum public PendingAction {
    none
    open_scene
    save_scene
    select_pak_output
}

var private g_pending_action = PendingAction.none
var private g_context_data = ""

def public open_editor_file_dialog(action : PendingAction; cfg : FileDialogConfig; context_data : string = "") {
    g_pending_action = action
    g_context_data = context_data
    file_dialog_open(cfg)
}

def public get_pending_action() : PendingAction {
    return g_pending_action
}

def public get_pending_context_data() : string {
    return g_context_data
}

def public reset_pending_action() {
    g_pending_action = PendingAction.none
    g_context_data = ""
}

def public get_file_name_without_extension(path : string) : string {
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