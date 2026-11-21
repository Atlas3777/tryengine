options gen2
options relaxed_assign = false

module Scene shared public

//! Scene document: application, saving and the active-scene resource set.
//!
//! A scene is applied from a parsed `Content::Document`; the text is read by whoever owns
//! the flow (`Scene::LoadAsync` for the editor, `Loader` for preloaded target scenes).
//! `Scene::Load` is the synchronous apply step: it never reads the disk and never waits.

require tryengine/ConfigFormat
require tryengine/ConfigFormatBoost
require tryengine/Content
require tryengine/FileReader
require tryengine/Serializable
require tryengine/Types
require tryResources

require daslib/async_boost
require daslib/decs
require daslib/decs_boost
require daslib/linq
require tryengine/Assets
require strings

struct Scene {
    requireMeshs : array<uint64>
    requireMaterials : array<uint64>
}

// Editor play mode snapshots the scene description together with the DECS heap: the
// heap restores the entities, and this restores the resource references the entities
// render through. The game never captures or applies it.
struct public SceneState {
    name : string
    path : string
    requireMeshs : array<uint64>
    requireMaterials : array<uint64>
}

struct LoadedSceneContext {
    scene_name : string // derived from the file name
    scene_path : string
}

// Called after a scene is applied, so scene-specific code has the new DECS world.
typedef public SceneCallback = function<() : void>

var scene_context : LoadedSceneContext
var active_scene : Scene

// Scene lifecycle event. The editor subscribes to spawn its camera after every load;
// the game has no listener and therefore no editor camera. `empty` marks the initial
// untitled scene that has no file yet.
typedef SceneLoadedListener = function<(name : string; path : string; empty : bool) : void>

typedef SceneUnloadedListener = function<() : void>

var private g_scene_loaded_listeners : array<SceneLoadedListener>
var private g_scene_unloaded_listeners : array<SceneUnloadedListener>

def public RegisterSceneLoadedListener(listener : SceneLoadedListener) {
    g_scene_loaded_listeners |> push(listener)
}

def public RegisterSceneUnloadedListener(listener : SceneUnloadedListener) {
    g_scene_unloaded_listeners |> push(listener)
}

def private NotifySceneLoaded(name, path : string; empty : bool) {
    for (listener in g_scene_loaded_listeners) {
        invoke(listener, name, path, empty)
    }
}

def private NotifySceneUnloaded() {
    for (listener in g_scene_unloaded_listeners) {
        invoke(listener)
    }
}

def private SceneNoop() {
}

// Scenes live in content, so a bare path is relative to `content/`. The extension is
// always explicit and is never appended here.
def private ScenePath(path : string) : string {
    return Content::ResolvePath(ContentKind.Scene, path)
}

//! The editor's first load: there is no file yet, but it is still a scene load.
def public LoadEmptyScene() {
    Create("")
}

//! Creates an empty scene at `path` ("" for an untitled scene) and announces it.
def public Create(path : string) {
    UnloadActiveScene()
    scene_context.scene_path = ScenePath(path)
    scene_context.scene_name = Content::Stem(scene_context.scene_path)
    NotifySceneLoaded(scene_context.scene_name, scene_context.scene_path, true)
}

//! Clears the active scene and releases the resources it held.
def public UnloadActiveScene() {
    NotifySceneUnloaded()
    ReleaseActiveSceneResources()
    active_scene.requireMeshs |> clear()
    active_scene.requireMaterials |> clear()
    active_scene <- Scene()
    decs::restart()
}

def private ReleaseActiveSceneResources() {
    for (mat in active_scene.requireMaterials) {
        ReleaseMaterialResource(mat)
    }
    for (mesh in active_scene.requireMeshs) {
        ReleaseMeshResource(mesh)
    }
}

//! Updates the open-scene context without touching the world (Save As / Save path).
def public SetScenePath(path : string) {
    scene_context.scene_path = ScenePath(path)
    scene_context.scene_name = Content::Stem(scene_context.scene_path)
}

def public GetScenePath() : string {
    return scene_context.scene_path
}

//! Editor play mode: the scene the editor had open before GameStart. `ApplyState` puts
//! it back and re-requests its resources; the caller restores the DECS heap separately.
def public CaptureState() : SceneState {
    var state : SceneState
    state.name = scene_context.scene_name
    state.path = scene_context.scene_path
    clone(state.requireMeshs, active_scene.requireMeshs)
    clone(state.requireMaterials, active_scene.requireMaterials)
    return <- state
}

//! Releases the resources of the scene that is active now (the play scene), installs
//! the captured editor scene, and requests its resources again. Does not touch DECS:
//! the caller loads the heap snapshot after this.
def public ApplyState(var state : SceneState) {
    ReleaseActiveSceneResources()

    clone(active_scene.requireMeshs, state.requireMeshs)
    clone(active_scene.requireMaterials, state.requireMaterials)
    scene_context.scene_name = state.name
    scene_context.scene_path = state.path

    for (mesh in active_scene.requireMeshs) {
        RequestLoadMesh(mesh)
    }
    for (mat in active_scene.requireMaterials) {
        RequestLoadMaterial(mat)
    }
}

