options gen2

module EditorHotkeys shared public

require tryengine/Input
require tryeditor/EditorInput

// Общая система горячих клавиш редактора.
//
// Модули регистрируют действия через RegisterHotkey, редактор раз в кадр вызывает
// ProcessHotkeys. Пока ввод захвачен game viewport, срабатывают только системные
// хоткеи (system = true) — например, Escape.

typedef HotkeyAction = function<() : void>

struct Hotkey {
    key : Key
    action : HotkeyAction
    name : string
    ctrl : bool
    shift : bool
    alt : bool
    system : bool
}

var private g_hotkeys : array<Hotkey>

def RegisterHotkey(key : Key; action : HotkeyAction; name : string;
                   ctrl : bool = false; shift : bool = false; alt : bool = false;
                   system : bool = false) {
    g_hotkeys |> push(Hotkey(
        key = key,
        action = action,
        name = name,
        ctrl = ctrl,
        shift = shift,
        alt = alt,
        system = system
    ))
}

def private IsCtrlDown() : bool => is_down(Key.LCtrl) || is_down(Key.RCtrl)
def private IsShiftDown() : bool => is_down(Key.LShift) || is_down(Key.RShift)
def private IsAltDown() : bool => is_down(Key.LAlt) || is_down(Key.RAlt)

def ProcessHotkeys() {
    let captured = IsGameViewportFocused()

    for (h in g_hotkeys) {
        if ((captured && !h.system)
            || !pressed(h.key)
            || IsCtrlDown() != h.ctrl
            || IsShiftDown() != h.shift
            || IsAltDown() != h.alt) {
            continue
        }
        invoke(h.action)
    }
}
