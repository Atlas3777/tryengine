options gen2
options relaxed_assign = false

module GameFlow shared public

//! Scene flow: what is loaded, how to load another scene, and what happens after it.
//!
//! The main menu owns the list of entries (`ScreenMainMenu.das`); this module only knows how to
//! load one entry: request the scene (and an optional prefab) through the loader, apply it from
//! the cache, reset the physics world and switch the UI to the HUD.

require daslib/decs
require math
require tryengine/AsyncTaskManager
require tryengine/Loader
require tryengine/Physics
require tryengine/Prefab
require tryengine/Scene
require tryengine/Time
require trygame/GameScreens
require trygame/GameTime
require trygame/PhysicsDemo

//! One entry of the main menu: what to load and what to spawn after it.
struct public SceneEntry {
    title : string
    scene : string
    prefab : string
    physics_demo : bool
}

var private g_ready = false
var private g_pending : SceneEntry

//! True between a successful scene load and the next `Start`.
def public IsReady() : bool {
    return g_ready
}

def public IsLoading() : bool {
    return Loader::IsActive()
}

//! Fresh game session: no scene, the main menu on screen, physics attached to the game clock.
def public Start() {
    Physics::Reset()
    Physics::Attach(game_world_clock)
    PhysicsDemo::Reset()
    g_ready = false
    GameScreens::Show("main_menu")
}

//! Loads `entry` through the loader and switches to the HUD when the scene is applied. One
//! request at a time; the menu disables its scene buttons while `IsLoading()`.
def public LoadScene(entry : SceneEntry) {
    if (empty(entry.scene)) {
        return
    }
    if (Loader::IsActive()) {
        print("[GameFlow] a scene load is already running\n")
        return
    }
    g_pending = entry
    var request <- LoaderRequest()
    request.AddScene(entry.scene)
    if (!empty(entry.prefab)) {
        request.AddPrefab(entry.prefab)
    }
    StartTask(Loader::Load(request, @@OnSceneLoaded))
}

//! Restores the flow after an editor hot reload: the world is already there, only the flag and
//! the screen stack are missing.
def public SetReady(ready : bool) {
    g_ready = ready
}

def private OnSceneLoaded() {
    if (!Scene::Load(g_pending.scene)) {
        print("[GameFlow] scene '{g_pending.scene}' failed to apply\n")
        return
    }
    Physics::Reset()
    if (g_pending.physics_demo) {
        PhysicsDemo::Start()
    }
    if (!empty(g_pending.prefab)) {
        let spawned = Prefab::Spawn(g_pending.prefab, float3(10.0f, 0.0f, 0.0f))
        if (spawned == INVALID_ENTITY_ID) {
            print("[GameFlow] failed to spawn prefab '{g_pending.prefab}'\n")
        }
    }
    g_ready = true
    print("[GameFlow] scene '{g_pending.scene}' loaded\n")
    GameScreens::Show("hud")
}