//! Reads a scene document asynchronously, parses it, applies it and then calls
//! `on_loaded`. This is the editor's Open Scene path: the text read is the only
//! asynchronous part; the world swap is synchronous. A game that already owns the text
//! (the boot flow) calls `Content::Cache` and then synchronous `Scene::Load` instead.
[async]
def public LoadAsync(path : string) : void {
    await(LoadAsync(path, @@SceneNoop))
}

[async]
def public LoadAsync(path : string; on_loaded : SceneCallback) : void {
    let resolved = ScenePath(path)
    let text : string <- await(Assets::ReadText(resolved))
    if (empty(text)) {
        print("[SCENE ERROR] Scene '{resolved}' is not available.\n")
    } else {
        let document = Content::Cache(ContentKind.Scene, resolved, text)
        if (!ApplyDocument(document)) {
            print("[SCENE ERROR] Scene '{resolved}' failed to apply.\n")
        } else {
            NotifySceneLoaded(scene_context.scene_name, resolved, false)
            invoke(on_loaded)
        }
    }
}

//! Synchronous scene apply from the parsed-document cache. It never reads the disk and
//! never waits: the caller must have preloaded the text first (`Loader::Load` with
//! `AddScene`, or `Scene::LoadAsync`). Returns false and prints what was missing when the
//! scene was not preloaded.
def public Load(path : string) : bool {
    let resolved = ScenePath(path)
    let document = Content::Get(ContentKind.Scene, resolved)
    if (document == null) {
        print("[SCENE ERROR] Scene '{resolved}' was not preloaded; add it to the LoaderRequest before Scene::Load.\n")
        return false
    }

    if (!ApplyDocument(document)) {
        return false
    }

    NotifySceneLoaded(scene_context.scene_name, resolved, false)
    return true
}

//! Applies the parsed document: swaps the world, installs the resource set and requests
//! its meshes and materials. The document was validated by `Content::Parse` (the root is
//! an object); this checks the scene-specific shape.
def private ApplyDocument(document : Content::Document?) : bool {
    if (document == null || document.root == null || !(document.root.value is _object)) {
        print("[SCENE ERROR] scene document root is not an object.\n")
        return false
    }

    assume root_obj = document.root.value as _object
    if (!root_obj |> key_exists("entities")) {
        print("[SCENE ERROR] scene document is missing 'entities'.\n")
        return false
    }

    let entities_cf = root_obj?["entities"] ?? null
    if (entities_cf == null || !(entities_cf.value is _array)) {
        print("[SCENE ERROR] 'entities' field is not an array.\n")
        return false
    }

    UnloadActiveScene()
    scene_context.scene_name = Content::Stem(document.path)
    scene_context.scene_path = document.path

    // The dependency set was collected once while parsing the document (header arrays and
    // entity components together), so the active scene releases exactly what it requested.
    clone(active_scene.requireMeshs, document.refs.meshes)
    clone(active_scene.requireMaterials, document.refs.materials)

    for (mesh_id in active_scene.requireMeshs) {
        RequestLoadMesh(mesh_id)
    }
    for (mat_id in active_scene.requireMaterials) {
        RequestLoadMaterial(mat_id)
    }

    assume entities_arr = entities_cf.value as _array
    let ent_count = length(entities_arr)
    var total_loaded = 0
    var warnings_count = 0

    for (ent_idx in range(ent_count)) {
        let ent_cf = entities_arr[ent_idx]
        if (ent_cf == null || !(ent_cf.value is _object)) {
            print("[SCENE WARNING] Element #{ent_idx} is not a valid Config object. Skipped.\n")
            warnings_count++
            continue
        }

        assume ent_obj = ent_cf.value as _object

        var entity_tag = "Entity_{ent_idx}"
        if (ent_obj |> key_exists("tag")) {
            let tag_cf = ent_obj?["tag"] ?? null
            if (tag_cf != null && tag_cf.value is _string) {
                entity_tag = from_CF(tag_cf, type<string>)
            }
        }

        unsafe {
            create_entity() @(eid : EntityId; var cmp : ComponentMap) {
                assume cur_ent_obj = ent_cf.value as _object
                for (field_name in keys(cur_ent_obj)) {
                    if (starts_with(field_name, "_")) {
                        continue
                    }
                    let cf = cur_ent_obj?[field_name] ?? null
                    let type_name = cf != null ? cf.typeName : ""
                    parse_and_apply_field(cmp, entity_tag, field_name, cf, warnings_count, type_name)
                }
            }
        }
        total_loaded++
    }

    commit()

    print("[SCENE] '{scene_context.scene_path}': {total_loaded} entities, {length(active_scene.requireMeshs)} mesh(es), {length(active_scene.requireMaterials)} material(s), {warnings_count} warning(s)\n")
    return true
}

