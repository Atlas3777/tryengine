options gen2
options relaxed_assign = false
options rtti = true

module Game shared public

require daslib/decs
require daslib/decs_boost
require tryengine/AsyncTaskManager
require tryengine/Input
require tryengine/Loader
require tryengine/Physics
require tryengine/Prefab
require tryengine/Profiling
require tryengine/Scene
require tryengine/Time
require tryengine/Types
require trygame/GameUi
require trygame/PhysicsDemo
require math

// === engine convention ===
let front_raw = float3(0.0f, 1.0f, 0.0f)   // local Y (forward)
let up_raw    = float3(0.0f, 0.0f, 1.0f)   // local Z (up)
let world_up  = float3(0.0f, 0.0f, 1.0f)

// === content loaded at startup ===
// The request is the whole startup policy: everything listed here is read and compiled
// before `OnLoaded` runs. `OnLoaded` then applies the scene and spawns gameplay from the
// cache; neither `Scene::Load` nor `Prefab::Spawn` touches the disk.
let STARTUP_SCENE = "content/test.scene"
let STARTUP_BUNDLE = "data/bundles/startup.bundle"
let GAMEPLAY_PREFAB = "prefabl.prefab"

// === orbit parameters ===
var  orbit_angle  = 0.0f
let  orbit_center = float3(0.0f, 7.5f, 0.0f)   // mesh group center in the XY plane
let  orbit_radius = 20.0f
let  orbit_height = 10.0f
let  orbit_speed  = 0.48f                      // radians per second

// === game time ===
// The game owns its subtree and attaches it to the engine root; the editor does the same
// with its own clock. Nothing is registered for us, and GameStart may run again on Play.
struct GameClock : ClockTag {}
struct GameWorldClock : ClockTag {}
struct GameUIClock : ClockTag {}

var game_clock       = GameClock()
var game_world_clock = GameWorldClock()
var game_ui_clock    = GameUIClock()

// Gameplay runs only after `OnLoaded` applied the startup scene. This is game state,
// not loader state: the loader task owns the request and disappears when it is done.
var private g_scene_ready = false

def RegisterGameTime() {
    TimeAttach(game_clock)
    TimeRegister(game_world_clock, game_clock)
    TimeRegister(game_ui_clock, game_clock)
}

//! Game state that must survive an editor hot reload. The editor saves it through
//! Editor HotReload; the standalone game never calls these.
struct public GameReloadState {
    scene_ready : bool
    orbit_angle : float
}

def public CaptureReloadState() : GameReloadState {
    return GameReloadState(scene_ready = g_scene_ready, orbit_angle = orbit_angle)
}

def public ApplyReloadState(var state : GameReloadState) {
    g_scene_ready = state.scene_ready
    orbit_angle = state.orbit_angle
    // A resumed Play needs its clocks back: the new context starts with an empty time tree.
    if (g_scene_ready) {
        RegisterGameTime()
    }
}

// Explicit entry point, shared by the standalone game and editor Play. The request says
// what this game needs at startup; the loader task reads and compiles it, then calls
// `OnLoaded` synchronously in the cache.
[export]
def GameStart(){
    print("Game Start\n")
    RegisterGameTime()
    Physics::Reset()
    Physics::Attach(game_world_clock)
    PhysicsDemo::Reset()
    g_scene_ready = false

    var request <- LoaderRequest()
    request.AddBundle(STARTUP_BUNDLE)
    request.AddScene(STARTUP_SCENE)
    StartTask(Loader::Load(request, @@OnLoaded))
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
    if (!g_scene_ready) {
        return
    }
    TimeSetPaused(game_world_clock, GameUi::IsPaused())
    UpdateGameplay()
}

// The loader callback: everything in the request is in `Content`/`Prefab` caches.
// Applying the scene is a separate synchronous step, exactly like spawning a prefab.
def private OnLoaded() {
    if (!Scene::Load(STARTUP_SCENE)) {
        print("[GAME ERROR] startup scene '{STARTUP_SCENE}' was not preloaded\n")
        return
    }
    g_scene_ready = true
    PhysicsDemo::Start()

    let spawned = Prefab::Spawn(GAMEPLAY_PREFAB, float3(10.0f, 0.0f, 0.0f))
    if (spawned == INVALID_ENTITY_ID) {
        print("[GAME ERROR] failed to spawn prefab '{GAMEPLAY_PREFAB}'\n")
    }
}

def private UpdateGameplay() {
    // Everything below integrates time, so it runs on the world clock: a paused game
    // freezes it, a slowdown scales it, and the caller never passes dt in.
    let dt = TimeDelta(game_world_clock)

    // ================== CAMERA ORBIT ==================
    orbit_angle += orbit_speed * dt

    // camera position: orbit in the XY plane, lift along Z
    let eye = float3(
        orbit_center.x + orbit_radius * cos(orbit_angle),
        orbit_center.y + orbit_radius * sin(orbit_angle),
        orbit_center.z + orbit_height
    )

    // target camera basis
    let f = normalize(orbit_center - eye)
    let r = normalize(cross(f, world_up))   // horizontal right
    let u = cross(r, f)                     // corrected up

    // quaternion: align forward first, then roll around f
    // to line up up
    let q1 = quat_from_unit_arc(front_raw, f)
    let up_after = quat_mul_vec(q1, up_raw)

    let cos_roll = dot(up_after, u)
    let sin_roll = dot(cross(up_after, u), f)
    let roll = atan2(sin_roll, cos_roll)

    let q2 = quat_from_unit_vec_ang(f, roll)
    let q  = quat_mul(q2, q1)

    // The editor camera is editor state: in Play it sits next to the game camera and
    // must not be moved by gameplay.
    query() $[REQUIRE_NOT(editorCameraTag)] (var transform : Transform, let camera : Camera) {
        transform.position = eye
        transform.rotation = q
    }

    // ================== player input ==================
    query()$(var transform : Transform, var inputState : float3){
        inputState = float3(0, 0, 0)
        if (is_down(Key.I)) { inputState.y =  1.0f }
        if (is_down(Key.K)) { inputState.y = -1.0f }
        if (is_down(Key.J)) { inputState.x = -1.0f }
        if (is_down(Key.L)) { inputState.x =  1.0f }
    }

    query()$(var transform : Transform, inputState : float3, speed : float){
        transform.position += inputState * 4.8f * speed * dt;
    }
}
