options gen2
options relaxed_assign = false

module Physics shared public

//! Physics facade over the native tryPhysics module.
//!
//! Jolt owns the simulation in C++; this module owns only the script-side shape of the
//! API and the bridge to the ECS heap: a body remembers the entity it belongs to, and
//! `PhysicsSync` copies the resulting pose back into `Transform` before the world
//! transform pass runs. A virtual character follows the same bridge through
//! `CharacterSync`: a CapsuleCollider + CharacterController pair makes one, and the runtime
//! CharacterBody component carries its handle.

require math
require daslib/decs
require daslib/decs_boost
require tryengine/ConfigFormatBoost
require tryengine/EcsMacro
require live/live_vars
require live_host
require tryengine/Profiling
require tryengine/Serializable
require tryengine/Time
require tryengine/TransformSystem
require tryengine/Types
require tryPhysics

enum PhysicsShape {
    Box,
    Sphere,
    Capsule,
    Cylinder,
    Plane
}

enum PhysicsMotion {
    Static,
    Kinematic,
    Dynamic
}

//! One collider description. The C++ default matches this one field for field; changing
//! a field here without changing PhysicsWorld::BodyDesc only changes the script default.
struct PhysicsBodyDesc {
    shape : PhysicsShape = PhysicsShape.Box
    motion : PhysicsMotion = PhysicsMotion.Dynamic
    position : float3 = float3(0.0f, 0.0f, 0.0f)
    rotation : float4 = float4(0.0f, 0.0f, 0.0f, 1.0f)
    //! Half size for Box; half size of the debug grid for Plane.
    half_extents : float3 = float3(0.5f, 0.5f, 0.5f)
    radius : float = 0.5f
    //! Half of the cylindrical part for Capsule; half height for Cylinder.
    half_height : float = 0.5f
    mass : float = 1.0f
    friction : float = 0.5f
    restitution : float = 0.0f
    linear_velocity : float3 = float3(0.0f, 0.0f, 0.0f)
    angular_velocity : float3 = float3(0.0f, 0.0f, 0.0f)
    entity : EntityId = INVALID_ENTITY_ID
    //! Sensor: reports overlaps but never pushes anything. The body is a trigger volume.
    sensor : bool = false
}

//! Virtual character: a capsule moved by collision queries, not a rigid body. `position` is
//! the point under the capsule (the feet), not its center, so a character stands on a floor
//! without a manual offset. The controller owns vertical motion: SetCharacterInput takes
//! horizontal velocity only, while gravity, jumping and step/slope handling are applied by
//! Physics::Update on the fixed physics step.
struct CharacterDesc {
    position : float3 = float3(0.0f, 0.0f, 0.0f)
    rotation : float4 = float4(0.0f, 0.0f, 0.0f, 1.0f)
    //! Capsule radius.
    radius : float = 0.3f
    //! Half of the cylindrical part, without the two caps.
    half_height : float = 0.6f
    //! Character mass: with it the character presses on the body it stands on.
    mass : float = 70.0f
    //! Maximum force (N) with which the character pushes dynamic bodies while walking.
    max_strength : float = 100.0f
    //! Steepest slope the character still walks on, in degrees.
    max_slope_angle : float = 45.0f
    //! Jump speed along Z, m/s.
    jump_speed : float = 5.0f
    //! Optional ECS owner: its Transform follows the capsule center. Must be a root: a
    //! child's local Transform is not a world pose, so a parented character is not synced.
    //! One character per entity; creating another for the same entity drops the previous.
    entity : EntityId = INVALID_ENTITY_ID
}

//! Begin-contact event. `body_a` and `body_b` are stable body handles; `entity_a` and
//! `entity_b` map them back to the ECS owners (INVALID_ENTITY_ID for a body created
//! without an entity).
struct PhysicsContact {
    body_a   : uint64
    body_b   : uint64
    point    : float3
    normal   : float3
    depth    : float
    entity_a : EntityId = INVALID_ENTITY_ID
    entity_b : EntityId = INVALID_ENTITY_ID
}

// --- ECS components ---------------------------------------------------------------
//
// Colliders are one component per primitive, so the Inspector shows exactly the fields
// that shape has. An entity with a collider but without RigidBody is a static part of
// the level; RigidBody upgrades it to kinematic or dynamic. Physics owns one body per
// root entity: the component sync creates it on the first Update after the components
// appear and recreates it when an authored field changes.

[DecsSerializable]
struct RigidBody {
    //! PhysicsMotion as an int: 0 static, 1 kinematic, 2 dynamic.
    @min = 0
    @max = 2
    motion : int = int(PhysicsMotion.Dynamic)
    @min = 0.001
    mass : float = 1.0f
    @min = 0.0
    friction : float = 0.5f
    @min = 0.0
    @max = 1.0
    restitution : float = 0.0f
    //! Trigger volume: bodies overlap it without being pushed, and Physics::Contacts
    //! reports the pairs. A character is not a body, so it does not produce these events.
    sensor : bool = false
}

[DecsSerializable]
struct BoxCollider {
    //! Half size on each axis: the full box is twice this.
    half_extents : float3 = float3(0.5f, 0.5f, 0.5f)
}

[DecsSerializable]
struct SphereCollider {
    @min = 0.001
    radius : float = 0.5f
}

