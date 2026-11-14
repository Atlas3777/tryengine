options gen2

module EditorInput shared public

// Состояние захвата ввода вьюпортами. Заполняется из SceneViewPort на кадре ImGui,
// читается камерой редактора (Mover) и системой горячих клавиш (EditorHotkeys).

var scene_viewport_hovered : bool = false
var game_viewport_focused : bool = false

def SetSceneViewportHovered(value : bool) {
    scene_viewport_hovered = value
}

def SetGameViewportFocused(value : bool) {
    game_viewport_focused = value
}

def IsSceneViewportHovered() : bool => scene_viewport_hovered

def IsGameViewportFocused() : bool => game_viewport_focused
