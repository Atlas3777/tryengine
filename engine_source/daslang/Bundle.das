options gen2
options relaxed_assign = false

module Bundle shared public

//! Asset bundle: a developer-authored list of prefabs stored as ConfigFormat text in
//! `game/data`. The build puts every data file into the common pak, so a bundle is
//! available before the first `Loader::Load`.
//!
//! A bundle does not load anything itself. `LoaderRequest::AddBundle` resolves the path,
//! reads the text asynchronously and expands the prefab list into the same prefab preload
//! step that `AddPrefab` uses. Paths, parsing and dependencies are shared through
//! `tryengine/Content`; this module owns only the `.bundle` document shape.

require daslib/linq
require tryengine/AssetCache
require tryengine/ConfigFormat
require tryengine/ConfigFormatBoost
require tryengine/Content

let public BUNDLE_VERSION : uint = 1u

struct public BundleData {
    path : string
    name : string
    prefabs : array<string>
}

//! Parses a parsed bundle document. `document.path` is kept for diagnostics and for the
//! cache key. Every prefab entry is an authored `content/...` path with an explicit
//! extension; the loader resolves it through `Content::Find` when it queues the prefab.
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
    if (prefabs_cf == null || !(prefabs_cf.value is _array)) {
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

    return new BundleData(path = data.path, name = data.name, prefabs <- data.prefabs)
}

// ---------------------------------------------------------------------------
// AssetCache kind
// ---------------------------------------------------------------------------
// A bundle is a plan fragment, not a runtime value: its ready form is the prefab list that
// the loader expands. It is still cached the same way as every other kind, so a hit costs
// no read and no parse.

var private g_values : table<string; BundleData?>

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
