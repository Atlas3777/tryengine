options gen2
options relaxed_assign = false

module EditorContent shared public

//! Content actions the editor performs on demand: open a scene, spawn a dropped prefab, read
//! the headers of a build. They all have the same shape — drop the cached form, ask the
//! loader, then apply synchronously from the cache — so the editor never grows a second reader
//! and `Loader::Load` stays the only place that starts I/O.
//!
//! The paths here are the editor's, not the authoring convention: an absolute path from a file
//! dialog, a repository path from the asset tree, or a `content/...` path. The loader resolves
//! whatever the asset registry knows, so a wrapper only registers a dialog path and invalidates
//! a stale cached document; it never parses or waits for anything itself.

require daslib/async_boost
require daslib/decs
require tryengine/Assets
require tryengine/Loader
require tryengine/Prefab
require tryengine/Scene

//! Releases a request that failed and keeps the loader active until its error is read. Every
//! editor action that starts a load calls it first, or the failure would block it.
def public ReleaseFailedLoad() {
    if (Loader::IsFailed()) {
        print("[EDITOR] content load failed: {Loader::Error()}\n")
        Loader::Abort()
    }
}

//! Opens a `.scene` and applies it: the loader reads and compiles the file, then `Scene::Load`
//! swaps the world synchronously.
[async]
def public OpenScene(path : string) : void {
    ReleaseFailedLoad()

    // The asset API is path-id based, so a file from a dialog is registered before the loader
    // asks for it.
    Assets::RegisterPath(path)
    // The file may have changed since it was last opened, and the loader trusts the cache.
    Scene::Invalidate(path)

    var request <- LoaderRequest()
    request.AddScene(path)
    await(Loader::Load(request))

    if (!Scene::Load(path)) {
        print("[EDITOR] scene '{path}' failed to open\n")
    }
}

//! Spawns a `.prefab` the user dropped, or the editor prefab artifact a glTF model was imported
//! into. The loader reads it and `Prefab::Spawn` creates the entities; `out_eid` is
//! `INVALID_ENTITY_ID` when the file or its resources are not available.
//!
//! The id comes back through a parameter because a value-returning coroutine cannot await
//! `Loader::Load`: the task is an `iterator`, and only a `: void` coroutine may discard its
//! result (a `yield value` body gets a "res only allowed for variants" error otherwise).
[async]
def public SpawnPrefab(path : string; position : float3; var out_eid : EntityId&) : void {
    ReleaseFailedLoad()

    // A drop may follow an edit of the file: the loader would apply the cached version.
    Prefab::InvalidatePrefab(path)

    var request <- LoaderRequest()
    request.AddPrefab(path)
    await(Loader::Load(request))

    out_eid = Prefab::Spawn(path, position)
}
