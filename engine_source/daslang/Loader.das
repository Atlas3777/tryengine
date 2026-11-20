options gen2
options relaxed_assign = false

module Loader shared public

//! Explicit asset loader.
//!
//! The game builds a `LoaderRequest` (prefabs, bundles, scenes), optionally names the
//! scene shown while the request runs, and calls `Invoke`. The loader reads texts,
//! expands bundles, parses scene/prefab dependencies, requests meshes and materials and
//! exposes progress and per-phase counts. It never switches the target scene: the game
//! calls `Scene::Load(path, callback)` when the loader is ready.
//!
//! `blocking` is a contract for the caller: while `IsBlocking()` is true, gameplay should
//! not advance. The loader itself always runs through `Update`, so a loading screen stays
//! interactive and can read `Headline()`/`Current()`/`Total()`/`Progress()`.

require strings
require math
require daslib/linq
require daslib/strings_boost
require tryengine/Assets
require tryengine/Bundle
require tryengine/Prefab
require tryengine/Profiling
require tryengine/Scene
require tryResources

enum public LoaderPhase {
    Idle
    LoadingScene
    Bundles
    Scenes
    Prefabs
    Assets
    Steps
    Ready
    Failed
}

// Optional notifications. Scene-specific code should use the callback passed to
// `Scene::Load`, not these events; they exist for HUD/animation/telemetry.
enum public LoaderEvent {
    LoadingSceneStarted
    BundlesLoaded
    ScenesLoaded
    PrefabsLoaded
    AssetsReady
    Ready
    Failed
}

typedef LoaderListener = function<(event : LoaderEvent; name : string) : void>

// Builder. Array fields are filled in place; the request is consumed by `Invoke`.
struct public LoaderRequest {
    blocking : bool = true
    loading_scene : string = ""
    prefabs : array<string>
    bundles : array<string>
    scenes : array<string>
    steps : array<string>

    def static AddStep(var self : LoaderRequest; name : string) {
        self.steps |> push(name)
    }

    def static AddPrefab(var self : LoaderRequest; name : string) {
        self.prefabs |> push(name)
    }

    def static AddBundle(var self : LoaderRequest; name : string) {
        self.bundles |> push(name)
    }

    def static AddScene(var self : LoaderRequest; path : string) {
        self.scenes |> push(path)
    }

    def static SetLoadingScene(var self : LoaderRequest; path : string) {
        self.loading_scene = path
    }

    def static SetBlocking(var self : LoaderRequest; blocking : bool) {
        self.blocking = blocking
    }
}

enum private LoaderAssetKind {
    Mesh
    Material
}

