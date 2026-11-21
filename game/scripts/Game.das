options gen2
options relaxed_assign = false
options rtti = true

module Game shared public

require daslib/decs
require daslib/decs_boost
require tryengine/Types
require tryengine/Input
require tryengine/Prefab
require tryengine/Profiling
require tryengine/Time
require tryengine/TransformSystem
require trygame/Boot
require math

// === engine convention ===
let front_raw = float3(0.0f, 1.0f, 0.0f)   // local Y (forward)
let up_raw    = float3(0.0f, 0.0f, 1.0f)   // local Z (up)
let world_up  = float3(0.0f, 0.0f, 1.0f)

// === gameplay content ===
// The startup policy (loading scene, startup scene, bundle, hold time) lives in
// `trygame/Boot.das`; this file is gameplay only. The prefab is preloaded by the
// startup bundle before the startup scene is applied.
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

def RegisterGameTime() {
    TimeAttach(game_clock)
    TimeRegister(game_world_clock, game_clock)
    TimeRegister(game_ui_clock, game_clock)
}

// Explicit entry point, shared by the standalone game and editor Play. GameStart only
// wires the game clocks and hands the slow startup flow to `Boot`; a boot session is
// stopped by the editor, not by the game.
[export]
def GameStart(){
    print("Game Start\n")
    RegisterGameTime()
    Boot::Start(@@OnGameSceneLoaded)
}

[export, profile]
def GameUpdate() {
    if (!Boot::Update(TimeTotal(game_clock))) {
        return
    }
    UpdateGameplay()
}

// Scene-specific setup: `Scene::Load` calls this after the startup scene's DECS world
// exists. The startup scene owns its camera; this hook only spawns gameplay objects.
def private OnGameSceneLoaded() {
    let spawned = Prefab::Spawn(GAMEPLAY_PREFAB, float3(10.0f, 0.0f, 0.0f))
    if (spawned == INVALID_ENTITY_ID) {
        print("[GAME ERROR] failed to spawn prefab '{GAMEPLAY_PREFAB}'\n")
    }
    UpdateWorldTransforms()
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

    query()$(var t : Transform, let camera : Camera) {
        t.position = eye
        t.rotation = q
    }

    // ================== player input ==================
    query()$(var t : Transform, var inputState : float3){
        inputState = float3(0, 0, 0)
        if (is_down(Key.I)) { inputState.y =  1.0f }
        if (is_down(Key.K)) { inputState.y = -1.0f }
        if (is_down(Key.J)) { inputState.x = -1.0f }
        if (is_down(Key.L)) { inputState.x =  1.0f }
    }

    query()$(var t : Transform, inputState : float3, speed : float){
        t.position += inputState * 4.8f * speed * dt;
    }
}
