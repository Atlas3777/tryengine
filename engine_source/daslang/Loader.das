options gen2
options relaxed_assign = false

module Loader shared public

//! Asynchronous content loader.
//!
//! The caller builds a `LoaderRequest` (bundles, scenes, prefabs), starts
//! `Loader::Load(request, @@OnLoaded)` as a task and reads `GetProgress(phase)` while it
//! runs. The loader expands bundles, compiles prefabs and scenes, collects mesh/material
//! dependencies and waits for the resources.
//!
//! Every document kind is served by `tryengine/AssetCache`; the kind modules own their public
//! `Request`/`Advance`/`Cancel`, and this module names them explicitly. There is no registry
//! of kinds and no string dispatch: a new document kind is a compile error until it is named
//! here.
//!
//! A job carries the phase it belongs to from the moment it is queued, so `LoadPhase` is
//! output for the loading screen, not a state machine over kind names: progress and
//! `CurrentItem` read the queue.
//!
//! It never applies a scene and never switches what the player sees: scene entry is
//! `Scene::Load` and belongs to the callback.
//!
//! `Load` owns the request lifetime: it invokes the callback once on `Ready` and then
//! releases its references. On failure the phase stays `Failed`, `Error()` is available
//! and the callback is not called.

require daslib/async_boost
require strings
require tryengine/AssetCache
require tryengine/Assets
require tryengine/AssetMap
require tryengine/Bundle
require tryengine/Content
require tryengine/Prefab
require tryengine/Profiling
require tryengine/Scene
require tryResources

enum public LoadPhase {
    Idle
    Bundles
    Scenes
    Prefabs
    Resources
    Ready
    Failed
}

//! Ready alias tables of one request: canonical bundle path -> `AssetMap` handle. The screen
//! stack gets a handle from here and never looks a path up again.
typedef public LoaderCallback = function<(maps : table<string; AssetMap>) : void>

//! Handle of a requested bundle (`data/...` or canonical path); the empty map when the request
//! did not ask for it.
def public MapOf(maps : table<string; AssetMap>; bundle_path : string) : AssetMap {
    let resolved = Content::ResolvePath(DocumentKind.Bundle, bundle_path)
    if (!key_exists(maps, resolved)) {
        return AssetMap()
    }
    return maps?[resolved] ?? AssetMap()
}

// One already-addressed resource (mesh/material/texture/font). Content dependencies are
// collected automatically; this is for engine-owned ids that no document names, such as the
// fixed UI material.
struct public LoaderResourceRequest {
    kind : string
    id : uint64
}

// Builder. Array fields are filled in place; the request is consumed by `Loader::Load`.
// All paths are relative to the game directory: scenes and prefabs use `content/...`,
// bundles use `data/...`. Extensions are explicit; `game/` is implied.
struct public LoaderRequest {
    prefabs : array<string>
    bundles : array<string>
    scenes : array<string>
    resources : array<LoaderResourceRequest>

    def static AddPrefab(var self : LoaderRequest; name : string) {
        self.prefabs |> push(name)
    }

    def static AddBundle(var self : LoaderRequest; name : string) {
        self.bundles |> push(name)
    }

    def static AddScene(var self : LoaderRequest; path : string) {
        self.scenes |> push(path)
    }

    def static AddResource(var self : LoaderRequest; kind : string; id : uint64) {
        self.resources |> push(LoaderResourceRequest(kind = kind, id = id))
    }
}

enum private ResourceJobKind {
    Mesh
    Material
    Texture
    Font
}

//! One queued document. `phase` is fixed when the job is queued: the queue is the plan, so no
//! pass has to ask a kind what it is.
struct private AssetJob {
    phase : LoadPhase
    path  : string
    id    : uint64
    done  : bool
}

//! One mesh/material/texture/font the requested documents depend on.
struct private ResourceJob {
    kind : ResourceJobKind
    id : uint64
    //! Asset that named this resource (scene/prefab/bundle path), or "request" for an
    //! explicit AddResource. Used only by failure messages.
    source : string
    held : bool
    done : bool
}

var private g_active = false
var private g_phase = LoadPhase.Idle
// Incremented whenever a request starts or is released. A running `Load` task carries
// its own value, so an aborted request cannot advance the next one.
var private g_generation : uint64 = 0ul
var private g_asset_jobs : array<AssetJob>
var private g_resource_jobs : array<ResourceJob>
var private g_error = ""
var private g_on_loaded : LoaderCallback = @@LoaderNoop
var private g_maps : table<string; AssetMap>
var private g_notified = false

