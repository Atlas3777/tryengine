options gen2
options relaxed_assign = false
options rtti = true

module Game shared public

require daslib/decs
require daslib/decs_boost
require tryengine/Types
require tryengine/Input
require tryengine/Engine
require tryengine/Loader
require tryengine/Prefab
require tryengine/Profiling
require tryengine/Scene
require tryengine/Time
require tryengine/TransformSystem
require math

// === engine convention ===
let front_raw = float3(0.0f, 1.0f, 0.0f)   // local Y (forward)
let up_raw    = float3(0.0f, 0.0f, 1.0f)   // local Z (up)
let world_up  = float3(0.0f, 0.0f, 1.0f)

// === the explicit startup schema ===
// A new project gets this file. Change the paths here, not in C++: the engine does not
// load a scene on startup, does not preload game content, and does not pick what the
// player sees while loading. This block is the whole startup policy.
//
// For a standalone build, select both .scene files in BuildSettings: only selected
// scenes get their own pak. Prefabs, bundles and data files are in the common pak.
let STARTUP_SCENE = "content/test.scene"
let LOADING_SCENE = "content/loading.scene"
let STARTUP_BUNDLE = "data/bundles/startup.bundle"
let LOADING_VISUAL_PREFAB = "prefabl.prefab"

// Purely a feel knob. The loader reports readiness as soon as the assets are there;
// this holds the loading screen for at least this long. Set it to 0.0f to enter as fast
// as possible, or make it large for a fake, atmospheric screen.
let MIN_LOADING_SECONDS = 1.5f

// false: enter the startup scene as soon as it is ready.
// true:  hold the ready loading screen until Space; the same hook is where a "press any
//        key" prompt, a network handshake or a title screen would live.
let WAIT_FOR_SPACE = true

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

var private g_loading_visual : EntityId = INVALID_ENTITY_ID
var private g_loading_visual_spawned : bool = false

def RegisterGameTime() {
    TimeAttach(game_clock)
    TimeRegister(game_world_clock, game_clock)
    TimeRegister(game_ui_clock, game_clock)
}

[export]
def TestTestTest(){
    print("Test build works\n")
}

// Explicit entry point, shared by the standalone game and editor Play. The engine calls
// this before the first frame; it must not wait or read disk synchronously. Everything
// slow happens in Loader::Update, driven by GameUpdate.
[export]
def GameStart(){
    print("Game Start\n")
    RegisterGameTime()

    var request <- LoaderRequest()
    request.SetLoadingScene(LOADING_SCENE)
    request.SetBlocking(true)
    request.AddBundle(STARTUP_BUNDLE)
    request.AddScene(STARTUP_SCENE)

    if (!Loader::Invoke(request)) {
        print("[GAME ERROR] failed to start the loader\n")
    }
}

[export, profile]
def GameUpdate() {
    if (Loader::IsActive()) {
        Loader::Update()
        UpdateLoadingVisuals()

        if (Loader::IsFailed()) {
            if (Loader::IsBlocking()) {
                return
            }
        } elif (Loader::IsReady()) {
            // Step 3: the game decides when to enter the target scene. The callback is
            // the scene-specific hook; it runs after the new DECS world exists.
            if (TimeTotal(game_clock) >= double(MIN_LOADING_SECONDS)
                && (!WAIT_FOR_SPACE || pressed(Key.Space))) {
                if (!Scene::Load(STARTUP_SCENE, @@StartGameScene)) {
                    Loader::Fail("startup scene '{STARTUP_SCENE}' failed to load")
                } else {
                    Loader::Finish()
                }
                return
            }
        }

        if (Loader::IsBlocking()) {
            return
        }
    }

    UpdateGameplay()
}

// The loading scene is ordinary game content: a camera and whatever the game wants to
// show. The visual prefab is cached by the prefab phase of the request; spawn it once
// assets are being tracked, then animate it with the real progress value.
def private UpdateLoadingVisuals() {
    let phase = Loader::Phase()
    if (!g_loading_visual_spawned && (phase == LoaderPhase.Assets || phase == LoaderPhase.Ready)) {
        g_loading_visual = Prefab::Spawn(LOADING_VISUAL_PREFAB, float3(0.0f, 0.0f, 0.0f))
        g_loading_visual_spawned = true
    }

    let t = float(TimeTotal(game_clock))
    let angle = t * 0.6f

    query() $[REQUIRE_NOT(editorCameraTag)] (var transform : Transform, let camera : Camera) {
        transform.position = float3(cos(angle) * 10.0f, sin(angle) * 10.0f, 0.0f)
        transform.rotation = quat_from_unit_vec_ang(float3(0.0f, 0.0f, 1.0f), angle + 1.5707963f)
    }

    query() $(var transform : Transform; let meshGuid : uint64) {
        transform.rotation = quat_from_unit_vec_ang(float3(0.0f, 0.0f, 1.0f), t * 1.5f)
        let pulse = 0.9f + 0.2f * Loader::Progress()
        transform.scale = float3(pulse, pulse, pulse)
    }
}

// The game has no editor camera. A scene may ship its own camera, but a scene without
// one (or editor Play, which runs this same entry point) still needs an active camera
// for Render.das.
def private EnsureGameCamera() {
    var has_active = false
    query() $(let cam : Camera; let active_camera : bool) {
        has_active = true
    }
    if (has_active) {
        return
    }

    create_entity() @(eid, cmp){
        cmp.tag := "GameCamera"
        apply_decs_template(cmp, Transform(
            position = float3(0.0f, -20.0f, 10.0f),
            rotation = float4(0f, 0f, 0f, 1f),
            scale = float3(1.0f, 1.0f, 1.0f)
        ))
        apply_decs_template(cmp, Camera(
            fov = 35.0f,
            near_plane = 0.01f,
            far_plane = 500.0f
        ))
        cmp.active_camera := true
        apply_decs_template(cmp, Relationship())
    }
    commit()
}

// Scene-specific setup. `Scene::Load(path, @@StartGameScene)` calls this after the DECS
// world of the target scene exists, so this is the only place that decides what the
// startup scene spawns and which camera it uses.
def private StartGameScene() {
    g_loading_visual = INVALID_ENTITY_ID
    g_loading_visual_spawned = false
    EnsureGameCamera()
    SpawnGameplayPrefabs()
    UpdateWorldTransforms()
}

def private SpawnGameplayPrefabs() {
    let spawned = Prefab::Spawn(LOADING_VISUAL_PREFAB, float3(10.0f, 0.0f, 0.0f))
    if (spawned == INVALID_ENTITY_ID) {
        print("[GAME ERROR] failed to spawn prefab '{LOADING_VISUAL_PREFAB}'\n")
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

    query()$(var t : Transform, var cam : Camera, let active_camera : bool) {
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
