options gen2
options relaxed_assign = false

module Loader shared public

//! Asynchronous content loader.
//!
//! The caller builds a `LoaderRequest` (bundles, scenes, prefabs), starts
//! `Loader::Load(request, @@OnLoaded)` as a task and reads `GetProgress(phase)` while it
//! runs. The loader reads document texts through `tryengine/Content`, expands bundles,
//! compiles prefabs, collects mesh/material dependencies and waits for the resources.
//! It never applies a scene and never switches what the player sees: scene entry is
//! `Scene::Load` and belongs to the callback.
//!
//! `Load` owns the request lifetime: it invokes the callback once on `Ready` and then
//! releases its references. On failure the phase stays `Failed`, `Error()` is available
//! and the callback is not called.

require daslib/async_boost
require strings
require tryengine/Assets
require tryengine/Bundle
require tryengine/Content
require tryengine/Prefab
require tryengine/Profiling
require tryResources

enum public LoadPhase {
    Idle
    Bundles
    Scenes
    Prefabs
    Assets
    Ready
    Failed
}

//! Called once when the request reaches `Ready`. Failure is reported through `IsFailed` /
//! `Error`; the callback is not called for a failed request.
typedef public LoaderCallback = function<() : void>

// Builder. Array fields are filled in place; the request is consumed by `Loader::Load`.
// All paths are relative to the game directory: scenes and prefabs use `content/...`,
// bundles use `data/...`. Extensions are explicit; `game/` is implied.
struct public LoaderRequest {
    prefabs : array<string>
    bundles : array<string>
    scenes : array<string>

    def static AddPrefab(var self : LoaderRequest; name : string) {
        self.prefabs |> push(name)
    }

    def static AddBundle(var self : LoaderRequest; name : string) {
        self.bundles |> push(name)
    }

    def static AddScene(var self : LoaderRequest; path : string) {
        self.scenes |> push(path)
    }
}

enum private LoaderAssetKind {
    Mesh
    Material
}

// One requested document. `kind` selects both the parse rule and the phase it belongs to.
struct private LoaderDocument {
    kind : ContentKind
    path : string
    id : uint64
    held : bool
    done : bool
}

struct private LoaderAsset {
    kind : LoaderAssetKind
    id : uint64
    held : bool
    done : bool
}

var private g_active = false
var private g_phase = LoadPhase.Idle
// Incremented whenever a request starts or is released. A running `Load` task carries
// its own value, so an aborted request cannot advance the next one.
var private g_generation : uint64 = 0ul
var private g_documents : array<LoaderDocument>
var private g_assets : array<LoaderAsset>
var private g_error = ""
var private g_on_loaded : LoaderCallback = @@LoaderNoop
var private g_notified = false

def private LoaderNoop() {
}

// Loader request paths are relative to the game directory: scenes and prefabs use
// `content/...`, bundles use `data/...`. `game/` is implied, and the extension is always
// explicit. The loader needs the leading domain because it reads both folders.
def private IsContentPath(path : string) : bool {
    return starts_with(path, "content/")
}

def private IsDataPath(path : string) : bool {
    return starts_with(path, "data/")
}

//! Items done and total for one phase. `Loader::GetProgress(LoadPhase.Prefabs)` returns
//! e.g. `float2(10, 23)` while 23 prefabs were requested and 10 are ready. A phase that
//! has not started yet returns `float2(0, total)` as soon as its items are known; a phase
//! with no items, and `Idle`/`Failed`, returns `float2(0, 0)`.
def public GetProgress(phase : LoadPhase) : float2 {
    var done = 0
    var total = 0

    if (phase == LoadPhase.Bundles || phase == LoadPhase.Scenes || phase == LoadPhase.Prefabs) {
        var kind = ContentKind.Bundle
        if (phase == LoadPhase.Scenes) {
            kind = ContentKind.Scene
        } elif (phase == LoadPhase.Prefabs) {
            kind = ContentKind.Prefab
        }
        for (document in g_documents) {
            if (document.kind != kind) {
                continue
            }
            total++
            if (document.done) {
                done++
            }
        }
    } elif (phase == LoadPhase.Assets) {
        total = length(g_assets)
        for (asset in g_assets) {
            if (asset.done) {
                done++
            }
        }
    }

    return float2(float(done), float(total))
}

