options gen2
options relaxed_assign = false

//! Bundle format: the authored `assets` table becomes a ready name -> id map at parse time,
//! and the same table feeds `Content::Dependencies` so the loader requests exactly what the
//! screen names. Covers the guid/path reference forms, the duplicate-name and unknown-kind
//! rejections, and the version gate. No files, no GPU: the document is built as text.

require strings
require tryengine/Bundle
require tryengine/Content

var g_failures : int

def Contains(ids : array<uint64>; id : uint64) : bool {
    for (value in ids) {
        if (value == id) {
            return true
        }
    }
    return false
}

def Check(ok : bool; what : string) {
    if (!ok) {
        g_failures++
        print("FAIL: {what}\n")
    }
}

let kLogoGuid : uint64 = 14429000468236573201ul
let kFontGuid : uint64 = 7732110565556845333ul

def BuildText(assets : string; version : string = "2") : string {
    var text = "version: u32 = " + version + "\n"
    text += "name: string = \"hud\"\n"
    text += "prefabs: string[] = [\n  \"content/probe.prefab\"\n]\n"
    text += "assets: BundleAsset[] = [\n{assets}]\n"
    return text
}

def Asset(name : string; kind : string; ref : string) : string {
    return "  \{\n    name: string = \"{name}\"\n    kind: string = \"{kind}\"\n    ref: string = \"{ref}\"\n  \}\n"
}

[export]
def main() : int {
    let valid = BuildText(Asset("logo", "texture", "guid:14429000468236573201") +
                          Asset("font", "font", "guid:7732110565556845333"))

    var document = Content::ParseFor(DocumentKind.Bundle, "data/bundles/probe.bundle", valid)
    Check(document != null, "the bundle text parses")
    if (document != null) {
        Check(Contains(document.deps.textures, kLogoGuid), "the texture id is a dependency")
        Check(Contains(document.deps.fonts, kFontGuid), "the font id is a dependency")

        var compiled = Bundle::Parse(document)
        Check(compiled != null, "the bundle compiles")
        if (compiled != null) {
            Check((compiled.by_name?["logo"] ?? 0ul) == kLogoGuid, "the texture key resolves")
            Check((compiled.by_name?["font"] ?? 0ul) == kFontGuid, "the font key resolves")
            Check((compiled.by_name?["missing"] ?? 0ul) == 0ul, "an unknown key resolves to 0")
            Check(length(compiled.prefabs) == 1, "the prefab list survives the compile")
        }
    }

    var duplicate = Content::ParseFor(DocumentKind.Bundle, "data/bundles/probe.bundle",
                                      BuildText(Asset("logo", "texture", "guid:14429000468236573201") +
                                                Asset("logo", "font", "guid:7732110565556845333")))
    Check(duplicate != null && Bundle::Parse(duplicate) == null, "a duplicate asset name is rejected")

    var unknown_kind = Content::ParseFor(DocumentKind.Bundle, "data/bundles/probe.bundle",
                                         BuildText(Asset("icon", "sprite", "guid:14429000468236573201")))
    Check(unknown_kind != null && Bundle::Parse(unknown_kind) == null, "an unknown asset kind is rejected")

    var bad_ref = Content::ParseFor(DocumentKind.Bundle, "data/bundles/probe.bundle",
                                    BuildText(Asset("icon", "texture", "guid:not-a-number")))
    Check(bad_ref != null && Bundle::Parse(bad_ref) == null, "an unresolvable reference is rejected")

    var old_version = Content::ParseFor(DocumentKind.Bundle, "data/bundles/probe.bundle",
                                        BuildText(Asset("icon", "texture", "guid:14429000468236573201"), "1"))
    Check(old_version != null && Bundle::Parse(old_version) == null, "the old format version is rejected")

    print("-- {g_failures} check(s) failed --\n")
    return g_failures == 0 ? 0 : 1
}
