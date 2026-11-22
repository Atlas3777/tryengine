options gen2
options relaxed_assign = false

module GameScreens shared public

//! The game's screen stack and its navigation policy.
//!
//! `Ui::ScreenStack` is the mechanism: names, order, drawing. This module is the policy on top
//! of it — the stack is never empty, closing a layer falls back to the HUD, and a hot reload
//! restores the layer from `scene_ready`. Screens call these functions instead of touching the
//! stack directly, so a screen never needs to know where it sits in the stack.

require tryengine/Ui

var private g_stack : Ui::ScreenStack

def public Has(name : string) : bool {
    return g_stack.Has(name)
}

def public Top() : string {
    return g_stack.Top()
}

//! Replaces the stack with one screen: the main menu at startup, the HUD after a scene load.
def public Show(name : string) {
    g_stack.Show(name)
}

def public Push(name : string) {
    g_stack.Push(name)
}

//! Removes the top layer. The bottom layer stays, so the game always has a screen.
def public Pop() {
    if (length(g_stack.layers) > 1) {
        g_stack.Pop()
    }
}

def public Close(name : string) {
    g_stack.Remove(name)
    if (g_stack.IsEmpty()) {
        g_stack.Show("hud")
    }
}

//! Draws the stack, bottom first. Call inside `Ui::Frame`.
def public Frame() {
    g_stack.Draw()
}
