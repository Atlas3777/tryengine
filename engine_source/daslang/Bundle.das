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

// Same manifest matching rules as Prefab::FindInContent: exact path, content key, then a
// unique file stem. An ambiguous short name resolves to "".
def public FindInContent(name_or_path : string) : string {
    if (empty(name_or_path)) {
        return ""
    }

    var direct = replace(name_or_path, "\\", "/")
    while (starts_with(direct, "./")) {
        direct = slice(direct, 2, length(direct))
    }
    let marker = "/game/data/"
    let marker_index = rfind(direct, marker)
    if (marker_index >= 0) {
        direct = "game/data/{slice(direct, marker_index + length(marker), length(direct))}"
    }

    let manifest = Assets::ContentManifest()
    var direct_match = ""
    var key_match = ""
    var stem_match = ""
    var stem_count = 0

    for (path in split(manifest, "\n")) {
        if (empty(path) || !ends_with(path, ".bundle")) {
            continue
        }
        if (path == direct) {
            direct_match = path
        }
        let key = BundleKey(path)
        if (key == name_or_path) {
            key_match = path
        }
        if (BundleStem(key) == name_or_path) {
            stem_count++
            stem_match = path
        }
    }

    if (!empty(direct_match)) {
        return direct_match
    }
    if (!empty(key_match)) {
        return key_match
    }
    if (stem_count == 1) {
        return stem_match
    }
    if (stem_count > 1) {
        print("[BUNDLE WARNING] name '{name_or_path}' is ambiguous; use a content path\n")
    }
    return ""
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

def private BundleKey(path : string) : string {
    var key = path
    let prefix = "game/data/"
    if (starts_with(key, prefix)) {
        key = slice(key, length(prefix), length(key))
    }
    let suffix = ".bundle"
    if (ends_with(key, suffix)) {
        key = slice(key, 0, length(key) - length(suffix))
    }
    return key
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
