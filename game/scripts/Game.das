options gen2
options relaxed_assign = false
options rtti = true

module Game shared public

require daslib/decs
require daslib/decs_boost
require tryengine/Input
require tryengine/Profiling
require tryengine/Time
require tryengine/Types
require trygame/GameFlow
require trygame/GameScreens
require trygame/GameTime public
require trygame/GameUi
require math

// === engine convention ===
let front_raw = float3(0.0f, 1.0f, 0.0f)   // local Y (forward)
let up_raw    = float3(0.0f, 0.0f, 1.0f)   // local Z (up)
let world_up  = float3(0.0f, 0.0f, 1.0f)

// === orbit parameters ===
var  orbit_angle  = 0.0f
let  orbit_center = float3(0.0f, 7.5f, 0.0f)   // mesh group center in the XY plane
let  orbit_radius = 20.0f
let  orbit_height = 10.0f
let  orbit_speed  = 0.48f                      // radians per second

//! Game state that must survive an editor hot reload. The editor saves it through
//! Editor HotReload; the standalone game never calls these.
struct public GameReloadState {
    scene_ready : bool
    orbit_angle : float
}

def public CaptureReloadState() : GameReloadState {
    return GameReloadState(scene_ready = GameFlow::IsReady(), orbit_angle = orbit_angle)
}

def public ApplyReloadState(var state : GameReloadState) {
    orbit_angle = state.orbit_angle
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
