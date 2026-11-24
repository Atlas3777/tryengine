options gen2
options relaxed_assign = false

require daslib/decs
require daslib/decs_boost
require math
require tryengine/EcsMacro
require tryengine/TransformSystem
require tryengine/Types

//! World-transform pass: hierarchy composition, a moved parent, a gizmo-style partial
//! refresh, a deleted parent, a group entity without a transform, a singleton without links
//! and a link cycle.

var private g_failures = 0

def private Near(a : float3; b : float3) : bool {
    return length(a - b) < 0.0001f
}

def private Check(what : string; actual : float3; expected : float3) {
    if (!Near(actual, expected)) {
        print("FAIL {what}: {actual} != {expected}\n")
        g_failures++
        return
    }
    print("ok   {what}\n")
}

def private Add(position : float3; rotation : float4; scale : float3) : EntityId {
    var eid : EntityId
    eid = create_entity() @(self, cmp) {
        ApplyEntityTransform(cmp, position, rotation, scale)
        cmp |> set(RELATIONSHIP_FIELD, Relationship())
    }
    commit()
    return eid
}

def private GetRelationship(eid : EntityId) : Relationship {
    var value = Relationship()
    query(eid) $(relationship : Relationship) {
        value = relationship
    }
    return value
}

def private SetRelationship(eid : EntityId; value : Relationship) {
    update_entity(eid) @(self, cmp) {
        cmp |> set(RELATIONSHIP_FIELD, value)
    }
}

// Links `children` as the sibling chain of `parent`, keeping every other field intact.
def private LinkChildren(parent : EntityId; children : array<EntityId>) {
    for (i in range(length(children))) {
        var value = GetRelationship(children[i])
        value.parent = parent
        value.prev = i > 0 ? children[i - 1] : INVALID_ENTITY_ID
        value.next = i + 1 < length(children) ? children[i + 1] : INVALID_ENTITY_ID
        SetRelationship(children[i], value)
    }
    var parent_value = GetRelationship(parent)
    parent_value.first_child = length(children) > 0 ? children[0] : INVALID_ENTITY_ID
    SetRelationship(parent, parent_value)
    commit()
}

def private World(eid : EntityId) : float3 {
    var position = float3()
    query(eid) $(_world : Transform) {
        position = _world.position
    }
    return position
}

def private SetLocal(eid : EntityId; value : Transform) {
    update_entity(eid) @(self, cmp) {
        cmp |> set(TRANSFORM_FIELD, value)
    }
    commit()
}

def private NoRotation() : float4 {
    return float4(0.0f, 0.0f, 0.0f, 1.0f)
}

