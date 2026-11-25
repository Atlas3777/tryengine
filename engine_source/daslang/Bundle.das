options gen2
options relaxed_assign = false

module Bundle shared public

//! Asset bundle: the **load manifest** of one screen, stored as ConfigFormat text in
//! `game/data`. Only the loader sees this object: it owns the authored paths, the parent
//! link, the prefab list and the async cache state.
//!
//! A screen never receives a `Bundle`; after the request is ready the loader flattens the
//! hierarchy into an `AssetMap` (flat `key -> (kind, id)`), and that is what game/UI code
//! gets. The split keeps loading concerns (`ref`, `parent`, `prefabs`) out of the runtime
//! alias table, even though both are derived from the same `assets` table.
//!
//! Assets are addressed by `guid:<u64>` (an imported texture or font) or by an authored path
//! (an authoritative `.mat`). The guid lives here, once per asset; screen code names a key.

require daslib/linq
require strings
require tryengine/AssetCache
require tryengine/AssetMap
require tryengine/ConfigFormat
require tryengine/ConfigFormatBoost
require tryengine/Content

let public BUNDLE_VERSION : uint = 3u

//! One authored alias, already resolved to a runtime id.
struct public BundleAsset {
    name : string
    kind : string
    id : uint64
}

//! Parsed manifest: what to load and how it is named. The parent is a path, not a pointer:
//! `Flatten` walks the chain through the cache when the maps are built, so no record holds a
//! reference to another and a reload cannot leave a dangling link.
struct public BundleData {
    path : string
    name : string
    //! Authored `data/...` path of the parent bundle, `""` for a root.
    parent_path : string
    prefabs : array<string>
    entries : array<BundleAsset>
}

var private g_values : table<string; BundleData?>

def private IsBundleAssetKind(kind : string) : bool {
    return kind == "texture" || kind == "font" || kind == "material" || kind == "mesh"
}

//! Parses a bundle document into its ready manifest. Every prefab entry is an authored
//! `content/...` path with an explicit extension; every asset entry is a unique key in the
//! bundle plus a resolvable reference. The parent is recorded, not resolved: the loader
//! validates the hierarchy after all bundles of the request are ready.
def public Parse(var document : Content::Document?) : BundleData? {
    if (document == null || document.root == null || !(document.root.value is _object)) {
        print("[BUNDLE ERROR] document root is not an object\n")
        return null
    }

    assume root_obj = document.root.value as _object
    let version = from_CF(root_obj?["version"] ?? null, type<uint>)
    if (version != BUNDLE_VERSION) {
        print("[BUNDLE ERROR] '{document.path}': unsupported version {version}, expected {BUNDLE_VERSION}\n")
        return null
    }

    var data <- BundleData()
    data.path = document.path
    data.name = from_CF(root_obj?["name"] ?? null, type<string>)
    if (empty(data.name)) {
        data.name = Content::Stem(document.path)
    }

    data.parent_path = from_CF(root_obj?["parent"] ?? null, type<string>)
    if (!empty(data.parent_path) && !starts_with(data.parent_path, "data/")) {
        print("[BUNDLE ERROR] '{document.path}': parent '{data.parent_path}' must be a data/... path\n")
        return null
    }

    let prefabs_cf = root_obj?["prefabs"] ?? null
    if (prefabs_cf != null) {
        if (!(prefabs_cf.value is _array)) {
            print("[BUNDLE ERROR] '{document.path}': 'prefabs' must be an array\n")
            return null
        }

        assume prefabs_arr = prefabs_cf.value as _array
        for (prefab_cf in prefabs_arr) {
            if (prefab_cf == null || !(prefab_cf.value is _string)) {
                print("[BUNDLE ERROR] '{document.path}': every prefabs entry must be a string\n")
                return null
            }
            let prefab = prefab_cf.value as _string
            if (!empty(prefab) && !data.prefabs.contains(prefab)) {
                data.prefabs |> push(prefab)
            }
        }
    }

    let assets_cf = root_obj?["assets"] ?? null
    if (assets_cf != null) {
        if (!(assets_cf.value is _array)) {
            print("[BUNDLE ERROR] '{document.path}': 'assets' must be an array\n")
            return null
        }

        var seen : table<string; int>
        assume assets_arr = assets_cf.value as _array
        for (asset_cf in assets_arr) {
            if (asset_cf == null || !(asset_cf.value is _object)) {
                print("[BUNDLE ERROR] '{document.path}': every assets entry must be an object\n")
                return null
            }
            assume asset_obj = asset_cf.value as _object

            let name = from_CF(asset_obj?["name"] ?? null, type<string>)
            let kind = from_CF(asset_obj?["kind"] ?? null, type<string>)
            let ref = from_CF(asset_obj?["ref"] ?? null, type<string>)

            if (empty(name)) {
                print("[BUNDLE ERROR] '{document.path}': every assets entry needs a non-empty 'name'\n")
                return null
            }
            if (!IsBundleAssetKind(kind)) {
                print("[BUNDLE ERROR] '{document.path}': asset '{name}' has unsupported kind '{kind}' (texture, font, material, mesh)\n")
                return null
            }
            if (key_exists(seen, name)) {
                print("[BUNDLE ERROR] '{document.path}': asset name '{name}' is declared twice\n")
                return null
            }
            seen[name] = 1

            let id = Content::ResolveRef(ref)
            if (id == 0ul) {
                print("[BUNDLE ERROR] '{document.path}': asset '{name}' ref '{ref}' is not a known asset\n")
                return null
            }
            data.entries |> push(BundleAsset(name = name, kind = kind, id = id))
        }
    }

    return new BundleData(
        path = data.path,
        name = data.name,
        parent_path = data.parent_path,
        prefabs <- data.prefabs,
        entries <- data.entries
    )
}

