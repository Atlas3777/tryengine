options gen2
options relaxed_assign = false

module Bundle shared public

//! Asset bundle: a developer-authored list of prefabs stored as ConfigFormat text in
//! `game/data`. The build puts every data file into the common pak, so a bundle is
//! available before the first `Loader::Invoke`.
//!
//! A bundle does not load anything itself. `Loader::AddBundle` resolves the path, reads
//! the text asynchronously and expands the prefab list into the same prefab preload step
//! that `AddPrefab` uses.

require strings
require math
require daslib/linq
require daslib/strings_boost
require tryengine/Assets
require tryengine/ConfigFormat
require tryengine/ConfigFormatBoost

let public BUNDLE_VERSION : uint = 1u

struct public BundleData {
    path : string
    name : string
    prefabs : array<string>
}

// Authored path -> canonical content path. A bare path is relative to `data/`
// (`bundles/startup.bundle`), a `data/...` path is relative to the game directory, and
// `game/...` or absolute paths are kept. The extension is always explicit.
def private BundlePathOf(name_or_path : string) : string {
    return Assets::ResolveGamePath(name_or_path, "data")
}

//! Resolves an authored bundle path without reading the bundle. A bare path is relative
//! to `data/`, a `data/...` path is relative to the game directory; the extension is
//! required.
def public FindInContent(name_or_path : string) : string {
    let path = BundlePathOf(name_or_path)
    if (empty(path) || !Assets::Exists(path)) {
        return ""
    }
    return path
}

def private BundleStem(path : string) : string {
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

//! Parses a bundle document. `path` is kept for diagnostics and for the cache key.
def public Parse(path : string; text : string) : BundleData? {
    var error : string
    var root = read_config(text, error)
    if (length(error) > 0 || root == null || !(root.value is _object)) {
        print("[BUNDLE ERROR] '{path}' is not a valid bundle document: {error}\n")
        return null
    }

    assume root_obj = root.value as _object
    let version = from_CF(root_obj?["version"] ?? null, type<uint>)
    if (version != BUNDLE_VERSION) {
        print("[BUNDLE ERROR] '{path}': unsupported version {version}, expected {BUNDLE_VERSION}\n")
        return null
    }

    var data <- BundleData()
    data.path = path
    data.name = from_CF(root_obj?["name"] ?? null, type<string>)
    if (empty(data.name)) {
        data.name = BundleStem(path)
    }

    let prefabs_cf = root_obj?["prefabs"] ?? null
    if (prefabs_cf == null || !(prefabs_cf.value is _array)) {
        print("[BUNDLE ERROR] '{path}': 'prefabs' must be an array\n")
        return null
    }

    assume prefabs_arr = prefabs_cf.value as _array
    for (prefab_cf in prefabs_arr) {
        if (prefab_cf == null || !(prefab_cf.value is _string)) {
            print("[BUNDLE ERROR] '{path}': every prefabs entry must be a string\n")
            return null
        }
        let prefab = prefab_cf.value as _string
        if (!empty(prefab) && !data.prefabs.contains(prefab)) {
            data.prefabs |> push(prefab)
        }
    }

    return new BundleData(path = data.path, name = data.name, prefabs <- data.prefabs)
}
