options gen2
options relaxed_assign = false

module AssetKind shared public

// Asset reference is declared on the field, not on a wrapper type:
//
//   [EcsComponent(name = "model"), DecsSerializable]
//   struct ModelRender {
//       mesh     : uint64 = 0ul
//       @asset = "data:mat"
//       material : string
//   }
//
// The value is `kind:ext`. `kind` is the storage domain (engine/game/content/data), not a
// subfolder. `ext` filters the whole domain tree, so scene/prefab pickers see every
// `.scene`/`.prefab` anywhere under content. The leading dot is optional: `data:mat` and
// `data:.mat` mean the same. The annotation is inert metadata: the editor reads it from the
// [DecsSerializable] field schema and draws the picker for a plain string field. No editor
// types are required here.

require daslib/rtti
require strings

//! Parsed `@asset` annotation of one field. `has` is false when the field is not an asset
//! reference; a non-empty `error` means the annotation is present but malformed.
struct AssetFieldInfo {
    has   : bool
    kind  : string
    ext   : string
    error : string
}

def public IsAssetKind(kind : string) : bool {
    return kind == "engine" || kind == "game" || kind == "content" || kind == "data"
}

//! Finds and validates the `@asset` annotation in an apply-collected annotation list.
def public ParseAssetField(annotations : array<tuple<name : string; data : RttiValue>>) : AssetFieldInfo {
    var info : AssetFieldInfo
    for (ann in annotations) {
        if (ann.name != "asset") {
            continue
        }
        info.has = true
        if (!(ann.data is tString)) {
            info.error = "@asset must be a \"kind:ext\" string"
            return info
        }

        let spec = ann.data as tString
        let sep = find(spec, ":")
        if (sep <= 0) {
            info.error = "@asset must be \"kind:ext\", e.g. \"data:mat\""
            return info
        }

        info.kind = slice(spec, 0, sep)
        var ext = slice(spec, sep + 1, length(spec))
        if (empty(ext)) {
            info.error = "@asset must be \"kind:ext\", e.g. \"data:mat\""
            return info
        }
        if (!starts_with(ext, ".")) {
            ext = "." + ext
        }
        info.ext = ext

        if (!IsAssetKind(info.kind)) {
            info.error = "@asset kind must be engine, game, content or data"
            return info
        }
        return info
    }
    return info
}
