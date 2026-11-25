options gen2
options relaxed_assign = false

module Bundle shared public

//! Asset bundle: the authored manifest of one screen, stored as ConfigFormat text in
//! `game/data`. A bundle has two consumers of the same ready object:
//!
//!  * the loader (`LoaderRequest::AddBundle`) reads `prefabs` from it and requests the
//!    resources of its `assets` table; the ids come from `Content::Dependencies`, collected
//!    while the document is parsed;
//!  * the screen (`Bundle::Get`) uses the same table as a name -> id lookup.
//!
//! The ready value is the compiled table: the ConfigFormat document is dropped with the parse,
//! exactly like a prefab becomes a `PrefabTemplate`. A bundle never stores bytes and never
//! loads anything itself; loading is a request for the bundle *path*, and the path is the only
//! thing the loader needs.
//!
//! Assets are addressed by `guid:<u64>` (an imported texture or font) or by an authored path
//! (an authoritative `.mat`). The guid lives here, once per asset, so screen code only ever
//! names a key.

require daslib/linq
require tryengine/AssetCache
require tryengine/ConfigFormat
require tryengine/ConfigFormatBoost
require tryengine/Content

let public BUNDLE_VERSION : uint = 2u

//! Compiled bundle: what the screen needs, already resolved to runtime ids.
struct public BundleData {
    path : string
    name : string
    prefabs : array<string>
    //! Screen-local key -> runtime id. See `Bundle::Get`.
    by_name : table<string; uint64>
}

var private g_values : table<string; BundleData?>

def private IsBundleAssetKind(kind : string) : bool {
    return kind == "texture" || kind == "font" || kind == "material" || kind == "mesh"
}

//! Parses a bundle document into its ready form. `document.path` is kept for diagnostics and
//! for the cache key. Every prefab entry is an authored `content/...` path with an explicit
//! extension; every asset entry is a unique key in the bundle plus a resolvable reference.
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
            if (key_exists(data.by_name, name)) {
                print("[BUNDLE ERROR] '{document.path}': asset name '{name}' is declared twice\n")
                return null
            }

            let id = Content::ResolveRef(ref)
            if (id == 0ul) {
                print("[BUNDLE ERROR] '{document.path}': asset '{name}' ref '{ref}' is not a known asset\n")
                return null
            }
            data.by_name[name] = id
        }
    }

    return new BundleData(path = data.path, name = data.name, prefabs <- data.prefabs, by_name <- data.by_name)
}

//! True when the bundle record is ready. Sync and cache-only.
def public Ready(bundle_path : string) : bool {
    let resolved = Content::ResolvePath(DocumentKind.Bundle, bundle_path)
    return AssetCache::State(resolved) == AssetCache::AssetState.Ready
}

//! Runtime lookup of a screen asset: the key the bundle declares -> the id the loader
//! preloaded. Sync and cache-only; `0` means the bundle is not ready or the key is not in its
//! table. A screen must be shown only after its bundle request finished, so zero here is a
//! bug (a typo in the key or a bundle that is not in the request), not a "still loading" state.
def public Get(bundle_path : string; name : string) : uint64 {
    let resolved = Content::ResolvePath(DocumentKind.Bundle, bundle_path)
    if (AssetCache::State(resolved) != AssetCache::AssetState.Ready) {
        return 0ul
    }
    let data = g_values[resolved]
    if (data == null || !key_exists(data.by_name, name)) {
        return 0ul
    }
    return data.by_name?[name] ?? 0ul
}

//! True when the ready bundle declares `name`.
def public Has(bundle_path : string; name : string) : bool {
    return Get(bundle_path, name) != 0ul
}

// ---------------------------------------------------------------------------
// AssetCache kind
// ---------------------------------------------------------------------------
// A bundle is a plan fragment, not a runtime value: its ready form is the asset table plus
// the prefab list. It is still cached the same way as every other kind, so a hit costs no
// read and no parse.

def private RequestAsset(path : string) : bool {
    return AssetCache::Request("bundle", path)
}

def private AdvanceAsset(path : string) : AssetCache::AssetState {
    return AssetCache::Advance(g_values, path, Content::ItemsField(DocumentKind.Bundle),
                               @@Bundle::Parse)
}

def private AssetDeps(path : string) : Content::Dependencies {
    return <- AssetCache::Dependencies(path)
}

//! The loader expands a ready bundle into prefab jobs through this.
def private ExpandAsset(path : string) : array<string> {
    var out : array<string>
    if (AssetCache::State(path) != AssetCache::AssetState.Ready) {
        return <- out
    }
    let data = g_values[path]
    for (prefab in data.prefabs) {
        out |> push(prefab)
    }
    return <- out
}

def private CancelAsset(path : string) {
    AssetCache::Cancel(g_values, path)
}

def private ClearAssets() {
    AssetCache::Clear(g_values, "bundle")
}

[init]
def private RegisterAssetKind() {
    AssetCache::RegisterKind("bundle", AssetCache::AssetKindOps(
        request = @@RequestAsset,
        advance = @@AdvanceAsset,
        deps = @@AssetDeps,
        expands = @@ExpandAsset,
        cancel = @@CancelAsset,
        clear = @@ClearAssets
    ))
}
