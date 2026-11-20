options gen2
options relaxed_assign = false

module Loader shared public

//! Asynchronous content loader.
//!
//! The caller builds a `LoaderRequest` (bundles, scenes, prefabs) and calls `Invoke` with
//! a completion callback. The loader reads document texts through `tryengine/Content`,
//! expands bundles, compiles prefabs, collects mesh/material dependencies and waits for
//! the resources. It never applies a scene and never switches what the player sees:
//! scene entry is `Scene::Load` and belongs to the caller.
//!
//! `blocking` is a caller contract: while `IsBlocking()` is true, gameplay should not
//! advance. The loader itself always runs through `Update`, so a loading screen stays
//! interactive and can read `Progress(phase)` / `PhaseName(phase)`.

require strings
require tryengine/Assets
require tryengine/Bundle
require tryengine/Content
require tryengine/Prefab
require tryengine/Profiling
require tryResources

enum public LoaderPhase {
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

// Builder. Array fields are filled in place; the request is consumed by `Invoke`.
// All paths are relative to the game directory: scenes and prefabs use `content/...`,
// bundles use `data/...`. Extensions are explicit; `game/` is implied.
struct public LoaderRequest {
    blocking : bool = true
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

    def static SetBlocking(var self : LoaderRequest; blocking : bool) {
        self.blocking = blocking
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
var private g_phase = LoaderPhase.Idle
var private g_blocking = true
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

// Phase layout of the overall progress bar. Phases with no documents are skipped, so a
// request with only prefabs jumps straight to the prefab span.
def private PhaseBase(phase : LoaderPhase) : float {
    if (phase == LoaderPhase.Bundles) {
        return 0.0f
    }
    if (phase == LoaderPhase.Scenes) {
        return 0.10f
    }
    if (phase == LoaderPhase.Prefabs) {
        return 0.30f
    }
    if (phase == LoaderPhase.Assets) {
        return 0.60f
    }
    return 1.0f
}

def private PhaseSpan(phase : LoaderPhase) : float {
    if (phase == LoaderPhase.Bundles) {
        return 0.10f
    }
    if (phase == LoaderPhase.Scenes) {
        return 0.20f
    }
    if (phase == LoaderPhase.Prefabs) {
        return 0.30f
    }
    if (phase == LoaderPhase.Assets) {
        return 0.40f
    }
    return 0.0f
}

//! Items done and total for one phase. `Loader::Progress(LoaderPhase.Prefabs)` returns
//! e.g. `float2(10, 23)` while 23 prefabs were requested and 10 are ready. A phase that
//! has not started yet returns `float2(0, total)` as soon as its items are known; an
//! unknown phase returns `float2(0, 0)`.
def public Progress(phase : LoaderPhase) : float2 {
    if (phase == LoaderPhase.Ready) {
        return float2(1.0f, 1.0f)
    }

    var done = 0
    var total = 0

    if (phase == LoaderPhase.Bundles || phase == LoaderPhase.Scenes || phase == LoaderPhase.Prefabs) {
        var kind = ContentKind.Bundle
        if (phase == LoaderPhase.Scenes) {
            kind = ContentKind.Scene
        } elif (phase == LoaderPhase.Prefabs) {
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
    } elif (phase == LoaderPhase.Assets) {
        total = length(g_assets)
        for (asset in g_assets) {
            if (asset.done) {
                done++
            }
        }
    }

    return float2(float(done), float(total))
}

//! Overall progress 0..1. A phase with no items counts as complete.
def public Progress() : float {
    if (!g_active || g_phase == LoaderPhase.Idle || g_phase == LoaderPhase.Failed) {
        return 0.0f
    }
    if (g_phase == LoaderPhase.Ready) {
        return 1.0f
    }

    let phase_progress = Progress(g_phase)
    let total = phase_progress.y
    let fraction = total <= 0.0f ? 1.0f : phase_progress.x / total
    return PhaseBase(g_phase) + PhaseSpan(g_phase) * fraction
}

def public PhaseName(phase : LoaderPhase) : string {
    if (phase == LoaderPhase.Bundles) {
        return "Bundles"
    }
    if (phase == LoaderPhase.Scenes) {
        return "Scenes"
    }
    if (phase == LoaderPhase.Prefabs) {
        return "Prefabs"
    }
    if (phase == LoaderPhase.Assets) {
        return "Assets"
    }
    if (phase == LoaderPhase.Ready) {
        return "Ready"
    }
    if (phase == LoaderPhase.Failed) {
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
    g_phase = LoaderPhase.Idle
    g_blocking = true
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

def private NextPhase(phase : LoaderPhase) : LoaderPhase {
    if (phase == LoaderPhase.Idle) {
        if (HasKind(ContentKind.Bundle)) {
            return LoaderPhase.Bundles
        }
    }
    if (phase == LoaderPhase.Idle || phase == LoaderPhase.Bundles) {
        if (HasKind(ContentKind.Scene)) {
            return LoaderPhase.Scenes
        }
    }
    if (phase == LoaderPhase.Idle || phase == LoaderPhase.Bundles || phase == LoaderPhase.Scenes) {
        if (HasKind(ContentKind.Prefab)) {
            return LoaderPhase.Prefabs
        }
    }
    return LoaderPhase.Assets
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

//! Starts a request. The loader must be idle; call `Abort` or `Finish` first. All document
//! texts are requested immediately; bundles are expanded when their text arrives. The
//! callback runs once, after the phase becomes `Ready`.
def public Invoke(var request : LoaderRequest) : bool {
    return Invoke(request, @@LoaderNoop)
}

def public Invoke(var request : LoaderRequest; on_loaded : LoaderCallback) : bool {
    if (g_active) {
        print("[LOADER ERROR] a request is already active; call Abort or Finish first\n")
        return false
    }

    ResetState()
    g_active = true
    g_blocking = request.blocking
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

    g_phase = NextPhase(LoaderPhase.Idle)
    return true
}

//! One frame of loading. Text reads, bundle expansion, dependency collection and asset
//! polling all happen here. Does nothing after Ready/Failed.
[profile]
def public Update() {
    if (!g_active || g_phase == LoaderPhase.Ready || g_phase == LoaderPhase.Failed) {
        return
    }

    if (g_phase == LoaderPhase.Bundles) {
        ProcessDocuments(ContentKind.Bundle)
        if (g_phase == LoaderPhase.Failed) {
            return
        }
        if (AllDocumentsDone(ContentKind.Bundle)) {
            g_phase = NextPhase(g_phase)
        }
    }

    if (g_phase == LoaderPhase.Scenes) {
        ProcessDocuments(ContentKind.Scene)
        if (g_phase == LoaderPhase.Failed) {
            return
        }
        if (AllDocumentsDone(ContentKind.Scene)) {
            g_phase = NextPhase(g_phase)
        }
    }

    if (g_phase == LoaderPhase.Prefabs) {
        ProcessDocuments(ContentKind.Prefab)
        if (g_phase == LoaderPhase.Failed) {
            return
        }
        if (AllDocumentsDone(ContentKind.Prefab)) {
            g_phase = NextPhase(g_phase)
        }
    }

    if (g_phase == LoaderPhase.Assets) {
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
            g_phase = LoaderPhase.Ready
        }
    }

    if (g_phase == LoaderPhase.Ready) {
        NotifyReady()
    }
}

//! Releases the loader's references. Preloaded documents stay in `Content`'s cache and
//! compiled prefabs stay in `Prefab`'s cache, so a later `Scene::Load` / `Prefab::Spawn`
//! still works. Call it after the target scene was applied.
def public Finish() {
    if (!g_active) {
        return
    }
    ReleaseAll()
    ResetState()
}

//! Cancels the request. Content and prefab caches are not touched.
def public Abort() {
    if (!g_active) {
        return
    }
    ReleaseAll()
    ResetState()
}

//! Marks the request failed and releases its references. The phase stays `Failed` so the
//! caller can read `Error`; `Finish`/`Abort` clear it.
def public Fail(reason : string) {
    if (!g_active) {
        return
    }
    g_phase = LoaderPhase.Failed
    g_error = reason
    ReleaseAll()
    print("[LOADER ERROR] {reason}\n")
}

def public IsActive() : bool {
    return g_active
}

def public IsReady() : bool {
    return g_active && g_phase == LoaderPhase.Ready
}

def public IsFailed() : bool {
    return g_active && g_phase == LoaderPhase.Failed
}

def public IsBlocking() : bool {
    return g_blocking
}

def public Phase() : LoaderPhase {
    return g_phase
}

def public Error() : string {
    return g_error
}
