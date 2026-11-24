options gen2
options relaxed_assign = false

module EditorInput shared public

// Состояние захвата ввода вьюпортами. Заполняется из SceneViewPort на кадре ImGui,
// читается камерой редактора (Mover) и системой горячих клавиш (EditorHotkeys).

var scene_viewport_hovered : bool = false
var game_viewport_focused : bool = false
// Set by a viewport tool that owns the mouse (ProBuilder). SceneViewPort suspends the gizmo,
// and the tool itself consumes the click before PickingPass reads it.
var placement_active : bool = false

def SetSceneViewportHovered(value : bool) {
    scene_viewport_hovered = value
}

def SetGameViewportFocused(value : bool) {
    game_viewport_focused = value
}

def SetPlacementActive(value : bool) {
    placement_active = value
}

def IsSceneViewportHovered() : bool => scene_viewport_hovered

def IsGameViewportFocused() : bool => game_viewport_focused

def IsPlacementActive() : bool => placement_active
