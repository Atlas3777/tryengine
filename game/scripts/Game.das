options gen2
options relaxed_assign = false
options rtti = true

module Game shared public

require tryengine/Profiling
require trygame/GameFlow
require trygame/GameScreens
require trygame/GameTime public
require trygame/GameUi
require trygame/Player

//! Game state that must survive an editor hot reload. The editor saves it through
//! Editor HotReload; the standalone game never calls these.
struct public GameReloadState {
    scene_ready : bool
}

def public CaptureReloadState() : GameReloadState {
    return GameReloadState(scene_ready = GameFlow::IsReady())
}

def public ApplyReloadState(var state : GameReloadState) {
    GameFlow::SetReady(state.scene_ready)
    // The script context is new after a reload: the screen stack is empty and the clocks are
    // gone. Restore both from the scene flag.
    GameScreens::Show(state.scene_ready ? "hud" : "main_menu")
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
    GameFlow::Start()
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
    if (!GameTime::IsGamePaused()) {
        UpdateGameplay()
    }
}

//! Gameplay runs on the world clock, so a paused game freezes the player. The player system is
//! component-driven: it drives the entity that has PlayerInput + CharacterController and the
//! game camera, and does nothing when the scene has no player.
def private UpdateGameplay() {
    Player::Update()
}