struct private LoaderText {
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

struct private LoaderStep {
    name : string
    done : bool
}

var private g_active = false
var private g_phase = LoaderPhase.Idle
var private g_blocking = true
var private g_loading_scene = ""
var private g_loading_text : LoaderText
var private g_bundles : array<LoaderText>
var private g_scenes : array<LoaderText>
var private g_prefabs : array<LoaderText>
var private g_assets : array<LoaderAsset>
var private g_steps : array<LoaderStep>
var private g_error = ""
var private g_progress = 0.0f
var private g_current = 0
var private g_total = 0
var private g_listeners : array<LoaderListener>

def public RegisterLoaderListener(listener : LoaderListener) {
    g_listeners |> push(listener)
}

def private Notify(event : LoaderEvent; name : string) {
    for (listener in g_listeners) {
        invoke(listener, event, name)
    }
}

def private AddText(var items : array<LoaderText>; path : string) : bool {
    let id = Assets::IdOf(path)
    if (id == 0ul) {
        print("[LOADER ERROR] asset '{path}' is not registered\n")
        return false
    }

    for (item in items) {
        if (item.id == id) {
            return true
        }
    }

    InvalidateAssetText(id)
    RequestLoadAssetText(id)
    items |> push(LoaderText(path = path, id = id, held = true))
    return true
}

def private AddAsset(kind : LoaderAssetKind; id : uint64) {
    if (id == 0ul) {
        return
    }
    for (item in g_assets) {
        if (item.kind == kind && item.id == id) {
            return
        }
    }
    g_assets |> push(LoaderAsset(kind = kind, id = id))
}

def private ReleaseText(var item : LoaderText) {
    if (item.held) {
        ReleaseAssetText(item.id)
        item.held = false
    }
}

def private ReleaseAll() {
    ReleaseText(g_loading_text)
    for (index in range(length(g_bundles))) {
        ReleaseText(g_bundles[index])
    }
    for (index in range(length(g_scenes))) {
        ReleaseText(g_scenes[index])
    }
    for (index in range(length(g_prefabs))) {
        ReleaseText(g_prefabs[index])
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
    g_loading_scene = ""
    g_error = ""
    g_progress = 0.0f
    g_current = 0
    g_total = 0
    g_loading_text = LoaderText()
    clear(g_bundles)
    clear(g_scenes)
    clear(g_prefabs)
    clear(g_assets)
    clear(g_steps)
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

def private AddRefs(refs : AssetRefs) {
    for (mesh in refs.meshes) {
        AddAsset(LoaderAssetKind.Mesh, mesh)
    }
    for (material in refs.materials) {
        AddAsset(LoaderAssetKind.Material, material)
    }
}

def private PhaseStats(done_count : int; total_count : int) -> void {
    g_current = done_count
    g_total = total_count
}

def private UpdateProgress() {
    var base = 0.0f
    var span = 0.0f
    var done = 0.0f

    if (g_phase == LoaderPhase.LoadingScene) {
        base = 0.0f
        span = 0.05f
        PhaseStats(g_loading_text.done ? 1 : 0, 1)
        done = g_loading_text.done ? 1.0f : 0.0f
    } elif (g_phase == LoaderPhase.Bundles) {
        base = 0.05f
        span = 0.10f
        var count = 0
        for (item in g_bundles) {
            if (item.done) { count++ }
        }
        PhaseStats(count, length(g_bundles))
        done = length(g_bundles) == 0 ? 0.0f : float(count) / float(length(g_bundles))
    } elif (g_phase == LoaderPhase.Scenes) {
        base = 0.15f
        span = 0.20f
        var count = 0
        for (item in g_scenes) {
            if (item.done) { count++ }
        }
        PhaseStats(count, length(g_scenes))
        done = length(g_scenes) == 0 ? 0.0f : float(count) / float(length(g_scenes))
    } elif (g_phase == LoaderPhase.Prefabs) {
        base = 0.35f
        span = 0.25f
        var count = 0
        for (item in g_prefabs) {
            if (item.done) { count++ }
        }
        PhaseStats(count, length(g_prefabs))
        done = length(g_prefabs) == 0 ? 0.0f : float(count) / float(length(g_prefabs))
    } elif (g_phase == LoaderPhase.Assets) {
        base = 0.60f
        span = 0.30f
        var count = 0
        for (item in g_assets) {
            if (item.done) { count++ }
        }
        PhaseStats(count, length(g_assets))
        done = length(g_assets) == 0 ? 0.0f : float(count) / float(length(g_assets))
    } elif (g_phase == LoaderPhase.Steps) {
        base = 0.90f
        span = 0.10f
        var count = 0
        for (step in g_steps) {
            if (step.done) { count++ }
        }
        PhaseStats(count, length(g_steps))
        done = length(g_steps) == 0 ? 0.0f : float(count) / float(length(g_steps))
    } elif (g_phase == LoaderPhase.Ready) {
        g_progress = 1.0f
        return
    } else {
        g_progress = 0.0f
        return
    }

    g_progress = base + span * done
}

//! Starts a request. The loader must be idle; call `Abort` or `Finish` first. All texts
//! are requested immediately; bundles are expanded when their text arrives.
def public Invoke(var request : LoaderRequest) : bool {
    if (g_active) {
        print("[LOADER ERROR] a request is already active; call Abort or Finish first\n")
        return false
    }

    ResetState()
    g_active = true
    g_blocking = request.blocking
    g_loading_scene = request.loading_scene

    if (!empty(g_loading_scene)) {
        g_loading_text.path = g_loading_scene
        let id = Assets::IdOf(g_loading_scene)
        if (id == 0ul) {
            Fail("loading scene '{g_loading_scene}' is not registered")
            return false
        }
        InvalidateAssetText(id)
        RequestLoadAssetText(id)
        g_loading_text.id = id
        g_loading_text.held = true
        g_phase = LoaderPhase.LoadingScene
    } else {
        g_phase = LoaderPhase.Bundles
    }

    for (bundle_path in request.bundles) {
        let path = Bundle::FindInContent(bundle_path)
        if (empty(path)) {
            Fail("bundle '{bundle_path}' is not in content; use its path under game/data")
            return false
        }
        if (!AddText(g_bundles, path)) {
            Fail("bundle '{path}' is not registered")
            return false
        }
    }

    for (scene_path in request.scenes) {
        if (!AddText(g_scenes, scene_path)) {
            Fail("scene '{scene_path}' is not registered")
            return false
        }
    }

    for (prefab_path in request.prefabs) {
        let path = Prefab::FindInContent(prefab_path)
        if (empty(path)) {
            Fail("prefab '{prefab_path}' is not in content; use its path under game/content")
            return false
        }
        if (!AddText(g_prefabs, path)) {
            Fail("prefab '{path}' is not registered")
            return false
        }
    }

    for (step_name in request.steps) {
        g_steps |> push(LoaderStep(name = step_name))
    }

    if (g_phase == LoaderPhase.Bundles && length(g_bundles) == 0) {
        g_phase = length(g_scenes) > 0 ? LoaderPhase.Scenes : LoaderPhase.Prefabs
    }
    if (g_phase == LoaderPhase.Prefabs && length(g_prefabs) == 0) {
        g_phase = LoaderPhase.Assets
        RequestAssets()
    }

    UpdateProgress()
    return true
}

//! One frame of loading. Text reads, bundle expansion, dependency collection and asset
//! polling all happen here. Does nothing after Ready/Failed.
[profile]
def public Update() {
    if (!g_active || g_phase == LoaderPhase.Ready || g_phase == LoaderPhase.Failed) {
        return
    }

    if (g_phase == LoaderPhase.LoadingScene) {
        let id = g_loading_text.id
        if (AssetTextIsFailed(id)) {
            Fail("loading scene '{g_loading_scene}' failed to read")
            return
        }
        if (AssetTextIsReady(id)) {
            let text = GetAssetText(id)
            Scene::CacheText(g_loading_scene, text)
            ReleaseText(g_loading_text)
            g_loading_text.done = true
            if (!Scene::Load(g_loading_scene)) {
                Fail("loading scene '{g_loading_scene}' failed to load")
                return
            }
            Notify(LoaderEvent.LoadingSceneStarted, g_loading_scene)
            g_phase = LoaderPhase.Bundles
        }
    }

    if (g_phase == LoaderPhase.Bundles) {
        for (index in range(length(g_bundles))) {
            let item = g_bundles[index]
            if (item.done) {
                continue
            }
            if (AssetTextIsFailed(item.id)) {
                Fail("bundle '{item.path}' failed to read")
                return
            }
            if (!AssetTextIsReady(item.id)) {
                continue
            }

            let text = GetAssetText(item.id)
            var bundle = Bundle::Parse(item.path, text)
            ReleaseText(g_bundles[index])
            g_bundles[index].done = true
            if (bundle == null) {
                Fail("bundle '{item.path}' is not a valid bundle")
                return
            }
            print("[LOADER] bundle '{bundle.name}': {length(bundle.prefabs)} prefab(s)\n")
            for (prefab_path in bundle.prefabs) {
                let path = Prefab::FindInContent(prefab_path)
                if (empty(path)) {
                    Fail("bundle '{item.path}': prefab '{prefab_path}' is not in content; use its path under game/content")
                    return
                }
                if (!AddText(g_prefabs, path)) {
                    Fail("bundle '{item.path}': prefab '{path}' is not registered")
                    return
                }
            }
        }

        var complete = true
        for (item in g_bundles) {
            if (!item.done) { complete = false }
        }
        if (complete) {
            Notify(LoaderEvent.BundlesLoaded, "")
            g_phase = LoaderPhase.Scenes
        }
    }

    if (g_phase == LoaderPhase.Scenes) {
        for (index in range(length(g_scenes))) {
            let item = g_scenes[index]
            if (item.done) {
                continue
            }
            if (AssetTextIsFailed(item.id)) {
                Fail("scene '{item.path}' failed to read")
                return
            }
            if (!AssetTextIsReady(item.id)) {
                continue
            }

            let text = GetAssetText(item.id)
            Scene::CacheText(item.path, text)
            var refs <- Scene::CollectRefs(text)
            ReleaseText(g_scenes[index])
            g_scenes[index].done = true
            if (!refs.parsed) {
                Fail("scene '{item.path}' is not a valid scene document")
                return
            }
            AddRefs(refs)
        }

        var complete = true
        for (item in g_scenes) {
            if (!item.done) { complete = false }
        }
        if (complete) {
            Notify(LoaderEvent.ScenesLoaded, "")
            g_phase = LoaderPhase.Prefabs
        }
    }

    if (g_phase == LoaderPhase.Prefabs) {
        for (index in range(length(g_prefabs))) {
            let item = g_prefabs[index]
            if (item.done) {
                continue
            }
            if (AssetTextIsFailed(item.id)) {
                Fail("prefab '{item.path}' failed to read")
                return
            }
            if (!AssetTextIsReady(item.id)) {
                continue
            }

            let text = GetAssetText(item.id)
            let cached = Prefab::CachePrefabText(item.path, text)
            ReleaseText(g_prefabs[index])
            g_prefabs[index].done = true
            if (!cached) {
                Fail("prefab '{item.path}' is not a valid prefab")
                return
            }
            var refs <- Prefab::CollectRefs(Prefab::GetPrefabData(item.path))
            AddRefs(refs)
        }

        var complete = true
        for (item in g_prefabs) {
            if (!item.done) { complete = false }
        }
        if (complete) {
            Notify(LoaderEvent.PrefabsLoaded, "")
            g_phase = LoaderPhase.Assets
            RequestAssets()
        }
    }

    if (g_phase == LoaderPhase.Assets) {
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
            Notify(LoaderEvent.AssetsReady, "")
            g_phase = AllStepsDone() ? LoaderPhase.Ready : LoaderPhase.Steps
            if (g_phase == LoaderPhase.Ready) {
                Notify(LoaderEvent.Ready, "")
            }
        }
    }

    if (g_phase == LoaderPhase.Steps) {
        if (AllStepsDone()) {
            g_phase = LoaderPhase.Ready
            Notify(LoaderEvent.Ready, "")
        }
    }

    UpdateProgress()
}

def private AllStepsDone() : bool {
    for (step in g_steps) {
        if (!step.done) {
            return false
        }
    }
    return true
}

//! Completes a named external step (network sync, save read, warmup). Steps are requested
//! with `LoaderRequest::AddStep`; the loader stays active until every step is done.
def public CompleteStep(name : string) {
    for (index in range(length(g_steps))) {
        if (g_steps[index].name == name) {
            g_steps[index].done = true
            return
        }
    }
}

//! Releases the loader's references. Call after the target scene is applied; preloaded
//! scene texts live in `Scene`'s cache and stay available.
def public Finish() {
    if (!g_active) {
        return
    }
    ReleaseAll()
    ResetState()
}

//! Cancels the request. Preloaded scene texts stay in Scene's cache; call
//! `Scene::ClearCachedTexts` if you want to drop them too.
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
    Notify(LoaderEvent.Failed, reason)
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

def public Progress() : float {
    return g_progress
}

def public Current() : int {
    return g_current
}

def public Total() : int {
    return g_total
}

def public Error() : string {
    return g_error
}

def public Headline() : string {
    if (!g_active) {
        return "Idle"
    }
    if (g_phase == LoaderPhase.LoadingScene) {
        return "Loading scene"
    }
    if (g_phase == LoaderPhase.Bundles) {
        return "Bundles"
    }
    if (g_phase == LoaderPhase.Scenes) {
        return "Scenes"
    }
    if (g_phase == LoaderPhase.Prefabs) {
        return "Prefabs"
    }
    if (g_phase == LoaderPhase.Assets) {
        return "Assets"
    }
    if (g_phase == LoaderPhase.Steps) {
        return "Steps"
    }
    if (g_phase == LoaderPhase.Ready) {
        return "Ready"
    }
    if (g_phase == LoaderPhase.Failed) {
        return "Failed"
    }
    return "Idle"
}

def public Status() : string {
    if (!g_active) {
        return "Idle"
    }
    if (g_phase == LoaderPhase.Failed) {
        return "Failed: {g_error}"
    }
    return "{Headline()} {g_current}/{g_total}"
}