[DecsSerializable]
struct CapsuleCollider {
    @min = 0.001
    radius : float = 0.5f
    //! Half of the cylindrical part, without the two caps.
    @min = 0.001
    half_height : float = 0.5f
}

[DecsSerializable]
struct CylinderCollider {
    @min = 0.001
    radius : float = 0.5f
    @min = 0.001
    half_height : float = 0.5f
}

[DecsSerializable]
struct PlaneCollider {
    //! Half size of the debug grid; the Jolt plane itself is infinite.
    half_extents : float3 = float3(10.0f, 10.0f, 0.0f)
}

//! Runtime bridge to a Jolt body. Declared `transient = true, hidden = true` below: it is
//! not saved, not listed in Add Component, and never authored. `signature` is the shape and
//! material the body was created from; a mismatch means the body is recreated.
struct PhysicsBody {
    handle    : uint64 = 0ul
    signature : uint64 = 0ul
    managed   : bool = false
}

//! Turns a CapsuleCollider into a virtual Jolt character instead of a rigid body. The capsule
//! supplies the shape and size, this component the controller behavior: gravity, jumping,
//! steps, slopes and pushing dynamic bodies. A RigidBody on the same entity is ignored while
//! CharacterController is present; a character is not a body and does not collide with other
//! characters.
[DecsSerializable]
struct CharacterController {
    @min = 0.0
    mass : float = 70.0f
    //! Maximum force (N) with which the character pushes dynamic bodies while walking.
    @min = 0.0
    max_strength : float = 100.0f
    //! Steepest slope the character still walks on, in degrees.
    @min = 0.0
    @max = 89.0
    max_slope_angle : float = 45.0f
    //! Jump speed along Z, m/s.
    @min = 0.0
    jump_speed : float = 5.0f
}

//! Runtime bridge to a virtual character. Hidden and transient like PhysicsBody: the component
//! sync creates it from CharacterController + CapsuleCollider. A signature mismatch means the
//! character is recreated.
struct CharacterBody {
    handle    : uint64 = 0ul
    signature : uint64 = 0ul
}

//! Applies a continuous force and torque to the dynamic body of the entity every frame.
//! Meaningful only with a collider and a dynamic RigidBody: a static or kinematic body ignores
//! it, and a character entity has no body to push. The force is frame-rate independent, so
//! leaving the component on accelerates the body until it is removed or set to zero.
[DecsSerializable]
struct ConstantForce {
    //! Newtons.
    force : float3 = float3(0.0f, 0.0f, 0.0f)
    //! Newton-metres.
    torque : float3 = float3(0.0f, 0.0f, 0.0f)
}

//! The physics component set. Colliders, RigidBody, CharacterController and ConstantForce are
//! one-value structs, so they are [DecsSerializable] and written as one ConfigFormat object;
//! PhysicsBody and CharacterBody are runtime bridges and are transient.
%ecs~("rigidBody", type<RigidBody>) %%
%ecs~("boxCollider", type<BoxCollider>) %%
%ecs~("sphereCollider", type<SphereCollider>) %%
%ecs~("capsuleCollider", type<CapsuleCollider>) %%
%ecs~("cylinderCollider", type<CylinderCollider>) %%
%ecs~("planeCollider", type<PlaneCollider>) %%
%ecs~("physicsBody", type<PhysicsBody>, hidden = true, transient = true) %%
%ecs~("characterController", type<CharacterController>) %%
%ecs~("characterBody", type<CharacterBody>, hidden = true, transient = true) %%
%ecs~("constantForce", type<ConstantForce>) %%

//! Physics gets its own clock under the game clock: pausing the game freezes the
//! simulation, and a slowed game clock slows the physics without touching the engine.
struct PhysicsClock : ClockTag {}

//! The clock and the attachment flag are `@live`: the Jolt world itself lives in C++ and
//! survives a hot reload, and a live session must not stop stepping just because the script
//! generation that attached it is gone.
var private @live g_physics_clock = PhysicsClock()
var private @live g_physics_attached : bool

let private SIGNATURE_OFFSET : uint64 = 14695981039346656037ul
let private SIGNATURE_PRIME : uint64 = 1099511628211ul

//! Attaches physics to a game-owned clock. Idempotent: GameStart may run again on Play.
def Attach(var parent : auto) {
    if (parent.slot_ == -1) {
        return
    }
    if (g_physics_clock.slot_ == -1) {
        TimeRegister(g_physics_clock, parent)
    }
    g_physics_attached = g_physics_clock.slot_ != -1
}

//! Drops every body and character and stops stepping until Attach is called again. Physics
//! belongs to the scene, so a new scene or a new Play session calls this before creating its
//! own; the clock registration survives. The runtime components are dropped too, so the next
//! Update rebuilds both from the authored components.
def Reset() {
    PhysicsClear()
    query() $(eid : EntityId; physicsBody : PhysicsBody) {
        RemovePhysicsBodyComponent(eid)
    }
    query() $(eid : EntityId; characterBody : CharacterBody) {
        RemoveCharacterBodyComponent(eid)
    }
    commit()
    g_physics_attached = false
}

