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
require tryengine/Prefab
require tryengine/Profiling
require tryengine/Scene
require tryengine/Ui
require trygame/GameScreens
require trygame/GameState
require trygame/GameTime public
require trygame/GameUi
require trygame/PhysicsDemo
require trygame/Player
require trygame/ScreenHud
require trygame/ScreenPause
require trygame/ScreenShop
require trygame/UiTheme

//! The game: entry points, the scene flow and gameplay.
//!
//! The scene flow lives here because the game is the only module that may see both the UI and
//! the engine loader: `Game` imports `GameUi`, so a screen cannot import `Game` back. The main
//! menu therefore does not call the game — it fills a `GameState::SceneRequest`, and the game
//! drains it in its own frame (`ProcessSceneRequest`). The menu also never loads: the game
//! builds a `LoaderRequest`, and the callback applies the scene synchronously from the cache.
//!
//! Asynchrony lives only in `Loader::Load`. Everything after the callback — `Scene::Load`,
//! `Prefab::Spawn` — is a synchronous cache hit, because the loader already read, compiled and
//! waited for the resources of everything the request named.

//! Game state that must survive an editor hot reload. The editor saves it through
//! Editor HotReload; the standalone game never calls these.
struct public GameReloadState {
    scene_ready : bool
}

var private g_ready = false
var private g_pending : GameState::SceneRequest

def public CaptureReloadState() : GameReloadState {
    return GameReloadState(scene_ready = g_ready)
}

def public ApplyReloadState(var state : GameReloadState) {
    // The script context is new: the theme is gone with it and the font keys would resolve to
    // nothing. Install it again before the screens are rebuilt.
    Ui::SetTheme(UiTheme::kTheme)
    g_ready = state.scene_ready
    // The script context is new after a reload: the screen stack and its asset maps are gone,
    // the clocks are gone. Rebuild the stack through the loader (the world itself is preserved).
    GameUi::Restore(state.scene_ready)
    if (state.scene_ready) {
        RegisterGameTime()
    }
}

//! Fresh game session: no scene, physics attached to the game clock. The UI boot (menu bundle,
//! theme, first screen) belongs to `GameUi::Start`.
def private StartSession() {
    Physics::Reset()
    Physics::Attach(game_world_clock)
    PhysicsDemo::Reset()
    Player::Reset()
    g_ready = false
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

    var entry = GameState::TakeSceneRequest()
    if (empty(entry.scene)) {
        return
    }
    if (empty(Content::Find(DocumentKind.Scene, entry.scene))) {
        print("[GAME] scene '{entry.scene}' was not found\n")
        return
    }

    g_pending = entry
    var request <- LoaderRequest()
    request.AddScene(entry.scene)
    // The HUD is the screen that appears after the scene; its bundle, and the bundles of the
    // layers it can open, ride in the same request. `ScreenHud::Show` receives their maps.
    request.AddBundle(ScreenHud::kHudBundle)
    request.AddBundle(ScreenPause::kPauseBundle)
    request.AddBundle(ScreenShop::kShopBundle)
    if (!empty(entry.prefab)) {
        request.AddPrefab(entry.prefab)
    }
    StartTask(Loader::Load(request, @@OnSceneLoaded))
}

// The loader has read, compiled and waited for everything the request named, so this is the
// synchronous part: apply the scene, reset the world state, spawn what the entry asked for.
def private OnSceneLoaded(maps : table<string; AssetMap>) {
    if (!Scene::Load(g_pending.scene)) {
        print("[GAME] scene '{g_pending.scene}' failed to apply\n")
        return
    }
    Physics::Reset()
    // Reset detaches the simulation; a loaded scene starts with a live world again.
    Physics::Attach(game_world_clock)
    Player::Reset()
    if (g_pending.physics_demo) {
        PhysicsDemo::Start()
    }
    if (!empty(g_pending.prefab)) {
        let spawned = Prefab::Spawn(g_pending.prefab, float3(10.0f, 0.0f, 0.0f))
        if (spawned == INVALID_ENTITY_ID) {
            print("[GAME] failed to spawn prefab '{g_pending.prefab}'\n")
        }
    }
    g_ready = true
    print("[GAME] scene '{g_pending.scene}' loaded\n")
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

//! Gameplay runs on the world clock, so a paused game freezes the player. The player system is
//! component-driven: it drives the entity that has PlayerInput + CharacterController and the
//! game camera, and does nothing when the scene has no player. It also captures the mouse; any
//! screen above the HUD (pause, shop) releases it.
def private UpdateGameplay() {
    Player::Update()
}