def public PhaseName(phase : LoadPhase) : string {
    if (phase == LoadPhase.Bundles) {
        return "Bundles"
    }
    if (phase == LoadPhase.Scenes) {
        return "Scenes"
    }
    if (phase == LoadPhase.Prefabs) {
        return "Prefabs"
    }
    if (phase == LoadPhase.Assets) {
        return "Assets"
    }
    if (phase == LoadPhase.Ready) {
        return "Ready"
    }
    if (phase == LoadPhase.Failed) {
        return "Failed"
    }
    return "Idle"
}

def private AddDocument(kind : ContentKind; path : string) : bool {
    let resolved = Content::Find(kind, path)
    if (empty(resolved)) {
        return false
    }

    let id = Assets::IdOf(resolved)
    if (id == 0ul) {
        return false
    }

    for (document in g_documents) {
        if (document.id == id) {
            return true
        }
    }

    InvalidateAssetText(id)
    RequestLoadAssetText(id)
    g_documents |> push(LoaderDocument(kind = kind, path = resolved, id = id, held = true))
    return true
}

def private ReleaseDocument(var document : LoaderDocument) {
    if (document.held) {
        ReleaseAssetText(document.id)
        document.held = false
    }
}

def private ReleaseAll() {
    for (index in range(length(g_documents))) {
        ReleaseDocument(g_documents[index])
    }
    for (index in range(length(g_assets))) {
        if (!g_assets[index].held) {
            continue
        }
        if (g_assets[index].kind == LoaderAssetKind.Mesh) {
            ReleaseMeshResource(g_assets[index].id)
        } else {
            ReleaseMaterialResource(g_assets[index].id)
        }
        g_assets[index].held = false
    }
}

def private ResetState() {
    g_active = false
    g_phase = LoadPhase.Idle
    g_generation++
    g_error = ""
    g_on_loaded = @@LoaderNoop
    g_notified = false
    clear(g_documents)
    clear(g_assets)
}

def private AddAsset(kind : LoaderAssetKind; id : uint64) {
    if (id == 0ul) {
        return
    }
    for (asset in g_assets) {
        if (asset.kind == kind && asset.id == id) {
            return
        }
    }
    g_assets |> push(LoaderAsset(kind = kind, id = id))
}

def private AddRefs(refs : AssetRefs) {
    for (mesh in refs.meshes) {
        AddAsset(LoaderAssetKind.Mesh, mesh)
    }
    for (material in refs.materials) {
        AddAsset(LoaderAssetKind.Material, material)
    }
}

def private RequestAssets() {
    for (index in range(length(g_assets))) {
        if (g_assets[index].held) {
            continue
        }
        if (g_assets[index].kind == LoaderAssetKind.Mesh) {
            RequestLoadMesh(g_assets[index].id)
        } else {
            RequestLoadMaterial(g_assets[index].id)
        }
        g_assets[index].held = true
    }
}

def private HasKind(kind : ContentKind) : bool {
    for (document in g_documents) {
        if (document.kind == kind) {
            return true
        }
    }
    return false
}

def private NextPhase(phase : LoadPhase) : LoadPhase {
    if (phase == LoadPhase.Idle) {
        if (HasKind(ContentKind.Bundle)) {
            return LoadPhase.Bundles
        }
    }
    if (phase == LoadPhase.Idle || phase == LoadPhase.Bundles) {
        if (HasKind(ContentKind.Scene)) {
            return LoadPhase.Scenes
        }
    }
    if (phase == LoadPhase.Idle || phase == LoadPhase.Bundles || phase == LoadPhase.Scenes) {
        if (HasKind(ContentKind.Prefab)) {
            return LoadPhase.Prefabs
        }
    }
    return LoadPhase.Assets
}

def private AllDocumentsDone(kind : ContentKind) : bool {
    for (document in g_documents) {
        if (document.kind == kind && !document.done) {
            return false
        }
    }
    return true
}