//! One engine tick: reconcile ECS components into bodies and characters, push entity poses
//! into static/kinematic bodies and characters, advance Jolt by the game clock delta, then
//! copy the resulting poses back into Transform. EngineUpdate calls this before
//! UpdateWorldTransforms, so the world pass sees this frame's physics pose.
[profile]
def Update() {
    if (!g_physics_attached) {
        return
    }
    ReconcileComponents()
    ReconcileCharacters()
    // Poses are pushed even while the game is paused: the Inspector can then move a static
    // collider or a character and the debug view follows without stepping the simulation.
    PhysicsPushPoses()
    CharacterPushPoses()
    if (!TimeIsAdvancing(g_physics_clock)) {
        return
    }
    ApplyConstantForces()
    PhysicsStep(TimeDelta(g_physics_clock))
    PhysicsSync()
    CharacterSync()
}

def private FloatBits(value : float) : uint64 {
    return uint64(unsafe(reinterpret<uint>(value)))
}

def private MixSignature(hash : uint64; value : uint64) : uint64 {
    return (hash ^ value) * SIGNATURE_PRIME
}

//! EntityId as one opaque 64-bit key: the low half is the index, the high half is the
//! generation. LLVM JIT cannot lower reinterpret<EntityId, uint64>, so both directions are
//! numeric; the C++ side only compares keys and never decodes them.
def private EntityRaw(eid : EntityId) : uint64 {
    return uint64(eid.id) | (uint64(uint(eid.generation)) << 32)
}

def private EntityFromRaw(raw : uint64) : EntityId {
    return EntityId(id = uint(raw & 0xFFFFFFFFul), generation = int(uint(raw >> 32)))
}

def private MotionFromInt(value : int) : PhysicsMotion {
    if (value == int(PhysicsMotion.Static)) {
        return PhysicsMotion.Static
    }
    if (value == int(PhysicsMotion.Kinematic)) {
        return PhysicsMotion.Kinematic
    }
    return PhysicsMotion.Dynamic
}

//! Everything that forces a new Jolt body: shape, its size, motion and material. The
//! pose is not part of it, so moving a static collider does not recreate the body.
def private BodySignature(desc : PhysicsBodyDesc) : uint64 {
    var hash = SIGNATURE_OFFSET
    hash = MixSignature(hash, uint64(int(desc.shape)))
    hash = MixSignature(hash, uint64(int(desc.motion)))
    hash = MixSignature(hash, FloatBits(desc.half_extents.x))
    hash = MixSignature(hash, FloatBits(desc.half_extents.y))
    hash = MixSignature(hash, FloatBits(desc.half_extents.z))
    hash = MixSignature(hash, FloatBits(desc.radius))
    hash = MixSignature(hash, FloatBits(desc.half_height))
    hash = MixSignature(hash, FloatBits(desc.mass))
    hash = MixSignature(hash, FloatBits(desc.friction))
    hash = MixSignature(hash, FloatBits(desc.restitution))
    hash = MixSignature(hash, desc.sensor ? 1ul : 0ul)
    return hash
}

//! Everything that forces a new character: the capsule size and the controller settings. The
//! pose is not part of it, so moving a character entity does not recreate the character.
def private CharacterSignature(desc : CharacterDesc) : uint64 {
    var hash = SIGNATURE_OFFSET
    hash = MixSignature(hash, FloatBits(desc.radius))
    hash = MixSignature(hash, FloatBits(desc.half_height))
    hash = MixSignature(hash, FloatBits(desc.mass))
    hash = MixSignature(hash, FloatBits(desc.max_strength))
    hash = MixSignature(hash, FloatBits(desc.max_slope_angle))
    hash = MixSignature(hash, FloatBits(desc.jump_speed))
    return hash
}

def private MakeBodyDesc(transform : Transform; settings : RigidBody; shape : PhysicsShape;
                         half_extents : float3; radius : float; half_height : float) : PhysicsBodyDesc {
    var desc = PhysicsBodyDesc()
    desc.shape = shape
    desc.motion = MotionFromInt(settings.motion)
    desc.position = transform.position
    desc.rotation = transform.rotation
    desc.half_extents = half_extents
    desc.radius = radius
    desc.half_height = half_height
    desc.mass = settings.mass
    desc.friction = settings.friction
    desc.restitution = settings.restitution
    desc.sensor = settings.sensor
    return <- desc
}

def private SetPhysicsBodyComponent(eid : EntityId; value : PhysicsBody) {
    update_entity(eid) @(eid, cmp) {
        cmp |> set("physicsBody", value)
    }
}

def private RemovePhysicsBodyComponent(eid : EntityId) {
    update_entity(eid) @(eid, cmp) {
        decs::remove(cmp, "physicsBody")
    }
}

def private SetCharacterBodyComponent(eid : EntityId; value : CharacterBody) {
    update_entity(eid) @(eid, cmp) {
        cmp |> set("characterBody", value)
    }
}

def private RemoveCharacterBodyComponent(eid : EntityId) {
    update_entity(eid) @(eid, cmp) {
        decs::remove(cmp, "characterBody")
    }
}

//! Builds the character description from the authored components. The entity Transform is the
//! capsule center, like a body's; CharacterDesc::position is the feet.
def private MakeCharacterDesc(transform : Transform; settings : CharacterController;
                              capsule : CapsuleCollider) : CharacterDesc {
    var desc = CharacterDesc()
    desc.position = transform.position - float3(0.0f, 0.0f, capsule.radius + capsule.half_height)
    desc.rotation = transform.rotation
    desc.radius = capsule.radius
    desc.half_height = capsule.half_height
    desc.mass = settings.mass
    desc.max_strength = settings.max_strength
    desc.max_slope_angle = settings.max_slope_angle
    desc.jump_speed = settings.jump_speed
    return <- desc
}