[export]
def main() : int {
    decs::restart()
    EcsApplyRegistry()

    let root = Add(float3(10.0f, 0.0f, 0.0f), NoRotation(), float3(1.0f, 1.0f, 1.0f))
    let child = Add(float3(0.0f, 0.0f, 2.0f), NoRotation(), float3(1.0f, 1.0f, 1.0f))
    let grandchild = Add(float3(0.0f, 0.0f, 1.0f), NoRotation(), float3(1.0f, 1.0f, 1.0f))
    let sibling = Add(float3(0.0f, 0.0f, -2.0f), NoRotation(), float3(1.0f, 1.0f, 1.0f))
    LinkChildren(root, [child, sibling])
    LinkChildren(child, [grandchild])
    UpdateWorldTransforms()

    Check("root", World(root), float3(10.0f, 0.0f, 0.0f))
    Check("child", World(child), float3(10.0f, 0.0f, 2.0f))
    Check("grandchild", World(grandchild), float3(10.0f, 0.0f, 3.0f))
    Check("sibling", World(sibling), float3(10.0f, 0.0f, -2.0f))

    // Moving the root moves the whole tree in the same pass.
    SetLocal(root, Transform(position = float3(0.0f, 5.0f, 0.0f), rotation = NoRotation(), scale = float3(1.0f, 1.0f, 1.0f)))
    UpdateWorldTransforms()
    Check("moved root", World(root), float3(0.0f, 5.0f, 0.0f))
    Check("moved child", World(child), float3(0.0f, 5.0f, 2.0f))
    Check("moved grandchild", World(grandchild), float3(0.0f, 5.0f, 3.0f))

    // Parent scale applies to the child's local offset.
    SetLocal(root, Transform(position = float3(0.0f, 0.0f, 0.0f), rotation = NoRotation(), scale = float3(2.0f, 2.0f, 2.0f)))
    UpdateWorldTransforms()
    Check("scaled child", World(child), float3(0.0f, 0.0f, 4.0f))
    Check("scaled grandchild", World(grandchild), float3(0.0f, 0.0f, 6.0f))

    // A parent rotated by 90 degrees around Y sends the child's +Z offset to +X.
    SetLocal(root, Transform(
        position = float3(0.0f, 0.0f, 0.0f),
        rotation = normalize(float4(0.0f, 1.0f, 0.0f, 1.0f)),
        scale = float3(1.0f, 1.0f, 1.0f)
    ))
    UpdateWorldTransforms()
    Check("rotated child", World(child), float3(2.0f, 0.0f, 0.0f))

    // The gizmo path recomposes in the same frame it edits, before the scene is drawn.
    SetLocal(root, Transform(position = float3(1.0f, 0.0f, 0.0f), rotation = NoRotation(), scale = float3(1.0f, 1.0f, 1.0f)))
    UpdateWorldTransforms()
    SetLocal(child, Transform(position = float3(0.0f, 0.0f, 5.0f), rotation = NoRotation(), scale = float3(1.0f, 1.0f, 1.0f)))
    UpdateWorldTransforms()
    Check("recomposed child", World(child), float3(1.0f, 0.0f, 5.0f))
    Check("recomposed grandchild", World(grandchild), float3(1.0f, 0.0f, 6.0f))
    Check("ancestor", World(root), float3(1.0f, 0.0f, 0.0f))

    // A deleted parent turns its child into a root.
    delete_entity(child)
    commit()
    UpdateWorldTransforms()
    Check("orphan grandchild", World(grandchild), float3(0.0f, 0.0f, 1.0f))

    // A group entity can carry links without a transform; its child starts a hierarchy.
    var group : EntityId
    group = create_entity() @(self, cmp) {
        cmp |> set(RELATIONSHIP_FIELD, Relationship())
    }
    commit()
    let under_group = Add(float3(0.0f, 7.0f, 0.0f), NoRotation(), float3(1.0f, 1.0f, 1.0f))
    LinkChildren(group, [under_group])
    UpdateWorldTransforms()
    Check("child of transformless group", World(under_group), float3(0.0f, 7.0f, 0.0f))

    // A singleton without a relationship follows its own transform.
    var singleton : EntityId
    singleton = create_entity() @(self, cmp) {
        ApplyEntityTransform(cmp, float3(3.0f, 0.0f, 0.0f), NoRotation(), float3(1.0f, 1.0f, 1.0f))
    }
    commit()
    UpdateWorldTransforms()
    Check("singleton", World(singleton), float3(3.0f, 0.0f, 0.0f))
    SetLocal(singleton, Transform(position = float3(4.0f, 0.0f, 0.0f), rotation = NoRotation(), scale = float3(1.0f, 1.0f, 1.0f)))
    UpdateWorldTransforms()
    Check("moved singleton", World(singleton), float3(4.0f, 0.0f, 0.0f))

    // A link cycle must not hang or recurse away: the walk breaks on a node it already walked.
    var cycle_root : EntityId
    cycle_root = create_entity() @(self, cmp) {
        ApplyEntityTransform(cmp, float3(0.0f, 0.0f, 0.0f), NoRotation(), float3(1.0f, 1.0f, 1.0f))
        cmp |> set(RELATIONSHIP_FIELD, Relationship())
    }
    commit()
    let cycle_a = Add(float3(0.0f, 0.0f, 1.0f), NoRotation(), float3(1.0f, 1.0f, 1.0f))
    LinkChildren(cycle_root, [cycle_a])
    var cycle_rel = GetRelationship(cycle_a)
    cycle_rel.next = cycle_a
    SetRelationship(cycle_a, cycle_rel)
    commit()
    UpdateWorldTransforms()
    Check("cycle child", World(cycle_a), float3(0.0f, 0.0f, 1.0f))

    print("-- {g_failures} check(s) failed --\n")
    return g_failures == 0 ? 0 : 1
}
