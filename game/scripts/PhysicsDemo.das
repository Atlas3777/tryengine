options gen2
options relaxed_assign = false

module PhysicsDemo shared public

//! Small runtime proof that Jolt is wired into the game loop: a static floor and a row
//! of dynamic boxes that fall onto it. Both are ordinary ECS entities with collider
//! components; Physics::Update creates the bodies and DebugPass draws their colliders on
//! top of the scene.

require math
require daslib/decs
require daslib/decs_boost
require tryengine/EcsMacro
require tryengine/Physics
require tryengine/Prefab
require tryengine/TransformSystem
require tryengine/Types

let private DEMO_PREFAB = "prefabl.prefab"

var private g_started : bool

def Reset() {
    g_started = false
}

def Start() {
    if (g_started) {
        return
    }
    g_started = true

    // The camera orbits around y=8, z=10; the floor sits below it. Without RigidBody the
    // collider is a static part of the level.
    create_entity() @(eid, cmp) {
        cmp.tag := "PhysicsGround"
        ApplyEntityTransform(cmp, float3(0.0f, 8.0f, -0.5f), float4(0.0f, 0.0f, 0.0f, 1.0f),
                             float3(1.0f, 1.0f, 1.0f))
        cmp |> set("boxCollider", BoxCollider(half_extents = float3(20.0f, 20.0f, 0.5f)))
        cmp |> set(RELATIONSHIP_FIELD, Relationship())
    }
    commit()

    for (i in range(6)) {
        let position = float3(-5.0f + float(i) * 2.0f, 8.0f, 2.0f + float(i) * 0.6f)
        let root = Prefab::Spawn(DEMO_PREFAB, position)
        if (root == INVALID_ENTITY_ID) {
            continue
        }

        Physics::AddBoxCollider(root, float3(0.5f, 0.5f, 0.5f))
        Physics::AddRigidBody(root, friction = 0.6f, restitution = 0.1f)
    }
}