def private parse_and_apply_field(var cmp : ComponentMap;
                                  entity_tag : string;
                                  field_name : string;
                                  cf : StructField?;
                                  var out_warnings : int&,
                                  type_name : string) : bool {
    if (cf == null || cf.value is _null) {
        print("[SCENE WARNING] Entity '{entity_tag}': Field '{field_name}' is null. Skipped.\n")
        out_warnings++
        return false
    }

    if (ApplyDecsConfigField(cmp, field_name, cf, type_name)) {
        return true
    }

    print("[SCENE WARNING] Entity '{entity_tag}': Field '{field_name}' has unsupported ConfigFormat type. Skipped.\n")
    out_warnings++
    return false
}

[async]
def public SaveCurrentScene() : void {
    await(SaveSceneAsync(scene_context.scene_path))
}

struct private SaveSceneDTO {
    requireMeshs : array<uint64>
    requireMaterials : array<string>
    entities : array<StructField?>
}

[async]
def public SaveSceneAsync(path : string) : void {
    let target = ScenePath(path)
    if (empty(target)) {
        print("[SCENE ERROR] Scene has no path; use Save As first.\n")
    } else {
        let config_text = save_scene_to_config()
        let written : bool <- await(FileReader::WriteFile(target, config_text))
        if (!written) {
            print("[SCENE ERROR] Failed to write scene to '{target}'.\n")
        } else {
            Content::Invalidate(ContentKind.Scene, target)
            print("[SCENE] Scene saved successfully to '{target}'.\n")
        }
    }
}

def private save_scene_to_config() : string {
    var entities_list : array<StructField?>

    for (arch in decsState.allArchetypes) {
        for (i in range64(arch.size)) {
            var entity_obj : table<string; StructField?>

            for (comp in arch.components) {
                // Pointers (mesh/material) are runtime references and are never authored.
                if (comp.info.basicType == Type.tPointer) {
                    continue
                }
                // Components starting with "_" are runtime-only (resource load markers etc.).
                if (starts_with(comp.name, "_")) {
                    continue
                }

                var cf_val = DecsComponentToCF(comp, i)
                if (cf_val == null) {
                    print("[SCENE WARNING] Component '{comp.name}' of type '{comp.info.fullName}' is not registered for saving; skipped. Mark the struct [DecsSerializable].\n")
                    continue
                }
                cf_val.name = comp.name
                entity_obj[comp.name] = cf_val
            }

            // The editor camera is editor state, not scene content.
            if (entity_obj |> key_exists("editorCameraTag")) {
                continue
            }

            var entity_cf = CF(entity_obj)
            entity_cf.typeName = "Entity"
            entities_list |> push(entity_cf)
        }
    }

    active_scene.requireMeshs |> clear()
    active_scene.requireMaterials |> clear()

    query() $(meshGuid : uint64) {
        if (!active_scene.requireMeshs.contains(meshGuid)) {
            active_scene.requireMeshs |> push(meshGuid)
        }
    }

    // The scene stores authored references, not ids: a project path for an authoritative
    // material, a guid for an imported one. `active_scene` keeps resolved ids for release.
    var material_refs : array<string>
    query() $(materialRef : MaterialRef) {
        if (!empty(materialRef.path) && !material_refs.contains(materialRef.path)) {
            material_refs |> push(materialRef.path)
        }
        let material_id = ResolveAssetRef(materialRef.path)
        if (material_id != 0ul && !active_scene.requireMaterials.contains(material_id)) {
            active_scene.requireMaterials |> push(material_id)
        }
    }

    var dto <- SaveSceneDTO(
        requireMeshs := active_scene.requireMeshs,
        requireMaterials <- material_refs,
        entities <- entities_list
    )

    return write_config(CF(dto))
}

struct SceneHeader {
    name : string
    path : string
    requireMeshs : array<uint64>
    requireMaterials : array<uint64>
}

//! Reads a scene document header for the build preview without applying it.
[async]
def public GetSceneHeader(path : string) : SceneHeader {
    let resolved = ScenePath(path)
    let text : string <- await(Assets::ReadText(resolved))
    var empty_header : SceneHeader
    if (empty(text)) {
        print("[SCENE ERROR] Scene '{resolved}' is not available.\n")
        yield <- empty_header
    }

    let document = Content::Cache(ContentKind.Scene, resolved, text)
    if (document == null) {
        print("[SCENE ERROR] Scene '{resolved}' is not a valid scene document.\n")
        yield <- empty_header
    }

    var header <- SceneHeader()
    header.name = Content::Stem(resolved)
    header.path = resolved
    clone(header.requireMeshs, document.refs.meshes)
    clone(header.requireMaterials, document.refs.materials)
    yield <- header
}
