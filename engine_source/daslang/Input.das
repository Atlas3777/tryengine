options gen2

module Input shared public

require tryInput public

var g_input : InputState?

def update_input() {
    g_input = get_input_state()
}

def is_down(key : Key) : bool {
    return g_input.isDown[int(key)]
}

def pressed(key : Key) : bool {
    return g_input.justPressed[int(key)]
}

def released(key : Key) : bool {
    return g_input.justReleased[int(key)]
}

def is_mouse_down(btn : Mouse) : bool {
    return g_input.mouseButtons[int(btn)]
}

def mouse_pressed(btn : Mouse) : bool {
    return g_input.mouseJustPressed[int(btn)]
}

def mouse_released(btn : Mouse) : bool {
    return g_input.mouseJustReleased[int(btn)]
}

def mouse_pos() : float2 {
    return float2(g_input.mouseX, g_input.mouseY)
}

def mouse_delta() : float2 {
    return float2(g_input.mouseDeltaX, g_input.mouseDeltaY)
}