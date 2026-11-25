options gen2
options relaxed_assign = false

//! Scene compilation: the authored document becomes a ready-to-create entity set, exactly like
//! a prefab. Covers a parent stored after its child (a scene names a parent by file index),
//! the baked world transform, a hierarchy cycle that must not hang, and the root-parent error.
//! Applying the set must produce the same hierarchy through `relationship`, without the
//! transform pass.

require daslib/decs
require daslib/decs_boost
require strings
require tryengine/Content
require tryengine/EntitySet
require tryengine/Model
require tryengine/Scene
require tryengine/TransformSystem
require tryengine/Types

var g_failures : int

def Check(ok : bool; what : string) {
    if (!ok) {
        g_failures++
        print("FAIL: {what}\n")
    }
}

def Node(tag : string; pos : float3) : string {
    var text = "  \{\n"
    text += "    transform: Transform = \{\n"
    text += "      position: f32x3 = [{pos.x} {pos.y} {pos.z}]\n"
    text += "      rotation: f32x4 = [0 0 0 1]\n"
    text += "      scale: f32x3 = [1 1 1]\n"
    text += "    \}\n"
    text += "    tag: string = \"{tag}\"\n"
    return text
}

// A child stored before its parent: the hierarchy is a file index, so entity 0 names entity 1.
def BuildText() : string {
    var text = "entities: Entity[] = [\n"
    text += Node("child", float3(0.0f, 0.0f, 5.0f))
    text += "    parentLocalId: u32 = 1\n"
    text += "  \}\n"
    text += Node("root", float3(10.0f, 0.0f, 0.0f))
    text += "  \}\n"
    text += Node("sibling", float3(0.0f, 3.0f, 0.0f))
    text += "  \}\n"
    text += "]\n"
    return text
}

def BuildCycleText() : string {
    var text = "entities: Entity[] = [\n"
    text += Node("a", float3(0.0f, 0.0f, 1.0f))
    text += "    parentLocalId: u32 = 1\n"
    text += "  \}\n"
    text += Node("b", float3(0.0f, 0.0f, 2.0f))
    text += "    parentLocalId: u32 = 0\n"
    text += "  \}\n"
    text += "]\n"
    return text
}

def BuildRootParentText() : string {
    var text = "entities: Entity[] = [\n"
    text += Node("root", float3(0.0f, 0.0f, 0.0f))
    text += "    parentLocalId: u32 = 4294967295\n"
    text += "  \}\n"
    text += "]\n"
    return text
}

def Local(eid : EntityId) : Transform => decs::get_component(eid, TRANSFORM_FIELD, Transform())
def World(eid : EntityId) : Transform => decs::get_component(eid, WORLD_FIELD, Transform())
def Rel(eid : EntityId) : Relationship => decs::get_component(eid, RELATIONSHIP_FIELD, Relationship())

[export]
def main() : int {
    var document = Content::ParseFor(DocumentKind.Scene, "content/probe.scene", BuildText())
    Check(document != null, "the scene text parses")
    if (document == null) {
        print("-- {g_failures} check(s) failed --\n")
        return 1
    }

    var compiled = Scene::CompileScene(document)
    Check(compiled != null, "the scene compiles")
    if (compiled == null) {
        print("-- {g_failures} check(s) failed --\n")
        return 1
    }

    Check(length(compiled.nodes) == 3, "the set keeps every authored entity")
    Check(length(compiled.groups) == 1, "entities with the same components share one group")
    Check(compiled.nodes[0].parent == 1, "a parent stored after its child is still resolved")
    Check(compiled.nodes[1].parent == -1, "the parent is a root")
    Check(compiled.nodes[1].first_child == 0, "the parent's first child is the entity stored first")
    Check(compiled.nodes[2].parent == -1, "an entity without parentLocalId is a root")

    Check(compiled.nodes[1].world.position == float3(10.0f, 0.0f, 0.0f), "the root world is baked")
    Check(compiled.nodes[0].world.position == float3(10.0f, 0.0f, 5.0f), "the child world is composed from a parent stored later")
    Check(compiled.nodes[2].world.position == float3(0.0f, 3.0f, 0.0f), "the second root keeps its own world")

    var refs : array<Model::ModelRefs>
    Check(EntitySet::ResolveModels(compiled, refs), "a scene without models resolves")
    var ids : array<EntityId>
    let root = EntitySet::CreateEntities(compiled, float3(0.0f), refs, ids)
    Check(root != INVALID_ENTITY_ID, "creating the set returns an entity")
    if (root != INVALID_ENTITY_ID) {
        Check(Rel(ids[0]).parent == ids[1], "the child points at its parent in DECS")
        Check(Rel(ids[1]).first_child == ids[0], "the parent points at its child in DECS")
        Check(Rel(ids[2]).parent == INVALID_ENTITY_ID, "the sibling stays a root")
        Check(Local(ids[0]).position == float3(0.0f, 0.0f, 5.0f), "the child keeps its local position")
        Check(World(ids[0]).position == float3(10.0f, 0.0f, 5.0f), "the created child is placed in the same call")

        // The set carries the composed world, so the pass after creation must not move anything.
        UpdateWorldTransforms()
        Check(World(ids[0]).position == float3(10.0f, 0.0f, 5.0f), "the transform pass does not move the child")

        let parent_world = World(ids[1]).position
        Check(parent_world == float3(10.0f, 0.0f, 0.0f), "the transform pass does not move the parent")
    }

    // A cycle has no root, so the transform pass never reaches its nodes; compilation must not
    // hang and must leave each node's local transform as its world.
    var cycle_document = Content::ParseFor(DocumentKind.Scene, "content/cycle.scene", BuildCycleText())
    var cycle = Scene::CompileScene(cycle_document)
    Check(cycle != null, "a hierarchy cycle still compiles")
    if (cycle != null) {
        Check(length(cycle.nodes) == 2, "the cycle keeps both entities")
        Check(cycle.nodes[0].world.position == float3(0.0f, 0.0f, 1.0f), "a node in a cycle keeps its local world")
        Check(cycle.nodes[1].world.position == float3(0.0f, 0.0f, 2.0f), "the other node in a cycle keeps its local world")
    }

    // A root stores no parent: spelling the root parent id out is an error, not a second way.
    var bad_document = Content::ParseFor(DocumentKind.Scene, "content/bad.scene", BuildRootParentText())
    Check(Scene::CompileScene(bad_document) == null, "a root that stores parentLocalId is rejected")

    var empty_document = Content::ParseFor(DocumentKind.Scene, "content/empty.scene", "version: u32 = 1\n")
    Check(Scene::CompileScene(empty_document) == null, "a document without entities is rejected")

    print("-- {g_failures} check(s) failed --\n")
    return g_failures == 0 ? 0 : 1
}