// Expands a parsed bundle into prefab jobs, compiles a prefab, and adds scene/prefab
// dependencies to the asset phase. Returns false after `Fail` was called.
def private HandleDocument(var document : Content::Document?) : bool {
    if (document == null) {
        return false
    }

    if (document.kind == ContentKind.Bundle) {
        let bundle = Bundle::Parse(document)
        if (bundle == null) {
            Fail("'{document.path}' is not a valid bundle")
            return false
        }
        print("[LOADER] bundle '{bundle.name}': {length(bundle.prefabs)} prefab(s)\n")
        for (prefab_path in bundle.prefabs) {
            if (!IsContentPath(prefab_path)) {
                Fail("bundle '{document.path}': prefab '{prefab_path}' must be a content/... path")
                return false
            }
            if (!AddDocument(ContentKind.Prefab, prefab_path)) {
                Fail("bundle '{document.path}': prefab '{prefab_path}' was not found; check the content/ path")
                return false
            }
        }
    } elif (document.kind == ContentKind.Prefab) {
        if (!Prefab::CachePrefabDocument(document)) {
            Fail("'{document.path}' is not a valid prefab")
            return false
        }
        AddRefs(document.refs)
    } else {
        AddRefs(document.refs)
    }

    return true
}

// Polls the texts of one phase, parses each ready document and runs its kind-specific
// step. Documents of another kind are ignored, so the three document phases share one loop.
def private ProcessDocuments(kind : ContentKind) {
    let count = length(g_documents)
    for (index in range(count)) {
        let document = g_documents[index]
        if (document.kind != kind || document.done) {
            continue
        }
        if (AssetTextIsFailed(document.id)) {
            Fail("'{document.path}' failed to read")
            return
        }
        if (!AssetTextIsReady(document.id)) {
            continue
        }

        let text = GetAssetText(document.id)
        var parsed = Content::Cache(kind, document.path, text)
        ReleaseDocument(g_documents[index])
        g_documents[index].done = true
        if (parsed == null) {
            Fail("'{document.path}' is not a valid {Content::KindName(kind)} document")
            return
        }
        if (!HandleDocument(parsed)) {
            return
        }
    }
}

def private NotifyReady() {
    if (g_notified) {
        return
    }
    g_notified = true
    invoke(g_on_loaded)
}

// Starts a request and records its callback. The loader must be idle. All document texts
// are requested immediately; bundles are expanded when their text arrives.
def private Begin(var request : LoaderRequest; on_loaded : LoaderCallback) : bool {
    if (g_active) {
        print("[LOADER ERROR] a request is already active; call Abort first\n")
        return false
    }

    ResetState()
    g_active = true
    g_on_loaded = on_loaded

    for (bundle_path in request.bundles) {
        if (!IsDataPath(bundle_path)) {
            Fail("bundle '{bundle_path}' must be a data/... path")
            return false
        }
        if (!AddDocument(ContentKind.Bundle, bundle_path)) {
            Fail("bundle '{bundle_path}' was not found; check the data/ path")
            return false
        }
    }

    for (scene_path in request.scenes) {
        if (!IsContentPath(scene_path)) {
            Fail("scene '{scene_path}' must be a content/... path")
            return false
        }
        if (!AddDocument(ContentKind.Scene, scene_path)) {
            Fail("scene '{scene_path}' was not found; check the content/ path")
            return false
        }
    }

    for (prefab_path in request.prefabs) {
        if (!IsContentPath(prefab_path)) {
            Fail("prefab '{prefab_path}' must be a content/... path")
            return false
        }
        if (!AddDocument(ContentKind.Prefab, prefab_path)) {
            Fail("prefab '{prefab_path}' was not found; check the content/ path")
            return false
        }
    }

    g_phase = NextPhase(LoadPhase.Idle)
    return true
}

//! Starts one request and returns its task. Use it with `StartTask`:
//!
//!     var request <- LoaderRequest()
//!     request.AddScene("content/main.scene")
//!     StartTask(Loader::Load(request, @@OnLoaded))
//!
//! `on_loaded` runs once when every requested document and asset is ready; `Scene::Load`
//! and `Prefab::Spawn` are synchronous cache hits there. After the callback returns, the
//! loader releases its references. On failure the callback is not called and the phase
//! stays `Failed` for `IsFailed()`/`Error()`.
def public Load(var request : LoaderRequest; on_loaded : LoaderCallback) : auto {
    if (!Begin(request, on_loaded)) {
        return <- NoopTask()
    }
    let generation = g_generation
    return <- LoadTask(generation)
}