//! Brings one character entity in line with its components. Called from inside a query, so
//! component changes are only queued; ReconcileCharacters commits after all queries.
def private EnsureCharacterForCollider(eid : EntityId; desc : CharacterDesc; parented : bool) : bool {
    let signature = CharacterSignature(desc)
    let current = get_component(eid, "characterBody", CharacterBody())

    // A child's local Transform is not a world pose, so components do not own a character there.
    if (parented) {
        if (current.handle != 0ul) {
            PhysicsDestroyCharacter(current.handle)
            RemoveCharacterBodyComponent(eid)
            return true
        }
        return false
    }

    if (current.handle != 0ul && current.signature == signature && PhysicsIsCharacterAlive(current.handle)) {
        return false
    }

    if (current.handle != 0ul) {
        PhysicsDestroyCharacter(current.handle)
    }

    var owned = desc
    owned.entity = eid
    let handle = CreateCharacter(owned)
    if (handle == 0ul) {
        if (current.handle != 0ul) {
            RemoveCharacterBodyComponent(eid)
            return true
        }
        return false
    }
    SetCharacterBodyComponent(eid, CharacterBody(handle = handle, signature = signature))
    return true
}

//! A child's local Transform is not a world pose; an orphan whose parent was deleted is a
//! root, the same rule TransformSystem follows.
def private HasLiveParent(relationship : Relationship) : bool {
    return relationship.parent != INVALID_ENTITY_ID && decs::is_alive(relationship.parent)
}

//! Brings one collider entity in line with its components. Called from inside a query, so
//! component changes are only queued; ReconcileComponents commits after all queries.
def private EnsureBodyForCollider(eid : EntityId; desc : PhysicsBodyDesc; parented : bool) : bool {
    let signature = BodySignature(desc)
    let current = get_component(eid, "physicsBody", PhysicsBody())

    // A child's local Transform is not a world pose, so components do not own a body there.
    if (parented) {
        if (current.handle != 0ul) {
            PhysicsDestroyBody(current.handle)
            RemovePhysicsBodyComponent(eid)
            return true
        }
        return false
    }

    if (current.handle != 0ul && current.managed && current.signature == signature &&
        PhysicsIsBodyAlive(current.handle)) {
        return false
    }

    if (current.handle != 0ul) {
        PhysicsDestroyBody(current.handle)
    }

    var owned = desc
    owned.entity = eid
    let body = CreateNativeBody(owned)
    if (body == 0ul) {
        if (current.handle != 0ul) {
            RemovePhysicsBodyComponent(eid)
            return true
        }
        return false
    }
    SetPhysicsBodyComponent(eid, PhysicsBody(handle = body, signature = signature, managed = true))
    return true
}

//! Reconciles the ECS components into Jolt bodies. The five collider queries exclude the
//! higher-priority colliders, so every entity is handled exactly once; no per-frame table
//! records which entities were seen. A collider without RigidBody is static.
[profile]
def private ReconcileComponents() {
    var changed = false

    query() $ [REQUIRE_NOT(characterController)] (eid : EntityId; transform : Transform; boxCollider : BoxCollider) {
        let settings = get_component(eid, "rigidBody", RigidBody(motion = int(PhysicsMotion.Static)))
        let relationship = get_component(eid, "relationship", Relationship())
        let desc = MakeBodyDesc(transform, settings, PhysicsShape.Box, boxCollider.half_extents, 0.0f, 0.0f)
        if (EnsureBodyForCollider(eid, desc, HasLiveParent(relationship))) {
            changed = true
        }
    }

    query() $ [REQUIRE_NOT(boxCollider, characterController)] (eid : EntityId; transform : Transform; sphereCollider : SphereCollider) {
        let settings = get_component(eid, "rigidBody", RigidBody(motion = int(PhysicsMotion.Static)))
        let relationship = get_component(eid, "relationship", Relationship())
        let desc = MakeBodyDesc(transform, settings, PhysicsShape.Sphere, float3(0.0f, 0.0f, 0.0f),
                                sphereCollider.radius, 0.0f)
        if (EnsureBodyForCollider(eid, desc, HasLiveParent(relationship))) {
            changed = true
        }
    }

    query() $ [REQUIRE_NOT(boxCollider, sphereCollider, characterController)] (eid : EntityId; transform : Transform; capsuleCollider : CapsuleCollider) {
        let settings = get_component(eid, "rigidBody", RigidBody(motion = int(PhysicsMotion.Static)))
        let relationship = get_component(eid, "relationship", Relationship())
        let desc = MakeBodyDesc(transform, settings, PhysicsShape.Capsule, float3(0.0f, 0.0f, 0.0f),
                                capsuleCollider.radius, capsuleCollider.half_height)
        if (EnsureBodyForCollider(eid, desc, HasLiveParent(relationship))) {
            changed = true
        }
    }

    query() $ [REQUIRE_NOT(boxCollider, sphereCollider, capsuleCollider, characterController)] (eid : EntityId; transform : Transform; cylinderCollider : CylinderCollider) {
        let settings = get_component(eid, "rigidBody", RigidBody(motion = int(PhysicsMotion.Static)))
        let relationship = get_component(eid, "relationship", Relationship())
        let desc = MakeBodyDesc(transform, settings, PhysicsShape.Cylinder, float3(0.0f, 0.0f, 0.0f),
                                cylinderCollider.radius, cylinderCollider.half_height)
        if (EnsureBodyForCollider(eid, desc, HasLiveParent(relationship))) {
            changed = true
        }
    }

    query() $ [REQUIRE_NOT(boxCollider, sphereCollider, capsuleCollider, cylinderCollider, characterController)] (eid : EntityId; transform : Transform; planeCollider : PlaneCollider) {
        let settings = get_component(eid, "rigidBody", RigidBody(motion = int(PhysicsMotion.Static)))
        let relationship = get_component(eid, "relationship", Relationship())
        let desc = MakeBodyDesc(transform, settings, PhysicsShape.Plane, planeCollider.half_extents, 0.0f, 0.0f)
        if (EnsureBodyForCollider(eid, desc, HasLiveParent(relationship))) {
            changed = true
        }
    }

    // An entity that became a character drops the body it had before. The collider queries
    // skip character entities, so this is the only place that removes such a body.
    query() $(eid : EntityId; physicsBody : PhysicsBody; characterController : CharacterController) {
        if (physicsBody.handle != 0ul) {
            PhysicsDestroyBody(physicsBody.handle)
        }
        RemovePhysicsBodyComponent(eid)
        changed = true
    }

    // Managed bodies whose entity lost every collider. Manual bodies (managed = false) are
    // left alone: they belong to the caller who created them.
    query() $ [REQUIRE_NOT(boxCollider, sphereCollider, capsuleCollider, cylinderCollider, planeCollider)] (eid : EntityId; physicsBody : PhysicsBody) {
        if (physicsBody.managed) {
            PhysicsDestroyBody(physicsBody.handle)
            RemovePhysicsBodyComponent(eid)
            changed = true
        }
    }

    // commit() applies every deferred DECS action, not only physics ones; avoid flushing
    // unrelated script work on frames where no body lifecycle changed.
    if (changed) {
        commit()
    }
}

