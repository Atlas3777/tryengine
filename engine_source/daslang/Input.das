options gen2
options relaxed_assign = false

module Input shared public

require tryInput public

var g_input : InputState?

def SetInputSource() {
    g_input = get_input_state()
}

//! True when the engine input source is attached (EngineStart). Headless scripts and tools run
//! without it and must not dereference the state.
def public HasInputSource() : bool {
    return g_input != null
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

//! Captures or releases the mouse: a captured mouse hides the cursor and keeps mouse_delta
//! coming from relative motion, so look controls never stop at a window edge. The game decides
//! when to call it (gameplay captures, menus and pause release). Returns false when the window
//! does not have keyboard focus, in which case the mode did not change.
def public SetMouseCaptured(captured : bool) : bool {
    return set_relative_mouse_mode(captured)
}