[async]
def private NoopTask() : void {
}

// The task carries the generation of the request it started. `Abort` and `Finish` bump
// the loader generation, so a task that was queued before a Stop or a new request exits
// without touching the next session.
[async]
def private LoadTask(generation : uint64) : void {
    while (g_generation == generation && g_active && g_phase != LoadPhase.Ready && g_phase != LoadPhase.Failed) {
        Advance()
        if (g_phase == LoadPhase.Ready || g_phase == LoadPhase.Failed) {
            break
        }
        await_next_frame()
    }
    if (g_generation == generation && g_phase == LoadPhase.Ready) {
        Finish()
    }
}

// One frame of loading. Text reads, bundle expansion, dependency collection and asset
// polling all happen here. Does nothing after Ready/Failed.
[profile]
def private Advance() {
    if (!g_active || g_phase == LoadPhase.Ready || g_phase == LoadPhase.Failed) {
        return
    }

    if (g_phase == LoadPhase.Bundles) {
        ProcessDocuments(ContentKind.Bundle)
        if (g_phase == LoadPhase.Failed) {
            return
        }
        if (AllDocumentsDone(ContentKind.Bundle)) {
            g_phase = NextPhase(g_phase)
        }
    }

    if (g_phase == LoadPhase.Scenes) {
        ProcessDocuments(ContentKind.Scene)
        if (g_phase == LoadPhase.Failed) {
            return
        }
        if (AllDocumentsDone(ContentKind.Scene)) {
            g_phase = NextPhase(g_phase)
        }
    }

    if (g_phase == LoadPhase.Prefabs) {
        ProcessDocuments(ContentKind.Prefab)
        if (g_phase == LoadPhase.Failed) {
            return
        }
        if (AllDocumentsDone(ContentKind.Prefab)) {
            g_phase = NextPhase(g_phase)
        }
    }

    if (g_phase == LoadPhase.Assets) {
        RequestAssets()

        var complete = true
        for (index in range(length(g_assets))) {
            if (g_assets[index].done) {
                continue
            }
            let id = g_assets[index].id
            if (g_assets[index].kind == LoaderAssetKind.Mesh) {
                if (ResourceIsFailedMesh(id)) {
                    Fail("mesh {id} failed to load")
                    return
                }
                if (ResourceIsReadyMesh(id)) {
                    g_assets[index].done = true
                } else {
                    complete = false
                }
            } else {
                if (ResourceIsFailedMaterial(id)) {
                    Fail("material {id} failed to load")
                    return
                }
                if (ResourceIsReadyMaterial(id)) {
                    g_assets[index].done = true
                } else {
                    complete = false
                }
            }
        }

        if (complete) {
            g_phase = LoadPhase.Ready
        }
    }

    if (g_phase == LoadPhase.Ready) {
        NotifyReady()
    }
}

// Releases the loader's references after the callback. Preloaded documents stay in
// `Content`'s cache and compiled prefabs stay in `Prefab`'s cache, so a later
// `Scene::Load` / `Prefab::Spawn` still works.
def private Finish() {
    if (!g_active) {
        return
    }
    ReleaseAll()
    ResetState()
}

//! Cancels the request. Content and prefab caches are not touched. Also invalidates a
//! task that was started but has not run yet.
def public Abort() {
    if (g_active) {
        ReleaseAll()
    }
    ResetState()
}

// Marks the request failed and releases its references. The phase stays `Failed` so the
// caller can read `Error`; `Finish`/`Abort` clear it.
def private Fail(reason : string) {
    if (!g_active) {
        return
    }
    g_phase = LoadPhase.Failed
    g_error = reason
    ReleaseAll()
    print("[LOADER ERROR] {reason}\n")
}

def public IsActive() : bool {
    return g_active
}

def public IsReady() : bool {
    return g_active && g_phase == LoadPhase.Ready
}

def public IsFailed() : bool {
    return g_active && g_phase == LoadPhase.Failed
}

def public Phase() : LoadPhase {
    return g_phase
}

def public Error() : string {
    return g_error
}