//! Reconciles CapsuleCollider + CharacterController into Jolt characters. A capsule without
//! the controller stays a rigid body (or a static collider); the controller without a capsule
//! does nothing. The body queries skip character entities, so an entity is one or the other.
[profile]
def private ReconcileCharacters() {
    var changed = false

    query() $(eid : EntityId; transform : Transform; capsuleCollider : CapsuleCollider;
              characterController : CharacterController) {
        let relationship = get_component(eid, "relationship", Relationship())
        let desc = MakeCharacterDesc(transform, characterController, capsuleCollider)
        if (EnsureCharacterForCollider(eid, desc, HasLiveParent(relationship))) {
            changed = true
        }
    }

    // A character whose capsule is gone. An entity missing only the controller is covered by
    // the second query; the two do not overlap because one requires the capsule and the other
    // requires its absence.
    query() $ [REQUIRE_NOT(capsuleCollider)] (eid : EntityId; characterBody : CharacterBody) {
        PhysicsDestroyCharacter(characterBody.handle)
        RemoveCharacterBodyComponent(eid)
        changed = true
    }

    query() $ [REQUIRE(capsuleCollider), REQUIRE_NOT(characterController)] (eid : EntityId; characterBody : CharacterBody) {
        PhysicsDestroyCharacter(characterBody.handle)
        RemoveCharacterBodyComponent(eid)
        changed = true
    }

    if (changed) {
        commit()
    }
}

def private SamePosePosition(a : float3; b : float3) : bool {
    return abs(a.x - b.x) <= 1e-4f && abs(a.y - b.y) <= 1e-4f && abs(a.z - b.z) <= 1e-4f
}

def private SamePoseRotation(a : float4; b : float4) : bool {
    let dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w
    return abs(dot) >= 1.0f - 1e-4f
}

//! Bodies follow their entity pose. Static and kinematic bodies always do: the Inspector
//! can move a level collider and a moving platform needs its new pose before the step.
//! A managed dynamic body is pushed only when Transform differs from the pose PhysicsSync
//! last wrote, which is exactly the gizmo/Inspector edit case; otherwise Jolt owns it.
//! Pushes entity poses into static and kinematic bodies, and detects a body whose owner is
//! gone: a deleted entity has no PhysicsBody component anymore, and a removed component is
//! the supported way to drop a body. Orphans are collected first because BodyAt is dense.
[profile]
def private PhysicsPushPoses() {
    var orphans : array<uint64>
    let count = PhysicsBodyCount()
    for (i in range(count)) {
        let body = PhysicsBodyAt(i)
        if (body == 0ul) {
            continue
        }
        let entity = PhysicsBodyEntity(body)
        if (entity == 0ul) {
            continue
        }
        let eid = EntityFromRaw(entity)
        if (!decs::is_alive(eid)) {
            orphans |> push(body)
            continue
        }
        let dynamic = PhysicsBodyMotion(body) == int(PhysicsMotion.Dynamic)
        let physicsBody = get_component(eid, "physicsBody", PhysicsBody())
        if (physicsBody.handle != body) {
            orphans |> push(body)
            continue
        }
        let managed = physicsBody.managed
        query(eid) $(transform : Transform) {
            if (!dynamic) {
                PhysicsSetBodyTransform(body, transform.position, transform.rotation)
            } elif (managed) {
                let body_position = PhysicsGetBodyPosition(body)
                let body_rotation = PhysicsGetBodyRotation(body)
                if (!SamePosePosition(transform.position, body_position) ||
                    !SamePoseRotation(transform.rotation, body_rotation)) {
                    PhysicsSetBodyTransform(body, transform.position, transform.rotation)
                    PhysicsSetLinearVelocity(body, float3(0.0f, 0.0f, 0.0f))
                    PhysicsSetAngularVelocity(body, float3(0.0f, 0.0f, 0.0f))
                }
            }
        }
    }
    for (body in orphans) {
        PhysicsDestroyBody(body)
    }
}

