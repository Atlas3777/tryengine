options gen2
options relaxed_assign = false

module TransformSystem shared public

//! Keeps the derived world transform in sync with the authored local hierarchy.
//!
//! `Transform` is the authored local transform (saved to scenes, edited in the
//! Inspector). `WorldTransform` is runtime-only (`_world_*` components, skipped by
//! scene save and the Inspector) and is what Render.das reads. Every frame this
//! module walks Relationship parents and writes the composed world TRS.

require math
require daslib/decs
require daslib/decs_boost
require tryengine/Profiling
require tryengine/Types

// Plain value holder: Transform/WorldTransform are decs_template types, and a whole
// template value cannot be stored in a table. The render structs stay field-only.
struct TransformData {
    position : float3
    rotation : float4
    scale    : float3
}

def private ComposeWorld(parent : TransformData; local : TransformData) : TransformData {
    var world : TransformData
    world.position = parent.position + quat_mul_vec(parent.rotation, parent.scale * local.position)
    world.rotation = normalize(quat_mul(parent.rotation, local.rotation))
    world.scale = parent.scale * local.scale
    return <- world
}

def private SameTransform(a : TransformData; b : TransformData) : bool {
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

def private ResolveWorld(index : int;
                         var locals : array<TransformData>;
                         var parents : array<int>;
                         var worlds : array<TransformData>;
                         var state : array<uint8>) : TransformData {
    if (state[index] == 2u8) {
        return worlds[index]
    }
    if (state[index] == 1u8) {
        // A cycle in Relationship: keep the authored local instead of recursing forever.
        return locals[index]
    }

    state[index] = 1u8
    let parent_index = parents[index]
    var world = locals[index]
    if (parent_index >= 0 && parent_index != index) {
        let parent_world = ResolveWorld(parent_index, locals, parents, worlds, state)
        world = ComposeWorld(parent_world, locals[index])
    }

    worlds[index] = world
    state[index] = 2u8
    return <- world
}

//! Recomputes `_world_*` for every entity with a local Transform. Runs once per frame
//! after game/editor logic and before rendering.
[export, profile]
def UpdateWorldTransforms() {
    var entity_ids : array<EntityId>
    var locals : array<TransformData>
    var index_of : table<uint; int>

    query() $(eid : EntityId; transform : Transform) {
        index_of[eid.id] = length(entity_ids)
        entity_ids |> push(eid)
        locals |> emplace(TransformData(
            position = transform.position,
            rotation = transform.rotation,
            scale = transform.scale
        ))
    }

    let count = length(entity_ids)
    if (count == 0) {
        return
    }

    var parents : array<int>
    resize(parents, count)
    for (i in range(count)) {
        parents[i] = -1
    }

    query() $(eid : EntityId; rel : Relationship) {
        if (rel.parent == INVALID_ENTITY_ID) {
            return
        }
        if (!key_exists(index_of, eid.id) || !key_exists(index_of, rel.parent.id)) {
            return
        }
        parents[index_of[eid.id]] = index_of[rel.parent.id]
    }

    var current : array<TransformData>
    var has_current : array<bool>
    resize(current, count)
    resize(has_current, count)
    for (i in range(count)) {
        has_current[i] = false
    }

    query() $(eid : EntityId; world : WorldTransform) {
        if (!key_exists(index_of, eid.id)) {
            return
        }
        let index = index_of[eid.id]
        current[index] = TransformData(
            position = world.position,
            rotation = world.rotation,
            scale = world.scale
        )
        has_current[index] = true
    }

    var worlds : array<TransformData>
    var state : array<uint8>
    resize(worlds, count)
    resize(state, count)
    for (i in range(count)) {
        state[i] = 0u8
    }

    var changed = false
    for (index in range(count)) {
        let world = ResolveWorld(index, locals, parents, worlds, state)
        if (has_current[index] && SameTransform(current[index], world)) {
            continue
        }

        let eid = entity_ids[index]
        update_entity(eid) @(eid, cmp) {
            apply_decs_template(cmp, WorldTransform(
                position = world.position,
                rotation = world.rotation,
                scale = world.scale
            ))
        }
        changed = true
    }

    if (changed) {
        commit()
    }
}