//! Parent bundle path of a ready manifest, empty for a root. The loader queues it before the
//! request reaches the resource phase.
def public Parents(path : string) : array<string> {
    var out : array<string>
    if (!key_exists(g_values, path)) {
        return <- out
    }
    let data = g_values[path]
    if (data != null && !empty(data.parent_path)) {
        out |> push(data.parent_path)
    }
    return <- out
}

//! Walks the parent chain and checks that every parent is loaded and no cycle exists. Called by
//! the loader after every bundle of the request is ready, before maps are built.
def public Validate(path : string) : bool {
    var cursor_path = path
    var depth = 0
    var seen : table<string; int>
    while (!empty(cursor_path)) {
        if (key_exists(seen, cursor_path)) {
            print("[BUNDLE ERROR] '{path}': parent chain contains a cycle through '{cursor_path}'\n")
            return false
        }
        seen[cursor_path] = 1
        if (!key_exists(g_values, cursor_path)) {
            print("[BUNDLE ERROR] '{path}': parent '{cursor_path}' is not loaded\n")
            return false
        }
        let data = g_values[cursor_path]
        if (data == null) {
            print("[BUNDLE ERROR] '{path}': bundle '{cursor_path}' has no manifest\n")
            return false
        }
        if (empty(data.parent_path)) {
            return true
        }
        cursor_path = Content::ResolvePath(DocumentKind.Bundle, data.parent_path)
        depth++
        if (depth > 64) {
            print("[BUNDLE ERROR] '{path}': parent chain is deeper than 64\n")
            return false
        }
    }
    return false
}

//! Flattens the hierarchy into the runtime alias table: parent first, child overrides. The
//! table is registered in `AssetMap` and the screen gets its handle; lookups never walk the
//! cache again.
def public Flatten(path : string) : AssetMap {
    var aliases : table<string; AssetEntry>
    var chain : array<string>
    var cursor_path = path
    var depth = 0
    while (!empty(cursor_path) && key_exists(g_values, cursor_path)) {
        chain |> push(cursor_path)
        let data = g_values[cursor_path]
        if (data == null || empty(data.parent_path)) {
            break
        }
        cursor_path = Content::ResolvePath(DocumentKind.Bundle, data.parent_path)
        depth++
        if (depth > 64) {
            break
        }
    }

    // chain[0] is the leaf, the last element is the root: fill root first so the leaf wins.
    var i = length(chain) - 1
    while (i >= 0) {
        if (key_exists(g_values, chain[i])) {
            let data = g_values[chain[i]]
            if (data != null) {
                for (entry in data.entries) {
                    aliases[entry.name] = AssetEntry(kind = entry.kind, id = entry.id)
                }
            }
        }
        i--
    }
    return AssetMap::Register(aliases)
}

// ---------------------------------------------------------------------------
// AssetCache kind
// ---------------------------------------------------------------------------

//! Starts loading a bundle document. False means the storage layer does not know the path.
def public Request(path : string) : bool {
    return AssetCache::Request("bundle", path)
}

//! One step of the bundle state machine: the loader calls it once per ready path per frame.
def public Advance(path : string) : AssetCache::AssetState {
    return AssetCache::Advance(g_values, path, Content::ItemsField(DocumentKind.Bundle),
                               @@Bundle::Parse)
}

//! Prefab paths a ready manifest asks for, in authored `content/...` form. The loader expands
//! a bundle through this, so the prefab phase is fed by the bundles of the same request.
def public PrefabPaths(path : string) : array<string> {
    var out : array<string>
    if (!key_exists(g_values, path)) {
        return <- out
    }
    let data = g_values[path]
    if (data == null) {
        return <- out
    }
    for (prefab in data.prefabs) {
        out |> push(prefab)
    }
    return <- out
}

//! Abandons a bundle that is still loading; a ready manifest stays cached.
def public Cancel(path : string) {
    AssetCache::Cancel(g_values, path)
}

//! Drops every cached manifest, ready or failed.
def public Clear() {
    AssetCache::Clear(g_values, "bundle")
}