//! Character entities follow their Transform the same way dynamic bodies do: a difference
//! between the entity pose and the character pose is an Inspector/gizmo edit, so the character
//! is teleported there. The entity Transform is the capsule center; the character API takes the
//! feet. An untouched character has equal poses, because CharacterSync wrote the Transform.
[profile]
def private CharacterPushPoses() {
    query() $(var transform : Transform; characterController : CharacterController;
              capsuleCollider : CapsuleCollider; characterBody : CharacterBody) {
        let handle = characterBody.handle
        if (handle != 0ul && PhysicsIsCharacterAlive(handle)) {
            let center = PhysicsGetCharacterCenter(handle)
            let rotation = PhysicsGetCharacterRotation(handle)
            if (!SamePosePosition(transform.position, center) || !SamePoseRotation(transform.rotation, rotation)) {
                let feet = transform.position - float3(0.0f, 0.0f, capsuleCollider.radius + capsuleCollider.half_height)
                PhysicsSetCharacterPosition(handle, feet)
                PhysicsSetCharacterRotation(handle, transform.rotation)
            }
        }
    }
}

//! ConstantForce is applied on every physics tick, just before the step that consumes it.
//! A static or kinematic body ignores the force; a character has no body component to match.
[profile]
def private ApplyConstantForces() {
    query() $(constantForce : ConstantForce; physicsBody : PhysicsBody) {
        if (physicsBody.handle != 0ul) {
            if (length(constantForce.force) > 0.0f) {
                PhysicsApplyForce(physicsBody.handle, constantForce.force)
            }
            if (length(constantForce.torque) > 0.0f) {
                PhysicsApplyTorque(physicsBody.handle, constantForce.torque)
            }
        }
    }
}

def private PhysicsSync() {
    let count = PhysicsBodyCount()
    for (i in range(count)) {
        let body = PhysicsBodyAt(i)
        if (body == 0ul || PhysicsBodyMotion(body) == int(PhysicsMotion.Static)) {
            continue
        }
        let entity = PhysicsBodyEntity(body)
        if (entity == 0ul) {
            continue
        }
        let eid = EntityFromRaw(entity)
        if (!decs::is_alive(eid)) {
            continue
        }
        let position = PhysicsGetBodyPosition(body)
        let rotation = PhysicsGetBodyRotation(body)
        query(eid) $(var transform : Transform) {
            transform.position = position
            transform.rotation = rotation
        }
    }
}

//! Copies each character's capsule center into the Transform of the entity it was created
//! for. A character without an entity belongs to the caller and is left alone. Characters
//! whose entity is gone are dropped here, the same way PhysicsPushPoses prunes bodies.
def private CharacterSync() {
    var orphans : array<uint64>
    let count = PhysicsCharacterCount()
    for (i in range(count)) {
        let character = PhysicsCharacterAt(i)
        if (character == 0ul) {
            continue
        }
        let entity = PhysicsCharacterEntity(character)
        if (entity == 0ul) {
            continue
        }
        let eid = EntityFromRaw(entity)
        if (!decs::is_alive(eid)) {
            orphans |> push(character)
            continue
        }
        // A child's local Transform is not a world pose, so components do not own a body
        // there; the same rule applies to the entity a character follows.
        let relationship = get_component(eid, "relationship", Relationship())
        if (relationship.parent != INVALID_ENTITY_ID) {
            continue
        }
        let center = PhysicsGetCharacterCenter(character)
        let rotation = PhysicsGetCharacterRotation(character)
        query(eid) $(var transform : Transform) {
            transform.position = center
            transform.rotation = rotation
        }
    }
    for (character in orphans) {
        PhysicsDestroyCharacter(character)
    }
}

def private CreateNativeBody(desc : PhysicsBodyDesc) : uint64 {
    // A computed argument must land in a local first: passing a complex expression straight
    // to a native call makes the simulator build a bad call node (daslang 0.6.5, reproduced
    // with tryPhysics). An unset entity is zero for C++, not the INVALID_ENTITY_ID sentinel.
    var entity = 0ul
    if (desc.entity != INVALID_ENTITY_ID) {
        entity = EntityRaw(desc.entity)
    }
    return PhysicsCreateBody(
        int(desc.shape),
        int(desc.motion),
        desc.position,
        desc.rotation,
        desc.half_extents,
        desc.radius,
        desc.half_height,
        desc.mass,
        desc.friction,
        desc.restitution,
        desc.linear_velocity,
        desc.angular_velocity,
        entity,
        desc.sensor
    )
}