def private LoaderNoop(maps : table<string; AssetMap>) {
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

def private HasJobs(phase : LoadPhase) : bool {
    for (job in g_asset_jobs) {
        if (job.phase == phase) {
            return true
        }
    }
    return false
}

def private AllJobsDone(phase : LoadPhase) : bool {
    for (job in g_asset_jobs) {
        if (job.phase == phase && !job.done) {
            return false
        }
    }
    return true
}

//! Items done and total for one phase. `Loader::GetProgress(LoadPhase.Prefabs)` returns
//! e.g. `float2(10, 23)` while 23 prefabs were requested and 10 are ready. A phase that
//! has not started yet returns `float2(0, total)` as soon as its items are known; a phase
//! with no items, and `Idle`/`Failed`, returns `float2(0, 0)`.
def public GetProgress(phase : LoadPhase) : float2 {
    var done = 0
    var total = 0

    for (job in g_asset_jobs) {
        if (job.phase != phase) {
            continue
        }
        total++
        if (job.done) {
            done++
        }
    }

    if (phase == LoadPhase.Resources) {
        total = length(g_resource_jobs)
        for (job in g_resource_jobs) {
            if (job.done) {
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
    if (phase == LoadPhase.Resources) {
        return "Resources"
    }
    if (phase == LoadPhase.Ready) {
        return "Ready"
    }
    if (phase == LoadPhase.Failed) {
        return "Failed"
    }
    return "Idle"
}

//! Authored path of the document the request is working on, or "" when every document is
//! ready (the loader is waiting for resources). A loading screen shows
//! `PhaseName(Phase())`, `GetProgress(Phase())` and this.
def public CurrentItem() : string {
    for (job in g_asset_jobs) {
        if (!job.done) {
            return job.path
        }
    }
    return ""
}

// The three document phases are fixed: a bundle expands into prefabs, so it always resolves
// first, and resources wait for every document. `NextPhase` returns the first phase that has
// work left, and `Resources` when there is none.
def private NextPhase(phase : LoadPhase) : LoadPhase {
    if (phase == LoadPhase.Idle && HasJobs(LoadPhase.Bundles)) {
        return LoadPhase.Bundles
    }
    if ((phase == LoadPhase.Idle || phase == LoadPhase.Bundles) && HasJobs(LoadPhase.Scenes)) {
        return LoadPhase.Scenes
    }
    if ((phase == LoadPhase.Idle || phase == LoadPhase.Bundles || phase == LoadPhase.Scenes) &&
        HasJobs(LoadPhase.Prefabs)) {
        return LoadPhase.Prefabs
    }
    return LoadPhase.Resources
}

// ---------------------------------------------------------------------------
// Document jobs
// ---------------------------------------------------------------------------

//! Starts loading one bundle, scene or prefab by its authored path. False means the path was
//! not found or the storage layer cannot read it.
def private QueueBundle(path : string) : bool {
    let resolved = Content::Find(DocumentKind.Bundle, path)
    if (empty(resolved)) {
        return false
    }
    return QueueDocument(LoadPhase.Bundles, resolved)
}

def private QueueScene(path : string) : bool {
    let resolved = Content::Find(DocumentKind.Scene, path)
    if (empty(resolved)) {
        return false
    }
    return QueueDocument(LoadPhase.Scenes, resolved)
}

def private QueuePrefab(path : string) : bool {
    let resolved = Content::Find(DocumentKind.Prefab, path)
    if (empty(resolved)) {
        return false
    }
    return QueueDocument(LoadPhase.Prefabs, resolved)
}

// Queues one canonical path in its phase. A record that already exists (loading, ready or
// failed) is not requested again, so a repeated load of unchanged content does no I/O.
def private QueueDocument(phase : LoadPhase; resolved : string) : bool {
    let id = Assets::IdOf(resolved)
    if (id == 0ul) {
        return false
    }

    for (job in g_asset_jobs) {
        if (job.id == id) {
            return true
        }
    }

    var requested = false
    if (phase == LoadPhase.Bundles) {
        requested = Bundle::Request(resolved)
    } elif (phase == LoadPhase.Scenes) {
        requested = Scene::Request(resolved)
    } elif (phase == LoadPhase.Prefabs) {
        requested = Prefab::Request(resolved)
    } else {
        print("[LOADER ERROR] phase '{PhaseName(phase)}' is not a document phase\n")
        return false
    }
    if (!requested) {
        return false
    }

    g_asset_jobs |> push(AssetJob(phase = phase, path = resolved, id = id))
    return true
}

def private AdvanceDocument(job : AssetJob) : AssetCache::AssetState {
    if (job.phase == LoadPhase.Bundles) {
        return Bundle::Advance(job.path)
    }
    if (job.phase == LoadPhase.Scenes) {
        return Scene::Advance(job.path)
    }
    if (job.phase == LoadPhase.Prefabs) {
        return Prefab::Advance(job.path)
    }
    return AssetCache::AssetState.Failed
}

def private CancelDocument(job : AssetJob) {
    if (job.phase == LoadPhase.Bundles) {
        Bundle::Cancel(job.path)
    } elif (job.phase == LoadPhase.Scenes) {
        Scene::Cancel(job.path)
    } elif (job.phase == LoadPhase.Prefabs) {
        Prefab::Cancel(job.path)
    }
}

// Polls one phase: advances its records, adds dependencies of ready documents and expands a
// ready bundle into the prefabs and the parent bundle it asks for. Kind-specific validation
// and error printing live in the kind's compile function.
def private ProcessJobs(phase : LoadPhase) {
    let count = length(g_asset_jobs)
    for (index in range(count)) {
        let job = g_asset_jobs[index]
        if (job.phase != phase || job.done) {
            continue
        }

        let state = AdvanceDocument(job)
        if (state == AssetCache::AssetState.Failed) {
            Fail("'{job.path}' failed to load")
            return
        }
        if (state == AssetCache::AssetState.Loading) {
            continue
        }

        AddDependencies(AssetCache::Dependencies(job.path), job.path)

        // A bundle declares its parent as another document that must be ready before the
        // hierarchy can be linked, and expands into its own prefabs; both join the queue in
        // the phase they belong to.
        if (phase == LoadPhase.Bundles) {
            for (parent_path in Bundle::Parents(job.path)) {
                if (!IsDataPath(parent_path)) {
                    Fail("bundle '{job.path}': parent '{parent_path}' must be a data/... path")
                    return
                }
                if (!QueueBundle(parent_path)) {
                    Fail("bundle '{job.path}': parent '{parent_path}' was not found")
                    return
                }
            }

            for (prefab_path in Bundle::PrefabPaths(job.path)) {
                if (!IsContentPath(prefab_path)) {
                    Fail("bundle '{job.path}': prefab '{prefab_path}' must be a content/... path")
                    return
                }
                if (!QueuePrefab(prefab_path)) {
                    Fail("bundle '{job.path}': prefab '{prefab_path}' was not found; check the content/ path")
                    return
                }
            }
        }

        g_asset_jobs[index].done = true
    }
}

// Validates every bundle of the request (parents are bundle jobs too): a missing parent or a
// cycle fails the request before any scene or prefab is allowed to run.
def private ValidateBundles() : bool {
    for (job in g_asset_jobs) {
        if (job.phase != LoadPhase.Bundles) {
            continue
        }
        if (!Bundle::Validate(job.path)) {
            Fail("bundle '{job.path}' has an invalid parent chain")
            return false
        }
    }
    return true
}

def private FillMaps() {
    clear(g_maps)
    for (job in g_asset_jobs) {
        if (job.phase == LoadPhase.Bundles) {
            g_maps[job.path] = Bundle::Flatten(job.path)
        }
    }
}

def private NotifyReady() {
    if (g_notified) {
        return
    }
    g_notified = true
    FillMaps()
    invoke(g_on_loaded, g_maps)
}

// ---------------------------------------------------------------------------
// Resource jobs
// ---------------------------------------------------------------------------
// Four resource kinds, four operations each. The loader owns the whole table so "where is a
// texture requested" is one place; a new kind is a compile error until every operation
// handles it.

def private RequestResource(kind : ResourceJobKind; id : uint64) {
    if (kind == ResourceJobKind.Mesh) {
        RequestLoadMesh(id)
    } elif (kind == ResourceJobKind.Material) {
        RequestLoadMaterial(id)
    } elif (kind == ResourceJobKind.Texture) {
        RequestLoadTexture(id)
    } else {
        RequestLoadFont(id)
    }
}

def private ReleaseResource(kind : ResourceJobKind; id : uint64) {
    if (kind == ResourceJobKind.Mesh) {
        ReleaseMeshResource(id)
    } elif (kind == ResourceJobKind.Material) {
        ReleaseMaterialResource(id)
    } elif (kind == ResourceJobKind.Texture) {
        ReleaseTextureResource(id)
    } else {
        ReleaseFontResource(id)
    }
}

def private ResourceReady(kind : ResourceJobKind; id : uint64) : bool {
    if (kind == ResourceJobKind.Mesh) {
        return ResourceIsReadyMesh(id)
    }
    if (kind == ResourceJobKind.Material) {
        return ResourceIsReadyMaterial(id)
    }
    if (kind == ResourceJobKind.Texture) {
        return ResourceIsReadyTexture(id)
    }
    return ResourceIsReadyFont(id)
}

def private ResourceFailed(kind : ResourceJobKind; id : uint64) : bool {
    if (kind == ResourceJobKind.Mesh) {
        return ResourceIsFailedMesh(id)
    }
    if (kind == ResourceJobKind.Material) {
        return ResourceIsFailedMaterial(id)
    }
    if (kind == ResourceJobKind.Texture) {
        return ResourceIsFailedTexture(id)
    }
    return ResourceIsFailedFont(id)
}

//! Default material baked into a mesh artifact, 0 for every other kind. A mesh default is
//! known only once the mesh is loaded, so the queue grows while the phase is checked.
def private ResourceDefaultMaterial(kind : ResourceJobKind; id : uint64) : uint64 {
    if (kind != ResourceJobKind.Mesh) {
        return 0ul
    }
    return MeshDefaultMaterial(id)
}

def private AddResourceJob(kind : ResourceJobKind; id : uint64; source : string = "") {
    if (id == 0ul) {
        return
    }
    for (index in range(length(g_resource_jobs))) {
        if (g_resource_jobs[index].kind == kind && g_resource_jobs[index].id == id) {
            if (empty(g_resource_jobs[index].source) && !empty(source)) {
                g_resource_jobs[index].source = source
            }
            return
        }
    }
    g_resource_jobs |> push(ResourceJob(kind = kind, id = id, source = source))
}

def private AddDependencies(deps : Dependencies; source : string) {
    for (mesh in deps.meshes) {
        AddResourceJob(ResourceJobKind.Mesh, mesh, source)
    }
    for (material in deps.materials) {
        AddResourceJob(ResourceJobKind.Material, material, source)
    }
    for (texture in deps.textures) {
        AddResourceJob(ResourceJobKind.Texture, texture, source)
    }
    for (font in deps.fonts) {
        AddResourceJob(ResourceJobKind.Font, font, source)
    }
}

def private RequestResources() {
    for (index in range(length(g_resource_jobs))) {
        if (g_resource_jobs[index].held) {
            continue
        }
        RequestResource(g_resource_jobs[index].kind, g_resource_jobs[index].id)
        g_resource_jobs[index].held = true
    }
}

def private ReleaseAll() {
    for (index in range(length(g_asset_jobs))) {
        if (g_asset_jobs[index].done) {
            continue
        }
        CancelDocument(g_asset_jobs[index])
    }
    for (index in range(length(g_resource_jobs))) {
        if (!g_resource_jobs[index].held) {
            continue
        }
        ReleaseResource(g_resource_jobs[index].kind, g_resource_jobs[index].id)
        g_resource_jobs[index].held = false
    }
}

def private ResetState() {
    g_active = false
    g_phase = LoadPhase.Idle
    g_generation++
    g_error = ""
    g_on_loaded = @@LoaderNoop
    clear(g_maps)
    g_notified = false
    clear(g_asset_jobs)
    clear(g_resource_jobs)
}

// ---------------------------------------------------------------------------
// Request
// ---------------------------------------------------------------------------

// Starts a request and records its callback. The loader must be idle. Document requests are
// issued immediately; a record that is already cached needs no read at all.
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
        if (!QueueBundle(bundle_path)) {
            Fail("bundle '{bundle_path}' was not found; check the data/ path")
            return false
        }
    }

    for (scene_path in request.scenes) {
        if (!IsContentPath(scene_path)) {
            Fail("scene '{scene_path}' must be a content/... path")
            return false
        }
        if (!QueueScene(scene_path)) {
            Fail("scene '{scene_path}' was not found; check the content/ path")
            return false
        }
    }

    for (prefab_path in request.prefabs) {
        if (!IsContentPath(prefab_path)) {
            Fail("prefab '{prefab_path}' must be a content/... path")
            return false
        }
        if (!QueuePrefab(prefab_path)) {
            Fail("prefab '{prefab_path}' was not found; check the content/ path")
            return false
        }
    }

    for (resource in request.resources) {
        // Map the name inline: an enum passed as `var out` does not write back in this
        // daslang version (checked with a minimal probe), so a helper would leave the
        // default Mesh and request a material as a mesh.
        var kind = ResourceJobKind.Mesh
        if (resource.kind == "material") {
            kind = ResourceJobKind.Material
        } elif (resource.kind == "texture") {
            kind = ResourceJobKind.Texture
        } elif (resource.kind == "font") {
            kind = ResourceJobKind.Font
        } elif (resource.kind != "mesh") {
            Fail("resource of unknown kind '{resource.kind}'")
            return false
        }
        if (resource.id == 0ul) {
            Fail("resource of kind '{resource.kind}' has id 0")
            return false
        }
        AddResourceJob(kind, resource.id, "request")
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
//! `on_loaded` runs once when every requested document and resource is ready; `Scene::Load`
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

// One frame of loading. Document polling, bundle expansion, dependency collection and
// resource polling all happen here. Does nothing after Ready/Failed.
[profile]
def private Advance() {
    if (!g_active || g_phase == LoadPhase.Ready || g_phase == LoadPhase.Failed) {
        return
    }

    if (g_phase == LoadPhase.Bundles) {
        ProcessJobs(LoadPhase.Bundles)
        if (g_phase == LoadPhase.Failed) {
            return
        }
        if (AllJobsDone(LoadPhase.Bundles)) {
            if (!ValidateBundles()) {
                return
            }
            g_phase = NextPhase(g_phase)
        }
    }

    if (g_phase == LoadPhase.Scenes) {
        ProcessJobs(LoadPhase.Scenes)
        if (g_phase == LoadPhase.Failed) {
            return
        }
        if (AllJobsDone(LoadPhase.Scenes)) {
            g_phase = NextPhase(g_phase)
        }
    }

    if (g_phase == LoadPhase.Prefabs) {
        ProcessJobs(LoadPhase.Prefabs)
        if (g_phase == LoadPhase.Failed) {
            return
        }
        if (AllJobsDone(LoadPhase.Prefabs)) {
            g_phase = NextPhase(g_phase)
        }
    }

    if (g_phase == LoadPhase.Resources) {
        RequestResources()

        var complete = true
        for (index in range(length(g_resource_jobs))) {
            if (g_resource_jobs[index].done) {
                continue
            }
            let id = g_resource_jobs[index].id
            let kind = g_resource_jobs[index].kind
            let source = g_resource_jobs[index].source
            if (ResourceFailed(kind, id)) {
                Fail("{ResourceKindName(kind)} {id} (from {source}) failed to load")
                return
            }
            if (!ResourceReady(kind, id)) {
                complete = false
                continue
            }

            g_resource_jobs[index].done = true
            // Материал по умолчанию лежит в артефакте меша, поэтому он известен только
            // теперь: очередь ресурсов пополняется по ходу проверки, и загрузка не
            // считается законченной, пока он не готов.
            let default_material = ResourceDefaultMaterial(kind, id)
            if (default_material != 0ul) {
                AddResourceJob(ResourceJobKind.Material, default_material, "mesh {id}")
                complete = false
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

def private ResourceKindName(kind : ResourceJobKind) : string {
    if (kind == ResourceJobKind.Mesh) {
        return "mesh"
    }
    if (kind == ResourceJobKind.Material) {
        return "material"
    }
    if (kind == ResourceJobKind.Texture) {
        return "texture"
    }
    return "font"
}

// Releases the loader's references after the callback. Text requests are one-shot and are
// already gone; ready values stay in `AssetCache`, so a later `Scene::Load` /
// `Prefab::Spawn` still works.
def private Finish() {
    if (!g_active) {
        return
    }
    ReleaseAll()
    ResetState()
}

//! Cancels the request. Documents that already became ready stay in `AssetCache`; pending
//! text reads are cancelled. Also invalidates a task that was started but has not run yet.
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
