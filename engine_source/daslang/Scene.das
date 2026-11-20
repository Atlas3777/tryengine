options gen2
options relaxed_assign = false

module Scene shared public

require tryengine/ConfigFormat
require tryengine/ConfigFormatBoost
require tryResources

require daslib/decs
require daslib/decs_boost
require daslib/safe_addr
require daslib/async_boost
require daslib/array_boost
require daslib/linq

require strings
require math
require tryengine/Assets
require tryengine/FileReader
require tryengine/Types
require tryengine/Serializable
require daslib/strings_boost


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
    scene_name : string // берется из имени файла
    scene_path : string
}

// Called after a scene is applied, so scene-specific code has the new DECS world.
typedef public SceneCallback = function<() : void>

var scene_context : LoadedSceneContext
var active_scene : Scene

// Preloaded scene texts keyed by asset id. `Loader` fills this so a later
// `Scene::Load` is a pure apply step and does not need a pak read.
var private g_scene_texts : table<uint64; string>

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

// Scenes live in content, so a bare path is relative to `content/`. The extension is
// always explicit and is never appended here.
def private ScenePath(path : string) : string {
    return Assets::ResolveGamePath(path, "content")
}

//! The editor's first load: there is no file yet, but it is still a scene load.
def public LoadEmptyScene() {
    UnloadActiveScene()
    scene_context.scene_name = ""
    scene_context.scene_path = ""
    NotifySceneLoaded("", "", true)
}

[async]
def LoadScene(name, path : string) : void {
    UnloadActiveScene()

    let resolved = ScenePath(path)
    scene_context.scene_name = name
    scene_context.scene_path = resolved

    await(LoadSceneAsync(resolved))
    NotifySceneLoaded(name, resolved, false)
}

