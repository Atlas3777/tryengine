options gen2
options relaxed_assign = false

module GameScreens shared public

//! The game's screen stack and its navigation policy.
//!
//! `Ui::ScreenStack` is the mechanism: it stores `ScreenRef` values (a build function plus the
//! flat asset map it draws with) and draws them bottom first. There are no names and no `[init]`
//! registration; a screen is identified by its function pointer, which compares with `==`.
//! This module is the policy: the stack never pops its bottom layer and the HUD is the layer a
//! closing menu returns to.

require tryengine/Ui

var private g_stack : Ui::ScreenStack

def public Has(build : Ui::UiScreenBuild) : bool {
    return g_stack.Has(build)
}

def public Top() : Ui::ScreenRef {
    return g_stack.Top()
}

//! Replaces the stack with one screen: the main menu at startup, the HUD after a scene load.
def public Show(ref : Ui::ScreenRef) {
    g_stack.Show(ref)
}

def public Push(ref : Ui::ScreenRef) {
    g_stack.Push(ref)
}

//! Removes the top layer. The bottom layer stays, so the game always has a screen.
def public Pop() {
    if (length(g_stack.layers) > 1) {
        g_stack.Pop()
    }
}

//! Removes every layer built by `build` (a screen that is no longer valid).
def public Remove(build : Ui::UiScreenBuild) {
    g_stack.Remove(build)
}

//! Draws the stack, bottom first. Call inside `Ui::Frame`.
def public Frame() {
    g_stack.Draw()
}