//! Creates a body and returns a stable handle (0 when Jolt rejected it). With an entity it
//! also records a manual PhysicsBody component, so FindBody and the prune pass can see it.
//! Commits pending DECS actions: call it from gameplay, not from inside a query.
def CreateBody(desc : PhysicsBodyDesc) : uint64 {
    if (desc.entity != INVALID_ENTITY_ID) {
        let existing = FindBody(desc.entity)
        if (existing != 0ul) {
            DestroyBody(existing)
        }
    }

    let body = CreateNativeBody(desc)
    if (body != 0ul && desc.entity != INVALID_ENTITY_ID && decs::is_alive(desc.entity)) {
        SetPhysicsBodyComponent(desc.entity, PhysicsBody(handle = body, signature = 0ul, managed = false))
        commit()
    }
    return body
}

//! Creates a body and remembers `eid` as its owner. Prefer collider components: a body
//! created here is not managed by the component sync and is replaced if the entity later
//! gets a collider component.
def CreateBodyForEntity(desc : PhysicsBodyDesc; eid : EntityId) : uint64 {
    var owned = desc
    owned.entity = eid
    return CreateBody(owned)
}

//! Destroys a body and drops the matching PhysicsBody component. Commits pending DECS
//! actions: call it from gameplay, not from inside a query.
def DestroyBody(body : uint64) {
    let entity = PhysicsBodyEntity(body)
    if (entity != 0ul) {
        let eid = EntityFromRaw(entity)
        if (decs::is_alive(eid)) {
            let current = get_component(eid, "physicsBody", PhysicsBody())
            if (current.handle == body) {
                RemovePhysicsBodyComponent(eid)
                commit()
            }
        }
    }
    PhysicsDestroyBody(body)
}

//! Destroys the body owned by `eid`, if any.
def DestroyBodyForEntity(eid : EntityId) {
    let body = FindBody(eid)
    if (body != 0ul) {
        DestroyBody(body)
    }
}

// --- Character controller ---------------------------------------------------------

//! Creates a virtual character and returns a stable handle (0 when Jolt rejected it). A
//! character is not a rigid body: other bodies do not see it, but it collides with the
//! world, climbs steps, slides on slopes and pushes dynamic bodies aside. Characters do not
//! collide with each other, and there is one character per entity. It moves on the physics
//! clock, so pausing the game freezes it. Physics::Reset drops every body and character at
//! once.
def CreateCharacter(desc : CharacterDesc) : uint64 {
    var entity = 0ul
    if (desc.entity != INVALID_ENTITY_ID) {
        entity = EntityRaw(desc.entity)
    }
    return PhysicsCreateCharacter(desc.position, desc.rotation, desc.radius, desc.half_height,
                                  desc.mass, desc.max_strength, desc.max_slope_angle, desc.jump_speed,
                                  entity)
}

def DestroyCharacter(character : uint64) {
    PhysicsDestroyCharacter(character)
}

def IsCharacterAlive(character : uint64) : bool {
    return PhysicsIsCharacterAlive(character)
}

//! Horizontal intent in world m/s (Z is ignored) plus a latched jump request. Call it every
//! frame: the velocity stays in effect until the next call, and Physics::Update applies
//! gravity, jumping, step climbing and slope sliding on its fixed step.
def SetCharacterInput(character : uint64; velocity : float3; jump : bool = false) {
    PhysicsSetCharacterInput(character, velocity, jump)
}

//! Position of the feet, the same point CharacterDesc::position sets.
def GetCharacterPosition(character : uint64) : float3 {
    return PhysicsGetCharacterPosition(character)
}

//! Capsule center: the pose written into an entity Transform when the character has one.
def GetCharacterCenter(character : uint64) : float3 {
    return PhysicsGetCharacterCenter(character)
}

def GetCharacterRotation(character : uint64) : float4 {
    return PhysicsGetCharacterRotation(character)
}

//! Actual velocity after the last step, including gravity and the ground's motion.
def GetCharacterVelocity(character : uint64) : float3 {
    return PhysicsGetCharacterVelocity(character)
}

//! True while the character stands on a walkable surface and can jump.
def IsCharacterGrounded(character : uint64) : bool {
    return PhysicsIsCharacterGrounded(character)
}

//! Teleports the character and drops its accumulated velocity, so it does not continue moving
//! from the old point.
def SetCharacterPosition(character : uint64; position : float3) {
    PhysicsSetCharacterPosition(character, position)
}

def SetCharacterRotation(character : uint64; rotation : float4) {
    PhysicsSetCharacterRotation(character, rotation)
}

// --- Component helpers ------------------------------------------------------------
//
// They only edit the ECS; the body appears on the next Physics::Update. A collider
// alone is static, so the common pair is AddBoxCollider followed by AddRigidBody.

def AddRigidBody(eid : EntityId; motion : PhysicsMotion = PhysicsMotion.Dynamic; mass : float = 1.0f;
                 friction : float = 0.5f; restitution : float = 0.0f; sensor : bool = false) {
    update_entity(eid) @(eid, cmp) {
        cmp |> set("rigidBody", RigidBody(motion = int(motion), mass = mass, friction = friction,
                                         restitution = restitution, sensor = sensor))
    }
    commit()
}

def AddBoxCollider(eid : EntityId; half_extents : float3) {
    update_entity(eid) @(eid, cmp) {
        cmp |> set("boxCollider", BoxCollider(half_extents = half_extents))
    }
    commit()
}

def AddSphereCollider(eid : EntityId; radius : float) {
    update_entity(eid) @(eid, cmp) {
        cmp |> set("sphereCollider", SphereCollider(radius = radius))
    }
    commit()
}

