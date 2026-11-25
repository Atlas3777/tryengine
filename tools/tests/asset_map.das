options gen2
options relaxed_assign = false

//! Runtime alias map: a flattened table is registered once and the screen reads it through a
//! small copyable handle. Covers typed getters, the kind check, the empty handle and count.
//! No files, no GPU.

require tryengine/AssetMap

var g_failures : int

def Check(ok : bool; what : string) {
    if (!ok) {
        g_failures++
        print("FAIL: {what}\n")
    }
}

[export]
def main() : int {
    var aliases : table<string; AssetEntry>
    aliases["logo"] = AssetEntry(kind = "texture", id = 42ul)
    aliases["font/body"] = AssetEntry(kind = "font", id = 77ul)

    let map = AssetMap::Register(aliases)
    Check(map.Texture("logo") == 42ul, "a texture key resolves")
    Check(map.Font("font/body") == 77ul, "a font key resolves")
    Check(map.Texture("font/body") == 0ul, "a key of another kind is not a texture")
    Check(map.Material("logo") == 0ul, "a missing material key is 0")
    Check(map.Has("logo"), "Has sees an existing key")
    Check(!map.Has("missing"), "Has rejects a missing key")
    Check(map.KindOf("font/body") == "font", "KindOf reports the stored kind")
    Check(map.KindOf("missing") == "", "KindOf of a missing key is empty")
    Check(map.Count() == 2, "Count reports the number of aliases")

    let empty = AssetMap()
    Check(empty.Texture("logo") == 0ul, "the empty handle has no texture")
    Check(!empty.Has("logo"), "the empty handle has no keys")
    Check(empty.Count() == 0, "the empty handle is empty")

    print("-- {g_failures} check(s) failed --\n")
    return g_failures == 0 ? 0 : 1
}
