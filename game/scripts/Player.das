options gen2
options relaxed_assign = false

module Player shared public

//! First-person player: WASD + Space + mouse look over a virtual character.
//!
//! Movement is a composition of physics components: `CapsuleCollider` gives the shape,
//! `CharacterController` makes it a Jolt character, and `PlayerInput` turns that character into
//! a player driven by the keyboard and the mouse. A capsule without `PlayerInput` stays an NPC
//! character; a capsule without `CharacterController` stays a rigid body.
//!
//! The system drives the first player entity and the first non-editor camera. The entity
//! Transform is the capsule center (CharacterSync writes it); the camera sits `eye_height`
//! above the character's feet.

require math
require daslib/decs
require daslib/decs_boost
require tryengine/ConfigFormatBoost
require tryengine/EcsMacro
require tryengine/Input
require live/live_vars
require live_host
require tryengine/Physics
require tryengine/Serializable
require tryengine/TransformSystem
require tryengine/Types
require tryengine/Ui

//! Player controls over a CharacterController. Needs a CapsuleCollider and a
//! CharacterController on the same entity; add it to an entity to make that entity playable.
[DecsSerializable]
struct PlayerInput {
    @min = 0.0
    move_speed : float = 6.0f
    //! Mouse degrees per pixel.
    @min = 0.0
    look_sensitivity : float = 0.15f
    //! Camera height above the character's feet, metres.
    @min = 0.0
    eye_height : float = 1.6f
}

%ecs~("playerInput", type<PlayerInput>) %%

let private WORLD_UP = float3(0.0f, 0.0f, 1.0f)
let private FORWARD = float3(0.0f, 1.0f, 0.0f)
let private RIGHT = float3(1.0f, 0.0f, 0.0f)
let private MAX_PITCH = radians(89.0f)

//! Look state is `@live`: the mouse is still captured by the window across a hot reload, and
//! restoring the angles instead of taking the yaw from the entity again keeps the camera
//! from snapping. The character handle survives too, because the Jolt character is C++ state.
var private @live g_look_handle : uint64 = 0ul
var private @live g_yaw : float
var private @live g_pitch : float
var private @live g_mouse_captured : bool

//! Releases the mouse. Menus, pause and Play stop call it; the game decides the policy, the
//! platform only hides the cursor. Unconditional: it also repairs a state left over from a
//! script hot reload, where the globals are new but the window is still captured.
def public ReleaseMouse() {
    g_mouse_captured = false
    SetMouseCaptured(false)
}

//! Drops the accumulated look angles and releases the mouse. Call it when a fresh scene or Play
//! session starts; the next Update takes the yaw from the player entity's rotation again.
def public Reset() {
    g_look_handle = 0ul
    g_yaw = 0.0f
    g_pitch = 0.0f
    ReleaseMouse()
}

//! Yaw of a Z-up quaternion, ignoring any pitch and roll.
def private YawFromRotation(q : float4) : float {
    return atan2(2.0f * (q.w * q.z + q.x * q.y), 1.0f - 2.0f * (q.y * q.y + q.z * q.z))
}

//! Reads the controls and drives the first player character and the game camera. Call it from
//! GameUpdate: the movement is an intent for the next fixed physics step, and the camera is
//! placed on the character pose that the last step produced.
def public Update() {
    var handle : uint64 = 0ul
    var player_eid = INVALID_ENTITY_ID
    var input = PlayerInput()
    var player_rotation = float4(0.0f, 0.0f, 0.0f, 1.0f)

    query() $(eid : EntityId; transform : Transform; playerInput : PlayerInput;
              characterController : CharacterController; characterBody : CharacterBody) {
        // The first live player wins; a second PlayerInput entity is not driven.
        if (handle == 0ul && characterBody.handle != 0ul && Physics::IsCharacterAlive(characterBody.handle)) {
            handle = characterBody.handle
            player_eid = eid
            input = playerInput
            player_rotation = transform.rotation
        }
    }

    if (handle == 0ul || player_eid == INVALID_ENTITY_ID || !Physics::IsCharacterAlive(handle)) {
        return
    }

    // Gameplay owns the mouse: the cursor is hidden and look deltas keep coming from relative
    // motion. GameUpdate releases it on pause and on any screen above the HUD. SDL ignores a
    // request for the mode the window is already in, so calling it every frame is cheap.
    g_mouse_captured = SetMouseCaptured(true)

    if (handle != g_look_handle) {
        g_look_handle = handle
        g_yaw = YawFromRotation(player_rotation)
        g_pitch = 0.0f
    }

    // Captured gameplay owns the pointer. Without capture (editor focus is elsewhere, or the
    // window has no keyboard focus) fall back to the viewport/UI gating: the editor panels and
    // the game UI must not spin the camera.
    if (g_mouse_captured || (Ui::ViewportHovered() && !Ui::ConsumesMouse())) {
        let md = mouse_delta()
        g_yaw -= radians(md.x * input.look_sensitivity)
        g_pitch = clamp(g_pitch - radians(md.y * input.look_sensitivity), -MAX_PITCH, MAX_PITCH)
    }

    let q_yaw = quat_from_unit_vec_ang(WORLD_UP, g_yaw)
    let q_pitch = quat_from_unit_vec_ang(RIGHT, g_pitch)
    let view = normalize(quat_mul(q_yaw, q_pitch))

    let forward = quat_mul_vec(q_yaw, FORWARD)
    let right = quat_mul_vec(q_yaw, RIGHT)
    var wish = float3(0.0f, 0.0f, 0.0f)
    if (is_down(Key.W)) { wish += forward }
    if (is_down(Key.S)) { wish -= forward }
    if (is_down(Key.D)) { wish += right }
    if (is_down(Key.A)) { wish -= right }
    if (length(wish) > 0.0f) {
        wish = normalize(wish)
    }

    let jump = pressed(Key.Space) && Physics::IsCharacterGrounded(handle)
    Physics::SetCharacterInput(handle, wish * input.move_speed, jump)
    // The capsule faces where the camera looks. The entity rotation is written in the same
    // frame, so the physics-side pose push does not mistake the turn for an Inspector edit.
    Physics::SetCharacterRotation(handle, q_yaw)
    if (decs::is_alive(player_eid)) {
        query(player_eid) $(var transform : Transform) {
            transform.rotation = q_yaw
        }
    }

    let feet = Physics::GetCharacterPosition(handle)
    let eye = feet + float3(0.0f, 0.0f, input.eye_height)

    // The argument name is the component name: `transform`, not a local alias. `camera` keeps
    // the query on entities that actually have a Camera template.
    query() $[REQUIRE_NOT(editorCameraTag)] (var transform : Transform; camera : Camera) {
        transform.position = eye
        transform.rotation = view
    }
}

//! Creates a player entity at `feet` from the same components the Inspector would add:
//! capsule collider, character controller and player input. Use it from a scene loader when the
//! player is not authored in the scene.
def public Spawn(feet : float3) : EntityId {
    let radius = 0.35f
    let half_height = 0.55f
    let center = feet + float3(0.0f, 0.0f, radius + half_height)
    let eid = create_entity() @(e, cmp) {
        ApplyEntityTransform(cmp, center, float4(0.0f, 0.0f, 0.0f, 1.0f), float3(1.0f, 1.0f, 1.0f))
        cmp |> set("capsuleCollider", CapsuleCollider(radius = radius, half_height = half_height))
        cmp |> set("characterController", CharacterController())
        cmp |> set("playerInput", PlayerInput())
        cmp |> set(RELATIONSHIP_FIELD, Relationship())
        cmp.tag := "Player"
    }
    commit()
    return eid
}
