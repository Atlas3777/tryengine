options gen2
options relaxed_assign = false

//! Prefab format validation: exactly one root stored first, no root parent id, unique local
//! ids, no entity ids and no runtime components. A file with the old `entities` array or an
//! old version is rejected too — the format has no compatibility path.

require daslib/decs
require strings
require tryengine/Content
require tryengine/Prefab
require tryengine/Types

var g_failures : int

def Check(ok : bool; what : string) {
    if (!ok) {
        g_failures++
        print("FAIL: {what}\n")
    }
}

def BaseText() : string {
    var text = "version: u32 = 7\n"
    text += "name: string = \"validation\"\n"
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
    text += "]\n"
    return text
}

// Every case must parse as ConfigFormat; the prefab compiler is what rejects it.
def Rejected(text : string; what : string) {
    var document = Content::ParseFor(DocumentKind.Prefab, "content/validation.prefab", text)
    Check(document != null, "{what}: the document parses")
    if (document == null) {
        return
    }
    Check(Prefab::CompilePrefab(document) == null, "{what}: the prefab compiler rejects it")
}

[export]
def main() : int {
    let base = BaseText()
    var document = Content::ParseFor(DocumentKind.Prefab, "content/validation.prefab", base)
    Check(document != null, "the valid prefab parses")
    var compiled = Prefab::CompilePrefab(document)
    Check(compiled != null, "the valid prefab compiles")
    if (compiled != null) {
        Check(length(compiled.nodes) == 2, "the valid prefab keeps both nodes")
        Check(compiled.nodes[0].parent == -1, "the valid root has no parent")
        Check(compiled.nodes[1].parent == 0, "the valid child points at the root")
    }

    Rejected(
        replace(base, "    localId: i32 = 0\n", "    localId: i32 = 0\n    parentLocalId: u32 = 4294967295\n"),
        "an explicit root parent id"
    )
    Rejected(replace(base, "    localId: i32 = 1\n", "    localId: i32 = 0\n"), "a duplicate localId")
    Rejected(replace(base, "    parentLocalId: i32 = 0\n", ""), "two roots")
    Rejected(replace(base, "version: u32 = 7", "version: u32 = 6"), "an unsupported version")
    Rejected(replace(base, "    localId: i32 = 0\n", "    localId: u32 = 4294967295\n"), "the reserved localId")
    Rejected(
        replace(base, "    tag: string = \"child\"\n",
                "    tag: string = \"child\"\n    eid: EntityId = \{ id: u32 = 0, generation: i32 = 0 \}\n"),
        "an entity id"
    )
    Rejected(
        replace(base, "    tag: string = \"child\"\n",
                "    tag: string = \"child\"\n    _world: Transform = \{ position: f32x3 = [0 0 0] \}\n"),
        "a runtime component"
    )

    var old_format = "version: u32 = 7\n"
    old_format += "name: string = \"old\"\n"
    old_format += "entities: PrefabEntity[] = [\n"
    old_format += "  \{ localId: i32 = 0 \}\n"
    old_format += "]\n"
    Rejected(old_format, "the old entities array")

    print("-- {g_failures} check(s) failed --\n")
    return g_failures == 0 ? 0 : 1
}
