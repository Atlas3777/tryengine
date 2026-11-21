options gen2
options relaxed_assign = false

module Content shared public

//! Authored content documents above the storage layer.
//!
//! `tryengine/Assets` answers where the bytes are (a loose file in the editor, a pak chunk
//! in the game). `Content` answers what a document is: which root its authored path belongs
//! to, its file stem, whether it is registered, how its ConfigFormat text is parsed, and
//! which meshes and materials it depends on. It does not keep parsed documents:
//! `tryengine/AssetCache` owns the ready (or almost ready) values, one typed record per path.
//!
//! Scene, Prefab and Bundle keep their own format rules and runtime actions. Everything
//! they share lives here, so a path is resolved, a document is parsed and a dependency set
//! is collected in exactly one place.
//!
//! Paths are authored: `content/...` and `data/...` are relative to the game directory, a
//! bare path is relative to the document kind's root, and `game/...` or absolute paths are
//! kept. Extensions are always explicit.

require strings
require math
require daslib/linq
require daslib/strings_boost
require tryengine/Assets
require tryengine/ConfigFormat
require tryengine/ConfigFormatBoost
require tryengine/Types
require tryResources

enum public DocumentKind {
    Scene
    Prefab
    Bundle
}

let public CONTENT_ROOT = "content"
let public DATA_ROOT = "data"

//! Authored root of a kind: scenes and prefabs live under `content/`, bundles under `data/`.
def public RootOf(kind : DocumentKind) : string {
    if (kind == DocumentKind.Bundle) {
        return DATA_ROOT
    }
    return CONTENT_ROOT
}

//! Authored path -> canonical registry path. A bare path is relative to the kind's root;
//! the extension is never guessed or appended.
def public ResolvePath(kind : DocumentKind; path : string) : string {
    return Assets::ResolveGamePath(path, RootOf(kind))
}

//! File name without directory and extension.
def public Stem(path : string) : string {
    var name = path
    let slash = max(rfind(name, "/"), rfind(name, "\\"))
    if (slash >= 0) {
        name = slice(name, slash + 1, length(name))
    }
    let dot = rfind(name, ".")
    if (dot > 0) {
        name = slice(name, 0, dot)
    }
    return name
}

//! Canonical path of a registered document, or "" when the storage layer does not know it.
def public Find(kind : DocumentKind; path : string) : string {
    let resolved = ResolvePath(kind, path)
    if (empty(resolved) || !Assets::Exists(resolved)) {
        return ""
    }
    return resolved
}

//! Human name of a kind, used in diagnostics and in a loading screen.
def public KindName(kind : DocumentKind) : string {
    if (kind == DocumentKind.Scene) {
        return "scene"
    } elif (kind == DocumentKind.Prefab) {
        return "prefab"
    }
    return "bundle"
}

// Resource ids one content document needs before it can be shown: meshes and materials
// are the heavy, asynchronous part of loading. The collector below fills this from the
// authored document; the loading state machine requests the ids and waits for them.
struct public Dependencies {
    meshes    : array<uint64>
    materials : array<uint64>
}

def public AddMeshDependency(var deps : Dependencies; id : uint64) {
    if (id != 0ul && !deps.meshes.contains(id)) {
        deps.meshes |> push(id)
    }
}

def public AddMaterialDependency(var deps : Dependencies; id : uint64) {
    if (id != 0ul && !deps.materials.contains(id)) {
        deps.materials |> push(id)
    }
}

def private ParseUInt64Array(cf : StructField?) : array<uint64> {
    var res : array<uint64>
    if (cf == null || !(cf.value is _array)) {
        return <- res
    }

    assume arr = cf.value as _array
    for (elem in arr) {
        if (elem == null) {
            continue
        }
        if (elem.value is _uint64) {
            res |> push(elem.value as _uint64)
        } elif (elem.value is _int64) {
            res |> push(uint64(elem.value as _int64))
        } elif (elem.value is _uint) {
            res |> push(uint64(elem.value as _uint))
        } elif (elem.value is _int) {
            res |> push(uint64(elem.value as _int))
        }
    }
    return <- res
}

