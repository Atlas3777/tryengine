options gen2
options relaxed_assign = false
options rtti = true

module Game shared public

require daslib/decs
require math
require tryengine/AssetMap
require tryengine/AsyncTaskManager
require tryengine/Content
require tryengine/Input
require tryengine/Loader
require tryengine/Physics
require tryengine/Profiling
require tryengine/Scene
require tryengine/Ui
require trygame/GameScreens
require trygame/GameState
require trygame/GameTime public
require trygame/GameUi
require trygame/Player
require trygame/ScreenHud
require trygame/ScreenPause
require trygame/ScreenShop
require trygame/UiTheme


//! A scene is applied and gameplay runs. `@live`: the world and the scene come back after a
//! reload, so the flag must come back with them; the reload coordinator reads it before the
//! swap and rebuilds the game UI from it. Nothing in this module knows about reload.
var private @live g_ready = false
//! The scene request that is being loaded right now ("" when idle).
var private @live g_scene = ""

//! Whether a scene is applied and gameplay runs. A normal query, not a reload hook.
def public IsReady() : bool {
    return g_ready
}

//! Fresh game session: no scene, physics attached to the game clock. The UI boot (menu bundle,
//! theme, first screen) belongs to `GameUi::Start`.
def private StartSession() {
    Physics::Reset()
    Physics::Attach(game_world_clock)
    Player::Reset()
    g_ready = false
    g_scene = ""
}

// Drains the scene the UI asked for. One request at a time: while the loader is busy the
// request stays pending and the menu shows that a load is running.
def private ProcessSceneRequest() {
    // A failed request leaves the loader active so `Error()` can be read; release it, or the
    // menu would show "Loading..." forever and no further request would start.
    if (Loader::IsFailed()) {
        print("[GAME] scene load failed: {Loader::Error()}\n")
        Loader::Abort()
        return
    }
    if (Loader::IsActive()) {
        return
    }

    let scene = GameState::TakeSceneRequest()
    if (empty(scene)) {
        return
    }
    if (empty(Content::Find(DocumentKind.Scene, scene))) {
        print("[GAME] scene '{scene}' was not found\n")
        return
    }

    g_scene = scene
    var request <- LoaderRequest()
    request.AddScene(scene)
    // The HUD is the screen that appears after the scene; its bundle, and the bundles of the
    // layers it can open, ride in the same request. `ScreenHud::Show` receives their maps.
    request.AddBundle(ScreenHud::kHudBundle)
    request.AddBundle(ScreenPause::kPauseBundle)
    request.AddBundle(ScreenShop::kShopBundle)
    StartTask(Loader::Load(request, @@OnSceneLoaded))
}

// The loader has read, compiled and waited for everything the request named, so this is the
// synchronous part: apply the scene and reset the world state.
def private OnSceneLoaded(maps : table<string; AssetMap>) {
    if (!Scene::Load(g_scene)) {
        print("[GAME] scene '{g_scene}' failed to apply\n")
        g_scene = ""
        return
    }
    Physics::Reset()
    // Reset detaches the simulation; a loaded scene starts with a live world again.
    Physics::Attach(game_world_clock)
    Player::Reset()
    g_ready = true
    print("[GAME] scene '{g_scene}' loaded\n")
    g_scene = ""
    ScreenHud::Show(maps)
}

// Explicit entry point, shared by the standalone game and editor Play. The scene is loaded
// from the main menu, not here: a new session starts at the menu.
[export]
def GameStart(){
    print("Game Start\n")
    RegisterGameTime()
    // The theme is a plain value with font/material keys; it is installed explicitly, not from
    // an `[init]` side effect. Keys resolve against each screen's asset map.
    Ui::SetTheme(UiTheme::kTheme)
    StartSession()
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
    // The request comes from a screen, so it is drained before the ready check: the menu is
    // exactly the state where no scene is ready yet.
    ProcessSceneRequest()

    if (!g_ready) {
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

def private UpdateGameplay() {
    Player::Update()
}
