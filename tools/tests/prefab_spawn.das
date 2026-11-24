options gen2
options relaxed_assign = false

//! Prefab compilation and spawn: the authored flat node list becomes a template whose nodes
//! carry resolved components and a baked world transform, so spawn creates the whole hierarchy
//! in one call and never runs the transform pass. Covers the derived relationship links, the
//! spawn-origin shift, moving the root, and saving the subtree back to the text format.

require daslib/decs
require daslib/decs_boost
require strings
require tryengine/Content
require tryengine/Prefab
require tryengine/TransformSystem
require tryengine/Types

var g_failures : int

def Check(ok : bool; what : string) {
    if (!ok) {
        g_failures++
        print("FAIL: {what}\n")
    }
}

def BuildText() : string {
    var text = "version: u32 = 7\n"
    text += "name: string = \"probe\"\n"
    text += "nodes: PrefabNode[] = [\n"
    text += "  \{\n"
    text += "    transform: Transform = \{\n"
    text += "      position: f32x3 = [1 2 3]\n"
    text += "      rotation: f32x4 = [0 0 0 1]\n"
    text += "      scale: f32x3 = [1 1 1]\n"
    text += "    \}\n"
    text += "    tag: string = \"root\"\n"
    text += "    localId: i32 = 0\n"
    text += "  \}\n"
    text += "  \{\n"
    text += "    transform: Transform = \{\n"
    text += "      position: f32x3 = [0 0 5]\n"
    text += "      rotation: f32x4 = [0 0 0 1]\n"
    text += "      scale: f32x3 = [1 1 1]\n"
    text += "    \}\n"
    text += "    tag: string = \"child\"\n"
    text += "    localId: i32 = 1\n"
    text += "    parentLocalId: i32 = 0\n"
    text += "  \}\n"
    text += "  \{\n"
    text += "    transform: Transform = \{\n"
    text += "      position: f32x3 = [0 0 2]\n"
    text += "      rotation: f32x4 = [0 0 0 1]\n"
    text += "      scale: f32x3 = [1 1 1]\n"
    text += "    \}\n"
    text += "    tag: string = \"grandchild\"\n"
    text += "    localId: i32 = 2\n"
    text += "    parentLocalId: i32 = 1\n"
    text += "  \}\n"
    text += "]\n"
    return text
}

def Local(eid : EntityId) : Transform => decs::get_component(eid, TRANSFORM_FIELD, Transform())
def World(eid : EntityId) : Transform => decs::get_component(eid, WORLD_FIELD, Transform())
def Rel(eid : EntityId) : Relationship => decs::get_component(eid, RELATIONSHIP_FIELD, Relationship())
def Tag(eid : EntityId) : string => decs::get_component(eid, "tag", "")