//! Clears the active scene and releases the resources it held. Scene loads call it
//! internally; explicit entry points call it when they want to start from a blank world
//! before their own loading step (the game does this before Loader::Invoke).
def public UnloadActiveScene(){
    NotifySceneUnloaded()
    ReleaseActiveSceneResources()
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

def SetScenePath(name, path : string) {
    scene_context.scene_name = name
    scene_context.scene_path = ScenePath(path)
}

//! Keeps a parsed scene text for a later synchronous `Load`. The loader calls this after
//! reading the text asynchronously; the cache survives `Loader::Finish`, so preloaded
//! scenes can be entered at any later point.
def public CacheText(path : string; text : string) {
    let id = Assets::IdOf(ScenePath(path))
    if (id != 0ul) {
        g_scene_texts[id] = text
    }
}

def public InvalidateCachedText(path : string) {
    let id = Assets::IdOf(path)
    if (id != 0ul) {
        erase(g_scene_texts, id)
    }
}

def public ClearCachedTexts() {
    clear(g_scene_texts)
}

def GetScenePath() : string {
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

[async]
def CreateScene(name, path : string) : void {
    UnloadActiveScene()

    let resolved = ScenePath(path)
    scene_context.scene_name = name
    scene_context.scene_path = resolved
    NotifySceneLoaded(name, resolved, true)
}

[async]
def SaveCurrentScene() : void {
    await(SaveSceneAsync(scene_context.scene_path))
}

[async]
def SaveSceneAsync(path : string) : void {
    let target = ScenePath(path)
    let config_text = save_scene_to_config()
    let written : bool <- await(FileReader::WriteFile(target, config_text))
    if (!written) {
        print("[SCENE ERROR] Failed to write scene to '{target}'.\n")
    } else {
        InvalidateCachedText(target)
        print("[SCENE] Scene saved successfully to '{target}'.\n")
    }
}

[async]
def LoadSceneAsync(path : string) : void {
    let config_text : string <- await(Assets::ReadText(ScenePath(path)))
    if (empty(config_text)) {
        print("[SCENE ERROR] File '{path}' does not exist or is empty.\n")
    } else {
        print("[SCENE] Loaded {length(config_text)} bytes from '{path}'\n")
        load_scene_from_config(config_text)
    }
}

[DecsSerializable]
struct TestSer {
    name : string
    somee : uint64
    name2 : string
}

struct CastPlayer{
    hp : int
    speed : float
}

def private parse_uint64_array(cf : StructField?) : array<uint64> {
    var res : array<uint64>
    if (cf != null && cf.value is _array) {
        assume arr  = cf.value as _array
        for (elem in arr) {
            if (elem != null) {
                if (elem.value is _uint64) {
                    res |> push(elem.value as _uint64)
                } elif (elem.value is _int64) {
                    res |> push(uint64(elem.value as _int64))
                } elif (elem.value is _uint) {
                    res |> push(uint64(elem.value as _uint))
                } elif (elem.value is _int) {
                    res |> push(uint64(elem.value as _int))
                }
            }
        }
    }
    return <- res
}

// requireMaterials is an array of authored references: a project path or "guid:<id>".
// The loader resolves them to ids; there is no numeric fallback in the format.
def private parse_asset_ref_array(cf : StructField?) : array<uint64> {
    var res : array<uint64>
    if (cf == null || !(cf.value is _array))
        return <- res

    assume arr = cf.value as _array
    for (elem in arr) {
        if (elem == null || !(elem.value is _string))
            continue
        let id = ResolveAssetRef(elem.value as _string)
        if (id != 0ul && !res.contains(id))
            res |> push(id)
    }
    return <- res
}

// Refs live on the entity as components. `materialRef` is the only material form.
def private CollectEntityRefs(var refs : AssetRefs; entity_obj : table<string; StructField?> const) {
    let mesh_cf = entity_obj?["meshGuid"] ?? null
    if (mesh_cf != null) {
        AddMeshRef(refs, from_CF(mesh_cf, type<uint64>))
    }

    let material_cf = entity_obj?["materialRef"] ?? null
    if (material_cf != null && material_cf.value is _object) {
        let material = from_CF(material_cf, type<MaterialRef>)
        if (!empty(material.path)) {
            AddMaterialRef(refs, ResolveAssetRef(material.path))
        }
    }
}

//! Parsed dependency set of a scene document. Header arrays are authoritative, but a
//! scene saved by an older editor may list refs only on its entities, so both are read.
//! `parsed` is false when the text is not a valid scene object.
def public CollectRefs(text : string) : AssetRefs {
    var refs : AssetRefs

    var error : string
    var root = read_config(text, error)
    if (length(error) > 0 || root == null || !(root.value is _object)) {
        return <- refs
    }

    assume root_obj = root.value as _object
    refs.parsed = true

    let header_meshes <- parse_uint64_array(root_obj?["requireMeshs"] ?? null)
    for (id in header_meshes) {
        AddMeshRef(refs, id)
    }

    let header_materials <- parse_asset_ref_array(root_obj?["requireMaterials"] ?? null)
    for (id in header_materials) {
        AddMaterialRef(refs, id)
    }

    let entities_cf = root_obj?["entities"] ?? null
    if (entities_cf != null && entities_cf.value is _array) {
        assume entities_arr = entities_cf.value as _array
        for (entity_cf in entities_arr) {
            if (entity_cf == null || !(entity_cf.value is _object)) {
                continue
            }
            assume entity_obj = entity_cf.value as _object
            CollectEntityRefs(refs, entity_obj)
        }
    }

    return <- refs
}

struct private SaveSceneDTO {
    requireMeshs : array<uint64>
    requireMaterials : array<string>
    entities : array<StructField?>
}

def private save_scene_to_config() : string {
    var entities_list : array<StructField?>

    // 1. Собираем сущности из DECS
    for (arch in decsState.allArchetypes) {
        for (i in range64(arch.size)) {
            var entity_obj : table<string; StructField?>

            for (comp in arch.components) {
                // Указатели (mesh/material) — runtime-ссылки, в сцену они не пишутся.
                if (comp.info.basicType == Type.tPointer)
                    continue
                // Компоненты с "_" — runtime-only (маркеры загрузки ресурсов и т.п.).
                if (starts_with(comp.name, "_"))
                    continue

                var cf_val = ComponentToCF(comp, i)
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
        if (!active_scene.requireMeshs.contains(meshGuid))
            active_scene.requireMeshs |> push(meshGuid)
    }

    // В сцену пишем авторские ссылки, а не id: у авторитетного материала это путь,
    // у импортированного — guid. active_scene держит уже разрешённые id для выгрузки.
    var material_refs : array<string>
    query() $(materialRef : MaterialRef) {
        if (!empty(materialRef.path) && !material_refs.contains(materialRef.path))
            material_refs |> push(materialRef.path)
        let material_id = ResolveAssetRef(materialRef.path)
        if (material_id != 0ul && !active_scene.requireMaterials.contains(material_id))
            active_scene.requireMaterials |> push(material_id)
    }

    // 3. Формируем временный DTO для рефлексии CF()
    var dto <- SaveSceneDTO(
        requireMeshs := active_scene.requireMeshs,
        requireMaterials <- material_refs,
        entities <- entities_list
    )

    return write_config(CF(dto))
}


// Значение компонента пишется apply-based writer'ом, зарегистрированным для его типа
// (CF), поэтому аннотации полей обрабатываются так же, как при обычной сериализации.
def private ComponentToCF(comp : Component&; index : int64) : StructField? {
    let offset = index * int64(comp.stride)
    unsafe {
        let ptr = reinterpret<void?>(addr(comp.data[offset]))
        return DecsWriteComponent(ptr, comp.info.fullName, comp.name)
    }
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

def load_scene_from_config(config_text : string) : bool {
    var error : string
    var root = read_config(config_text, error)

    if (length(error) > 0 || root == null) {
        print("[SCENE ERROR] Failed to parse Config format: {error}\n")
        return false
    }

    if (!(root.value is _object)) {
        print("[SCENE ERROR] Config root is not an object.\n")
        return false
    }

    assume root_obj = root.value as _object
    if (!root_obj |> key_exists("entities")) {
        print("[SCENE ERROR] Config root missing 'entities' field.\n")
        return false
    }

    // Считываем списки ресурсов, сохраняем их в active_scene и запрашиваем их загрузку
    active_scene.requireMeshs <- parse_uint64_array(root_obj?["requireMeshs"] ?? null)
    active_scene.requireMaterials <- parse_asset_ref_array(root_obj?["requireMaterials"] ?? null)

    for (mesh_id in active_scene.requireMeshs) {
        RequestLoadMesh(mesh_id)
    }
    for (mat_id in active_scene.requireMaterials) {
        RequestLoadMaterial(mat_id)
    }

    let entities_cf = root_obj?["entities"] ?? null
    if (entities_cf == null || !(entities_cf.value is _array)) {
        print("[SCENE ERROR] 'entities' field is not an array.\n")
        return false
    }

    assume entities_arr = entities_cf.value as _array
    let ent_count = length(entities_arr)
    var total_loaded = 0
    var warnings_count = 0

    print("\n==================================================\n")
    print(" [SCENE LOAD] Starting runtime validation & load...\n")
    print("==================================================\n")

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
                print("create entity name = {entity_tag}\n")
                assume cur_ent_obj = ent_cf.value as _object
                for (field_name in keys(cur_ent_obj)) {
                    if (starts_with(field_name, "_"))
                        continue
                    let cf = cur_ent_obj?[field_name] ?? null
                    // print("field name = {field_name}\n")
                    parse_and_apply_field(cmp, entity_tag, field_name, cf, warnings_count, cf.typeName)
                }
            }
        }
        total_loaded++
    }

    commit()

    print("--------------------------------------------------\n")
    print(" [SCENE LOAD COMPLETED]\n")
    print(" Total Entities Loaded: {total_loaded}\n")
    print(" Meshes Requested: {length(active_scene.requireMeshs)}\n")
    print(" Materials Requested: {length(active_scene.requireMaterials)}\n")
    print(" Warnings / Skipped fields: {warnings_count}\n")
    print("==================================================\n\n")

    return true
}


struct SceneHeader {
    name : string
    path : string
    requireMeshs : array<uint64>
    requireMaterials : array<uint64>
}

// File name without directory and extension, as shown in the editor title.
def public SceneNameFromPath(path : string) : string {
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


//! Synchronous scene apply from the text cache. In the game the loader has already read
//! the text; in the editor the content layer serves the loose file synchronously.
def private LoadScenePathCached(path : string) : bool {
    let resolved = ScenePath(path)
    let id = Assets::IdOf(resolved)
    var config_text = ""
    if (id != 0ul && key_exists(g_scene_texts, id)) {
        config_text = g_scene_texts[id]
    } else {
        config_text = Assets::ReadTextCached(resolved)
    }
    if (empty(config_text)) {
        print("[SCENE ERROR] Scene '{resolved}' is not available.\n")
        return false
    }

    UnloadActiveScene()
    scene_context.scene_name = SceneNameFromPath(resolved)
    scene_context.scene_path = resolved

    let loaded = load_scene_from_config(config_text)
    if (loaded) {
        NotifySceneLoaded(scene_context.scene_name, resolved, false)
    }
    return loaded
}

//! Synchronous scene apply. `path` is relative to `content/` (`main.scene`), a
//! `content/...` path, or a full path; the extension is explicit. The text must be
//! cached: `Loader` fills the scene text cache asynchronously, and the editor can still
//! fall back to a loose-file read.
def public Load(path : string) : bool {
    return LoadScenePathCached(path)
}

//! Applies the scene and then calls `on_loaded`. The callback is the scene-specific hook:
//! this is where gameplay setup for that scene belongs, instead of a global load event.
def public Load(path : string; on_loaded : SceneCallback) : bool {
    let loaded = LoadScenePathCached(path)
    if (loaded) {
        invoke(on_loaded)
    }
    return loaded
}

[async]
def GetSceneHeader(let path : string -const) : SceneHeader {
    let resolved = ScenePath(path)
    let config_text : string <- await(Assets::ReadText(resolved))
    if (empty(config_text))
        return false

    var error : string
    let root = read_config(config_text, error)
    if (length(error) > 0 || root == null)
        return false

    if (!(root.value is _object))
        return false
    let root_obj & = root.value as _object

    var header <- SceneHeader()
    header.name = SceneNameFromPath(resolved)
    header.path = resolved
    header.requireMeshs <- parse_uint64_array(root_obj?["requireMeshs"] ?? null)
    header.requireMaterials <- parse_asset_ref_array(root_obj?["requireMaterials"] ?? null)

    yield <- header
}