options gen2
options relaxed_assign = false
options rtti = true

module Game shared public

require tryengine/Input
require tryengine/Profiling
require tryengine/Ui
require trygame/GameFlow
require trygame/GameScreens
require trygame/GameTime public
require trygame/GameUi
require trygame/GameState
require trygame/Player
require trygame/ScreenHud
require trygame/UiTheme

//! Game state that must survive an editor hot reload. The editor saves it through
//! Editor HotReload; the standalone game never calls these.
struct public GameReloadState {
    scene_ready : bool
}

def public CaptureReloadState() : GameReloadState {
    return GameReloadState(scene_ready = GameFlow::IsReady())
}

def public ApplyReloadState(var state : GameReloadState) {
    // The script context is new: the theme is gone with it and the font keys would resolve to
    // nothing. Install it again before the screens are rebuilt.
    Ui::SetTheme(UiTheme::kTheme)
    GameFlow::SetReady(state.scene_ready)
    // The script context is new after a reload: the screen stack and its asset maps are gone,
    // the clocks are gone. Rebuild the stack through the loader (the world itself is preserved).
    GameUi::Restore(state.scene_ready)
    if (state.scene_ready) {
        RegisterGameTime()
    }
}

// Explicit entry point, shared by the standalone game and editor Play. The scene is loaded
// from the main menu (`GameFlow::LoadScene`), not here: a new session starts at the menu.
[export]
def GameStart(){
    print("Game Start\n")
    RegisterGameTime()
    // The theme is a plain value with font/material keys; it is installed explicitly, not from
    // an `[init]` side effect. Keys resolve against each screen's asset map.
    Ui::SetTheme(UiTheme::kTheme)
    GameFlow::Start()
    GameUi::Start()
}

//! The UI is declared every frame on its own clock, separately from gameplay. The editor calls
//! this even while the game clock is paused, so a pause menu stays alive over a frozen world;
//! the game client calls it every frame too. Gameplay never calls it.
[export]
def GameUiFrame(){
    GameUi::Frame()
}

[export, profile]
def GameUpdate() {
    if (!GameFlow::IsReady()) {
        return
    }
    // Escape is the game's pause key: the pause screen is drawn by GameUiFrame, and pausing
    // releases the mouse so the menu can be clicked.
    if (pressed(Key.Escape)) {
        GameState::TogglePause()
    }
    if (GameTime::IsGamePaused() || GameScreens::Top().build != @@ScreenHud::Draw) {
        Player::ReleaseMouse()
        return
    }
    UpdateGameplay()
}

//! Gameplay runs on the world clock, so a paused game freezes the player. The player system is
//! component-driven: it drives the entity that has PlayerInput + CharacterController and the
//! game camera, and does nothing when the scene has no player. It also captures the mouse; any
//! screen above the HUD (pause, shop) releases it.
def private UpdateGameplay() {
    Player::Update()
}
