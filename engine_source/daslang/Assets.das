options gen2
options relaxed_assign = false

module Assets shared public

// Unified content reading. The script always asks with an authored path; the engine
// turns it into PathId(path) and finds either a loose file (editor) or a pak chunk
// (game). No script reads the disk directly, so the storage can be swapped later
// without touching Prefab/Scene/Game.

require daslib/array_boost
require daslib/async_boost
require daslib/linq
require tryResources

let public STARTUP_SCENE_PATH = "tryengine/startup"
let public CONTENT_MANIFEST_PATH = "tryengine/content_manifest"

// Binary assets one content document needs before it can be shown: meshes and materials
// are the heavy, asynchronous part of loading. Scene and prefab loaders fill this from
// the authored document; the loading state machine requests the ids and waits for them.
struct public AssetRefs {
    meshes : array<uint64>
    materials : array<uint64>
    parsed : bool
}

def public AddMeshRef(var refs : AssetRefs; id : uint64) {
    if (id != 0ul && !refs.meshes.contains(id)) {
        refs.meshes |> push(id)
    }
}

def public AddMaterialRef(var refs : AssetRefs; id : uint64) {
    if (id != 0ul && !refs.materials.contains(id)) {
        refs.materials |> push(id)
    }
}

def public Exists(path : string) : bool {
    return ResolveAssetPath(path) != 0ul
}

def public IdOf(path : string) : uint64 {
    return ResolveAssetPath(path)
}

// Drops a cached text so the next read comes from the current source. Reads in this
// module are always fresh; Save paths call this explicitly after writing.
def public Invalidate(path : string) {
    let id = ResolveAssetPath(path)
    if (id != 0ul) {
        InvalidateAssetText(id)
    }
}

// Editor-only: makes a loose file known to the registry when it came from a file dialog
// (an absolute path, or a file created after the last tree rebuild). The game never
// calls this: its paths are already in the pak.
def public RegisterPath(path : string) : uint64 {
    return RegisterAssetPath(path)
}

// Full text of one asset. Returns "" when the path is unknown or the chunk failed:
// callers treat that as a missing/invalid file, never as an empty document.
[async]
def public ReadText(path : string) : string {
    let id = ResolveAssetPath(path)
    if (id == 0ul) {
        yield ""
    }

    // Content is authored: a file may have been rewritten since the last read, and the
    // pak chunk is cheap enough to re-read. Never serve a stale document to a loader.
    InvalidateAssetText(id)
    RequestLoadAssetText(id)
    while (!AssetTextIsReady(id)) {
        if (AssetTextIsFailed(id)) {
            InvalidateAssetText(id)
            yield ""
        }
        await_next_frame()
    }

    let text = GetAssetText(id)
    ReleaseAssetText(id)
    yield text
}

// Synchronous text read. The game calls it after its C++ preload phase, so the text
// comes from the ResourceManager cache. The editor has no preload phase; there the
// same call falls back to a synchronous loose-file read, which keeps editor Play on
// the game's synchronous entry path.
def public ReadTextCached(path : string) : string {
    let id = ResolveAssetPath(path)
    if (id == 0ul) {
        return ""
    }
    return ReadAssetTextSync(id)
}

// Newline-separated list of content paths. In the editor it is assembled from the
// scanned trees, in the game it comes from the pak manifest.
def public ContentManifest() : string {
    return GetContentManifestText()
}
