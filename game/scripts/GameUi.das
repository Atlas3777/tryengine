options gen2
options relaxed_assign = false

module GameUi shared public

//! The game's screens, one file each, and the frame that draws the stack.
//!
//! Pieces, one concern each:
//! - primitives — `tryengine/Ui` (`Panel`, `Label`, `Button`, `Backdrop`, ...);
//! - composite widgets — `trygame/UiWidgets` (`StatRow`, `MeterRow`);
//! - look — `trygame/UiTheme` (per-kind theme, named styles, button templates, sound names);
//! - screens — `trygame/ScreenHud`, `ScreenPause`, `ScreenShop`, `ScreenMainMenu`;
//! - navigation — `trygame/GameScreens` (the stack, never empty) and `trygame/GameState`
//!   (state and labels the screens share).
//!
//! This module is the manifest: requiring a screen module runs its `[init]`, which registers the
//! screen in `tryengine/Ui`. The editor's Screens panel lists them all without Play. `Frame` is
//! the only entry point; the stack decides what is drawn.

require tryengine/Ui
require trygame/GameScreens
require trygame/ScreenHud
require trygame/ScreenMainMenu
require trygame/ScreenPause
require trygame/ScreenShop

//! Declares every layer of the stack, bottom first.
def public Frame() {
    Ui::Frame() {
        GameScreens::Frame()
    }
}
