options gen2
options relaxed_assign = false

module TransformSystem shared public

//! Keeps the derived world transform in sync with the authored local hierarchy.
//!
//! `Transform` is one authored component (`transform`): the Inspector edits it and scene save
//! writes it. The derived world transform is the same `Transform` shape in the component `_world`;
//! `Render.das` reads it.
//!
//! `_world` is created together with `transform` and is never added here: adding a component
//! moves the entity to another archetype and copies its whole row, which used to cost more
//! than the rest of the pass. Prefabs bake it into the compiled template, scenes and editor
//! entities apply it next to `transform`; an entity created without it is a creation bug and
//! the first write panics with the missing `_world`.
//!
//! The pass walks the Relationship forest downwards from its roots (`first_child`/`next`),
//! so a parent is always composed before its children and no parent table is needed.
//! `_world` is written straight into archetype storage as one struct and only when the value
//! actually changed, so a static scene costs one scan and no DECS writes at all.

require math
require daslib/decs
require daslib/decs_boost
require tryengine/Profiling
require tryengine/Types

let public TRANSFORM_FIELD : string = "transform"
let public WORLD_FIELD : string = "_world"

//! Composes a child's world transform from its parent's world transform and its local one.
def public ComposeWorld(parent : Transform; local : Transform) : Transform {
    var world = Transform()
    world.position = parent.position + quat_mul_vec(parent.rotation, parent.scale * local.position)
    world.rotation = normalize(quat_mul(parent.rotation, local.rotation))
    world.scale = parent.scale * local.scale
    return <- world
}

//! Applies the authored `transform` and the derived `_world` in one call. The pass never adds
//! `_world` to an existing entity (that moves it to another archetype and copies its whole
//! row), so both components are always applied together when the entity is created. The world
//! values start as the local ones and the next pass composes the hierarchy.
def public ApplyEntityTransform(var cmp : ComponentMap; transform : Transform) {
    cmp |> set(TRANSFORM_FIELD, transform)
    cmp |> set(WORLD_FIELD, transform)
}

//! Same, from the three values a creation site usually has at hand.
def public ApplyEntityTransform(var cmp : ComponentMap; position : float3; rotation : float4; scale : float3) {
    ApplyEntityTransform(cmp, Transform(position = position, rotation = rotation, scale = scale))
}

def private SameTransform(a : Transform; b : Transform) : bool {
    if (a.position.x != b.position.x) return false
    if (a.position.y != b.position.y) return false
    if (a.position.z != b.position.z) return false
    if (a.rotation.x != b.rotation.x) return false
    if (a.rotation.y != b.rotation.y) return false
    if (a.rotation.z != b.rotation.z) return false
    if (a.rotation.w != b.rotation.w) return false
    if (a.scale.x != b.scale.x) return false
    if (a.scale.y != b.scale.y) return false
    if (a.scale.z != b.scale.z) return false
    return true
}

// Sparse scratch reused between frames, indexed by entity id. Nothing here allocates once
// the arrays are grown: a frame is one scan, the forest walk and the writes that are needed.
var private g_local : array<Transform>
var private g_local_ok : array<bool>
var private g_first_child : array<EntityId>
var private g_next_sibling : array<EntityId>
var private g_parent : array<EntityId>
var private g_link_ok : array<bool>
var private g_visited : array<bool>
// What was last written to `_world`, so an unchanged subtree costs no DECS write.
var private g_world : array<Transform>
var private g_world_ok : array<bool>
var private g_world_generation : array<int>
// Component index of `_world` per archetype, resolved on the first write of that archetype.
var private g_world_index : table<int; int>

def private WriteWorld(eid : EntityId; world : Transform) {
    let record = decsState.entityLookup[int(eid.id)]
    let arch_index = int(decsState.archetypeLookup[record.archetype]) - 1
    if (!key_exists(g_world_index, arch_index)) {
        g_world_index[arch_index] = decs::find_component_index(decsState.allArchetypes[arch_index], WORLD_FIELD)
    }
    decs::set_direct_at(decsState.allArchetypes[arch_index], g_world_index[arch_index], int(record.index), world)
}

def private VisitSubtree(eid : EntityId; parent_world : Transform; has_parent : bool) {
    let index = int(eid.id)
    g_visited[index] = true

    // A root's world transform is its local one; only a parented node needs the compose.
    var world = g_local[index]
    if (has_parent) {
        world = ComposeWorld(parent_world, g_local[index])
    }

    // A repeated write of the same value is the common case in a static scene, and skipping
    // it is what keeps the pass cheap: compare with what was written last time.
    var dirty = !g_world_ok[index] || g_world_generation[index] != eid.generation
    dirty = dirty || !SameTransform(g_world[index], world)
    if (dirty) {
        WriteWorld(eid, world)
        g_world[index] = world
        g_world_ok[index] = true
        g_world_generation[index] = eid.generation
    }

    if (!g_link_ok[index]) {
        return
    }

    var child = g_first_child[index]
    while (child != INVALID_ENTITY_ID) {
        let child_index = int(child.id)
        if (child_index < length(g_local_ok) && !g_visited[child_index] && g_local_ok[child_index]) {
            VisitSubtree(child, world, true)
        }
        // A child without a Relationship (or a broken link) ends the sibling chain.
        if (child_index >= length(g_link_ok) || !g_link_ok[child_index]) {
            break
        }
        child = g_next_sibling[child_index]
    }
}

//! Recomputes `_world` for every entity with a local Transform. Runs once per frame after
//! game/editor logic and before rendering. Only changed transforms are written.
[export, profile]
def UpdateWorldTransforms() {
    let slots = length(decsState.entityLookup)
    if (slots == 0) {
        return
    }

    resize(g_local, slots)
    resize(g_local_ok, slots)
    resize(g_first_child, slots)
    resize(g_next_sibling, slots)
    resize(g_parent, slots)
    resize(g_link_ok, slots)
    resize(g_visited, slots)
    resize(g_world, slots)
    resize(g_world_ok, slots)
    resize(g_world_generation, slots)

    var has_local = 0
    query() $(eid : EntityId, transform : Transform) {
        let index = int(eid.id)
        g_local[index] = transform
        g_local_ok[index] = true
        g_visited[index] = false
        g_link_ok[index] = false
        has_local++
    }

    if (has_local == 0) {
        return
    }

    query() $(eid : EntityId, rel : Relationship) {
        let index = int(eid.id)
        if (index >= slots) {
            return
        }
        g_first_child[index] = rel.first_child
        g_next_sibling[index] = rel.next
        g_parent[index] = rel.parent
        g_link_ok[index] = true
    }

    // Roots: every transformed entity with no link to a parent. Singletons (no Relationship
    // at all) are roots too and simply take their local transform as the world one.
    var identity = Transform()
    for (index in range(slots)) {
        if (!g_local_ok[index] || g_visited[index]) {
            continue
        }
        // A parented entity is reached from its root. An orphan (parent deleted or without a
        // Transform) is a root itself, otherwise its subtree would never be visited.
        if (g_link_ok[index] && g_parent[index] != INVALID_ENTITY_ID) {
            let parent_index = int(g_parent[index].id)
            if (parent_index < slots && g_local_ok[parent_index]) {
                continue
            }
        }

        let eid = EntityId(id = uint(index), generation = decsState.entityLookup[index].generation)
        VisitSubtree(eid, identity, false)
    }
}
