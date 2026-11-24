options gen2
options relaxed_assign = false

module TransformSystem shared public

//! Keeps the derived world transform in sync with the authored local hierarchy.
//!
//! `transform` is the authored component: the Inspector edits it and a scene writes it. The
//! derived world transform is the same `Transform` type under the component name `_world`, a
//! declared `transient = true, hidden = true` component, so there is one transform layout and
//! nothing to keep in step between two structures. `Render.das` reads `_world`.
//!
//! `_world` is created together with `transform` and is never added here: adding a component
//! moves the entity to another archetype and copies its whole row. Prefabs bake it into the
//! compiled template and scenes and editor entities apply it next to `transform`; an entity
//! created without it simply has no world transform.
//!
//! A pass needs a parent-before-child order, which DECS iteration does not have, so it builds
//! a work list for the frame: one scan collects every transformed entity with its links and
//! its local transform, the walk composes the worlds in place in that list, and one scan
//! writes the results back into archetype storage. The list is rebuilt from DECS on every
//! pass and holds no authoritative data — nothing is cached between frames, so there is no
//! invalidation to get wrong. Composing through a query per entity instead was measured
//! ~4x slower on a 1000-entity hierarchy.

require math
require daslib/decs_boost
require tryengine/Profiling
require tryengine/Types

let public TRANSFORM_FIELD : string = "transform"
let public WORLD_FIELD : string = "_world"

// A hierarchy this deep is a cycle in the parent links, not a scene.
let private MAX_HIERARCHY_DEPTH : int = 512

//! Composes a child's world transform from its parent's world transform and its local one.
def public ComposeWorld(parent : Transform; local : Transform) : Transform {
    return Transform(
        position = parent.position + quat_mul_vec(parent.rotation, parent.scale * local.position),
        rotation = normalize(quat_mul(parent.rotation, local.rotation)),
        scale = parent.scale * local.scale
    )
}

//! Applies the authored `transform` and the derived `_world` in one call. The pass never adds
//! `_world` to an existing entity, so both components are always applied together when the
//! entity is created. The world value starts as the local one and the next pass composes the
//! hierarchy.
def public ApplyEntityTransform(var cmp : ComponentMap; transform : Transform) {
    cmp |> set(TRANSFORM_FIELD, transform)
    cmp |> set(WORLD_FIELD, transform)
}

//! Same, from the three values a creation site usually has at hand.
def public ApplyEntityTransform(var cmp : ComponentMap; position : float3; rotation : float4; scale : float3) {
    ApplyEntityTransform(cmp, Transform(position = position, rotation = rotation, scale = scale))
}

// One entry of the per-frame work list, indexed by entity id. `world` starts as the authored
// local transform and is replaced by the composed one during the walk.
struct private TransformNode {
    eid         : EntityId
    parent      : EntityId
    first_child : EntityId
    next        : EntityId
    stamp       : int
    // Pass stamp of the walk visit: a link cycle would otherwise revisit a node forever.
    walked      : int
    world       : Transform = Transform()
}

var private g_nodes : array<TransformNode>
// Bumped per pass: it marks the entries the current gather scan wrote, so a leftover from the
// previous pass in the same slot is never mistaken for a live node after a heap restart or a
// deleted entity.
var private g_stamp = 0

def private IsNode(eid : EntityId) : bool {
    // A dead or reused slot fails on either the stamp or the generation.
    let index = int(eid.id)
    return index >= 0 && index < length(g_nodes) && g_nodes[index].stamp == g_stamp && g_nodes[index].eid == eid
}

//! Composes `world` for one work-list node and its descendants.
def private ComposeNode(index : int; parent : Transform; has_parent : bool; depth : int) {
    if (depth > MAX_HIERARCHY_DEPTH) {
        return
    }
    g_nodes[index].walked = g_stamp
    if (has_parent) {
        g_nodes[index].world = ComposeWorld(parent, g_nodes[index].world)
    }
    let world = g_nodes[index].world
    var child = g_nodes[index].first_child
    while (child != INVALID_ENTITY_ID) {
        // A deleted or re-created child ends the chain: its slot no longer holds the links of
        // that entity. A child already walked this pass is a link cycle, not a tree.
        if (!IsNode(child)) {
            break
        }
        let child_index = int(child.id)
        if (g_nodes[child_index].walked == g_stamp) {
            break
        }
        let next = g_nodes[child_index].next
        ComposeNode(child_index, world, true, depth + 1)
        child = next
    }
}

//! Recomputes `_world` for every entity with a local Transform. Runs once per frame after
//! game/editor logic and before rendering.
[export, profile]
def UpdateWorldTransforms() {
    let slots = length(decsState.entityLookup)
    if (slots == 0) {
        return
    }
    if (length(g_nodes) < slots) {
        resize(g_nodes, slots)
    }
    g_stamp++

    // `relationship` is optional here: an entity without links is a root of its own and still
    // needs `_world`, and a query default is the cheapest way to say "links if present".
    query() $(eid : EntityId; transform : Transform; relationship : Relationship = Relationship()) {
        let index = int(eid.id)
        if (index >= 0 && index < slots) {
            g_nodes[index] = TransformNode(
                eid = eid,
                parent = relationship.parent,
                first_child = relationship.first_child,
                next = relationship.next,
                stamp = g_stamp,
                walked = 0,
                world = transform
            )
        }
    }

    // Roots: a node with no parent, or whose parent is gone or has no transform. A group entity
    // that carries links without a transform is not in the list at all, so its children start
    // a hierarchy of their own. Every other node is reached from its own root, so each tree is
    // walked exactly once.
    for (index in range(slots)) {
        if (g_nodes[index].stamp != g_stamp) {
            continue
        }
        let parent = g_nodes[index].parent
        if (parent != INVALID_ENTITY_ID && IsNode(parent)) {
            continue
        }
        ComposeNode(index, Transform(), false, 0)
    }

    // The composed values go straight back into archetype storage, one scan, no per-entity
    // `update_entity`.
    query() $(eid : EntityId; var _world : Transform&) {
        let index = int(eid.id)
        if (index >= 0 && index < slots && g_nodes[index].stamp == g_stamp && g_nodes[index].eid == eid) {
            _world = g_nodes[index].world
        }
    }
}