// requireMaterials is an array of authored references: a project path or "guid:<id>".
// The loader resolves them to ids; there is no numeric fallback in the format.
def private ParseAssetRefArray(cf : StructField?) : array<uint64> {
    var res : array<uint64>
    if (cf == null || !(cf.value is _array)) {
        return <- res
    }

    assume arr = cf.value as _array
    for (elem in arr) {
        if (elem == null || !(elem.value is _string)) {
            continue
        }
        let id = ResolveAssetRef(elem.value as _string)
        if (id != 0ul && !res.contains(id)) {
            res |> push(id)
        }
    }
    return <- res
}

// Dependencies live on the item's components. `materialRef` is the only material form.
def private CollectObjectDependencies(var deps : Dependencies; obj : table<string; StructField?> const) {
    AddMeshDependency(deps, from_CF(obj?["meshGuid"] ?? null, type<uint64>))

    let material_cf = obj?["materialRef"] ?? null
    if (material_cf != null && material_cf.value is _object) {
        let material = from_CF(material_cf, type<MaterialRef>)
        if (!empty(material.path)) {
            AddMaterialDependency(deps, ResolveAssetRef(material.path))
        }
    }
}

// Authored array of items differs by kind: a scene lists entities, a prefab document
// lists nodes, a bundle has no item array. A plain config asset passes "".
def public ItemsField(kind : DocumentKind) : string {
    if (kind == DocumentKind.Scene) {
        return "entities"
    }
    if (kind == DocumentKind.Prefab) {
        return "nodes"
    }
    return ""
}

//! Parsed dependency set of one document. Header arrays are authoritative, but a scene
//! saved by an older editor may list deps only on its entities, so both are read.
//! Prefabs have no header arrays and contribute their node deps only.
def public CollectDependencies(items_field : string; root : StructField?) : Dependencies {
    var deps : Dependencies
    if (root == null || !(root.value is _object)) {
        return <- deps
    }

    assume root_obj = root.value as _object

    for (id in ParseUInt64Array(root_obj?["requireMeshs"] ?? null)) {
        AddMeshDependency(deps, id)
    }
    for (id in ParseAssetRefArray(root_obj?["requireMaterials"] ?? null)) {
        AddMaterialDependency(deps, id)
    }

    if (!empty(items_field)) {
        let items_cf = root_obj?[items_field] ?? null
        if (items_cf != null && items_cf.value is _array) {
            assume items_arr = items_cf.value as _array
            for (item_cf in items_arr) {
                if (item_cf == null || !(item_cf.value is _object)) {
                    continue
                }
                assume item_obj = item_cf.value as _object
                CollectObjectDependencies(deps, item_obj)
            }
        }
    }

    return <- deps
}

//! One parsed document. `root` owns the ConfigFormat tree the typed modules read;
//! `deps` is the dependency set collected while parsing. The source text is not kept:
//! the tree is all the scene/prefab/bundle code needs after parsing.
//!
//! `Content` does not cache documents: `tryengine/AssetCache` does, one typed record per
//! path. This module only knows how a document is shaped and parsed.
struct public Document {
    path : string
    root : StructField?
    deps : Dependencies
}

//! Parses a document and collects its dependencies from `items_field` (see `ItemsField`).
//! Returns null when the text is not a ConfigFormat object; the typed module decides
//! whether the object is a valid scene, prefab, bundle or plain asset.
def public Parse(path : string; text : string; items_field : string) : Document? {
    var error : string
    var root = read_config(text, error)
    if (!empty(error) || root == null || !(root.value is _object)) {
        print("[CONTENT ERROR] '{path}' is not a valid document: {error}\n")
        return null
    }

    return new Document(
        path = path,
        root = root,
        deps <- CollectDependencies(items_field, root)
    )
}

//! Same parse for one of the authored kinds; the kind only selects the item array whose
//! components contribute dependencies.
def public ParseFor(kind : DocumentKind; path : string; text : string) : Document? {
    return Parse(path, text, ItemsField(kind))
}