def AddCapsuleCollider(eid : EntityId; radius : float; half_height : float) {
    update_entity(eid) @(eid, cmp) {
        cmp |> set("capsuleCollider", CapsuleCollider(radius = radius, half_height = half_height))
    }
    commit()
}

def AddCylinderCollider(eid : EntityId; radius : float; half_height : float) {
    update_entity(eid) @(eid, cmp) {
        cmp |> set("cylinderCollider", CylinderCollider(radius = radius, half_height = half_height))
    }
    commit()
}

def AddPlaneCollider(eid : EntityId; half_extents : float3 = float3(10.0f, 10.0f, 0.0f)) {
    update_entity(eid) @(eid, cmp) {
        cmp |> set("planeCollider", PlaneCollider(half_extents = half_extents))
    }
    commit()
}

def IsBodyAlive(body : uint64) : bool {
    return PhysicsIsBodyAlive(body)
}

def FindBody(eid : EntityId) : uint64 {
    return get_component(eid, "physicsBody", PhysicsBody()).handle
}

def private EntityFromBody(body : uint64) : EntityId {
    if (body == 0ul) {
        return INVALID_ENTITY_ID
    }
    let entity = PhysicsBodyEntity(body)
    if (entity == 0ul) {
        return INVALID_ENTITY_ID
    }
    return EntityFromRaw(entity)
}

def BodyEntity(body : uint64) : EntityId {
    return EntityFromRaw(PhysicsBodyEntity(body))
}

def GetBodyPosition(body : uint64) : float3 {
    return PhysicsGetBodyPosition(body)
}

def GetBodyRotation(body : uint64) : float4 {
    return PhysicsGetBodyRotation(body)
}

def SetBodyPosition(body : uint64; position : float3) {
    PhysicsSetBodyPosition(body, position)
}

def SetBodyRotation(body : uint64; rotation : float4) {
    PhysicsSetBodyRotation(body, rotation)
}

def SetBodyTransform(body : uint64; position : float3; rotation : float4) {
    PhysicsSetBodyTransform(body, position, rotation)
}

def GetLinearVelocity(body : uint64) : float3 {
    return PhysicsGetLinearVelocity(body)
}

def SetLinearVelocity(body : uint64; velocity : float3) {
    PhysicsSetLinearVelocity(body, velocity)
}

def GetAngularVelocity(body : uint64) : float3 {
    return PhysicsGetAngularVelocity(body)
}

def SetAngularVelocity(body : uint64; velocity : float3) {
    PhysicsSetAngularVelocity(body, velocity)
}

def ApplyImpulse(body : uint64; impulse : float3) {
    PhysicsApplyImpulse(body, impulse)
}

def ApplyImpulseAt(body : uint64; impulse : float3; point : float3) {
    PhysicsApplyImpulseAt(body, impulse, point)
}

//! Applies a force (N) to the body's center of mass for one frame: the next physics step
//! turns it into an impulse F * dt, so acceleration does not depend on the frame rate and
//! calls made in the same frame add up. Call it every frame while the force acts; a
//! one-shot push is ApplyImpulse. Only dynamic bodies accept force.
def ApplyForce(body : uint64; force : float3) {
    PhysicsApplyForce(body, force)
}

//! ApplyForce at a world-space point: the offset from the center of mass adds torque.
def ApplyForceAt(body : uint64; force : float3; point : float3) {
    PhysicsApplyForceAt(body, force, point)
}

//! Applies torque (N*m) for one frame, the same way ApplyForce does; call it every frame
//! while the torque acts.
def ApplyTorque(body : uint64; torque : float3) {
    PhysicsApplyTorque(body, torque)
}

def SetMotionType(body : uint64; motion : PhysicsMotion) {
    PhysicsSetMotionType(body, int(motion))
}

def GetBodyMotion(body : uint64) : PhysicsMotion {
    let motion = PhysicsBodyMotion(body)
    if (motion == int(PhysicsMotion.Kinematic)) {
        return PhysicsMotion.Kinematic
    }
    if (motion == int(PhysicsMotion.Dynamic)) {
        return PhysicsMotion.Dynamic
    }
    return PhysicsMotion.Static
}

def SetGravity(gravity : float3) {
    PhysicsSetGravity(gravity)
}

def GetGravity() : float3 {
    return PhysicsGetGravity()
}

def SetFixedStep(step : float) {
    PhysicsSetFixedStep(step)
}

def SetMaxSubsteps(steps : int) {
    PhysicsSetMaxSubsteps(steps)
}

def SetDebugDraw(enabled : bool) {
    PhysicsSetDebugDraw(enabled)
}

def IsDebugDraw() : bool {
    return PhysicsDebugDraw()
}

//! Begin-contact events of the last step, deduplicated by body pair. The entity fields
//! let gameplay react without keeping a body-handle table of its own.
def Contacts() : array<PhysicsContact> {
    var contacts : array<PhysicsContact>
    for (i in range(PhysicsContactCount())) {
        let body_a = PhysicsContactBodyA(i)
        let body_b = PhysicsContactBodyB(i)
        contacts |> push(PhysicsContact(
            body_a = body_a,
            body_b = body_b,
            point = PhysicsContactPoint(i),
            normal = PhysicsContactNormal(i),
            depth = PhysicsContactDepth(i),
            entity_a = EntityFromBody(body_a),
            entity_b = EntityFromBody(body_b)
        ))
    }
    return <- contacts
}