[export]
def main() : int {
    var document = Content::ParseFor(DocumentKind.Prefab, "content/probe.prefab", BuildText())
    Check(document != null, "the prefab text parses")
    if (document == null) {
        print("-- {g_failures} check(s) failed --\n")
        return 1
    }

    var compiled = Prefab::CompilePrefab(document)
    Check(compiled != null, "the prefab compiles")
    if (compiled == null) {
        print("-- {g_failures} check(s) failed --\n")
        return 1
    }

    Check(length(compiled.nodes) == 3, "the template keeps every authored node")
    Check(length(compiled.groups) == 1, "nodes with the same components share one spawn group")
    Check(compiled.nodes[0].first_child == 1, "the root's first child is node 1")
    Check(compiled.nodes[1].first_child == 2, "the child's first child is node 2")
    Check(compiled.nodes[1].parent == 0, "the child's parent is node 0")
    Check(compiled.nodes[2].parent == 1, "the grandchild's parent is node 1")
    Check(compiled.nodes[0].world.position == float3(1.0f, 2.0f, 3.0f), "the root world is baked")
    Check(compiled.nodes[2].world.position == float3(1.0f, 2.0f, 10.0f), "the grandchild world is baked")

    let origin = float3(10.0f, 0.0f, 0.0f)
    let root = Prefab::SpawnPrefabTemplate(compiled, origin)
    Check(root != INVALID_ENTITY_ID, "spawn returns a root entity")
    if (root == INVALID_ENTITY_ID) {
        print("-- {g_failures} check(s) failed --\n")
        return 1
    }

    let child = Rel(root).first_child
    let grandchild = Rel(child).first_child
    Check(Rel(root).parent == INVALID_ENTITY_ID, "the root has no parent")
    Check(Rel(root).prev == INVALID_ENTITY_ID, "the root has no previous sibling")
    Check(child != INVALID_ENTITY_ID, "the root has a child")
    Check(grandchild != INVALID_ENTITY_ID, "the child has a grandchild")
    Check(Rel(child).parent == root, "the child points at the root")
    Check(Rel(grandchild).parent == child, "the grandchild points at the child")
    Check(Rel(child).next == INVALID_ENTITY_ID, "the child has no next sibling")
    Check(Tag(root) == "root" && Tag(child) == "child" && Tag(grandchild) == "grandchild", "tags survive the spawn")

    Check(Local(root).position == origin + float3(1.0f, 2.0f, 3.0f), "the root keeps its authored position, shifted by the origin")
    Check(Local(child).position == float3(0.0f, 0.0f, 5.0f), "the child keeps its local position")
    Check(World(root).position == origin + float3(1.0f, 2.0f, 3.0f), "the root world includes the origin")
    Check(World(child).position == origin + float3(1.0f, 2.0f, 8.0f), "the child world includes its parent")
    Check(World(grandchild).position == origin + float3(1.0f, 2.0f, 10.0f), "the grandchild world includes both parents")

    // The template already carries the composed world, so the pass after spawn is a no-op.
    UpdateWorldTransforms()
    Check(World(child).position == origin + float3(1.0f, 2.0f, 8.0f), "the transform pass does not move a spawned child")
    Check(World(grandchild).position == origin + float3(1.0f, 2.0f, 10.0f), "the transform pass does not move a spawned grandchild")

    // Moving the root moves the whole subtree on the next pass.
    update_entity(root) @(eid : EntityId; var cmp : ComponentMap) {
        cmp |> set(TRANSFORM_FIELD, Transform(position = origin + float3(1.0f, 2.0f, 4.0f)))
    }
    commit()
    UpdateWorldTransforms()
    Check(World(child).position == origin + float3(1.0f, 2.0f, 9.0f), "moving the root moves the child")
    Check(World(grandchild).position == origin + float3(1.0f, 2.0f, 11.0f), "moving the root moves the grandchild")

    // Saving puts the root back at the origin and writes only authored components.
    let saved = Prefab::BuildPrefabTextFromEntity(root, "saved")
    Check(!empty(saved), "the subtree saves to prefab text")
    Check(find(saved, RELATIONSHIP_FIELD) < 0, "the saved text drops the runtime relationship")
    Check(find(saved, WORLD_FIELD) < 0, "the saved text drops the derived world transform")
    Check(find(saved, "eid:") < 0, "the saved text drops entity ids")
    Check(find(saved, "parentLocalId") >= 0, "the saved text keeps the child's parent")

    var saved_document = Content::ParseFor(DocumentKind.Prefab, "content/saved.prefab", saved)
    var saved_template = Prefab::CompilePrefab(saved_document)
    Check(saved_template != null, "the saved text compiles again")
    if (saved_template != null) {
        Check(length(saved_template.nodes) == 3, "the saved text keeps all three nodes")
        Check(saved_template.nodes[0].local.position == float3(0.0f, 0.0f, 0.0f), "the saved root is rebased to the origin")
        Check(saved_template.nodes[0].parent == -1, "the saved root stores no parent")
        Check(saved_template.nodes[1].parent == 0, "the saved child keeps its parent")
    }

    let respawned = saved_template != null ? Prefab::SpawnPrefabTemplate(saved_template, float3(0.0f)) : INVALID_ENTITY_ID
    Check(respawned != INVALID_ENTITY_ID, "the saved prefab spawns again")

    print("-- {g_failures} check(s) failed --\n")
    return g_failures == 0 ? 0 : 1
}
