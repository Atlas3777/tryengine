options gen2
options relaxed_assign = false

module Scene shared public

//! Scene document: application, saving and the active-scene resource set.
//!
//! A scene stores authored components and `parentLocalId` (a local index in `entities`);
//! `eid` and the derived `relationship` never reach the file.
//!
//! A scene is applied from a parsed `Content::Document`; the text is read by whoever owns
//! the flow (`Scene::LoadAsync` for the editor, `Loader` for preloaded target scenes).
//! `Scene::Load` is the synchronous apply step: it never reads the disk and never waits.

require tryengine/AssetCache
require tryengine/ConfigFormat
require tryengine/ConfigFormatBoost
require tryengine/Content
require tryengine/FileReader
require tryengine/Serializable
require tryengine/TransformSystem
require tryengine/Types
require tryResources

require daslib/async_boost
require daslib/decs
require daslib/decs_boost
require daslib/linq

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
    return Content::ResolvePath(DocumentKind.Scene, path)
}

// Scenes store the hierarchy as a local index (`parentLocalId`), exactly like a prefab:
// DECS assigns fresh `EntityId`s on load, so an absolute id from the file would be stale.
let private SCENE_ROOT_PARENT : uint = 4294967295u

def private ParentLocalId(cf : StructField?) : uint {
    if (cf == null) {
        return SCENE_ROOT_PARENT
    }
    return from_CF(cf, type<uint>)
}

// The entity id lives in the archetype row. Save needs it to turn an absolute parent id
// back into a local index; the same read is used for the relationship component.
def private EntityIdAt(var arch : Archetype&; index : int64) : EntityId {
    unsafe {
        let comp & = arch.components[arch.eidIndex]
        return *addr<EntityId?>(comp.data[index * int64(comp.stride)])
    }
}

def private RelationshipAt(var arch : Archetype&; comp_idx : int; index : int64) : Relationship {
    unsafe {
        let comp & = arch.components[comp_idx]
        return *addr<Relationship?>(comp.data[index * int64(comp.stride)])
    }
}

// ---------------------------------------------------------------------------
// AssetCache kind
// ---------------------------------------------------------------------------
// The ready value of a scene is its parsed document; applying it stays in this module.

var private g_values : table<string; Content::Document?>

def private CompileSceneDocument(var document : Content::Document?) : Content::Document? {
    return <- document
}

def private RequestAsset(path : string) : bool {
    return AssetCache::Request("scene", path)
}

def private AdvanceAsset(path : string) : AssetCache::AssetState {
    return AssetCache::Advance(g_values, path, Content::ItemsField(DocumentKind.Scene),
                               @@CompileSceneDocument)
}

def private AssetDeps(path : string) : Content::Dependencies {
    return <- AssetCache::Dependencies(path)
}

// Reading the scene's own table is a plain non-generic access; the generic cache only
// writes values, so the returned document keeps its non-const type.
def private CachedDocument(resolved : string) : Content::Document? {
    if (AssetCache::State(resolved) != AssetCache::AssetState.Ready) {
        return null
    }
    return g_values[resolved]
}

def private CancelAsset(path : string) {
    AssetCache::Cancel(g_values, path)
}

def private ClearAssets() {
    AssetCache::Clear(g_values, "scene")
}

[init]
def private RegisterAssetKind() {
    AssetCache::RegisterKind("scene", AssetCache::AssetKindOps(
        request = @@RequestAsset,
        advance = @@AdvanceAsset,
        deps = @@AssetDeps,
        expands = @@AssetCache::NoExpansion,
        cancel = @@CancelAsset,
        clear = @@ClearAssets
    ))
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
    ResetWorldTransformCaches()
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
//! asynchronous part; the world swap is synchronous. A game that already loaded the scene
//! through `Loader::Load` calls synchronous `Scene::Load` instead of this.
[async]
def public LoadAsync(path : string) : void {
    await(LoadAsync(path, @@SceneNoop))
}

[async]
def public LoadAsync(path : string; on_loaded : SceneCallback) : void {
    let resolved = ScenePath(path)
    AssetCache::Refresh(g_values, "scene", resolved)

    while (AssetCache::Advance(g_values, resolved, Content::ItemsField(DocumentKind.Scene),
                               @@CompileSceneDocument) == AssetCache::AssetState.Loading) {
        await_next_frame()
    }

    let document = CachedDocument(resolved)
    if (document == null) {
        print("[SCENE ERROR] Scene '{resolved}' is not available or not a valid scene document.\n")
    } else {
        if (!ApplyDocument(document)) {
            print("[SCENE ERROR] Scene '{resolved}' failed to apply.\n")
        } else {
            NotifySceneLoaded(scene_context.scene_name, resolved, false)
            invoke(on_loaded)
        }
    }
}

//! Synchronous scene apply from the asset cache. It never reads the disk and never waits:
//! the caller must have preloaded the scene first (`Loader::Load` with `AddScene`, or
//! `Scene::LoadAsync`). Returns false and prints what was missing when the scene was not
//! preloaded.
def public Load(path : string) : bool {
    let resolved = ScenePath(path)
    let document = CachedDocument(resolved)
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
    clone(active_scene.requireMeshs, document.deps.meshes)
    clone(active_scene.requireMaterials, document.deps.materials)

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

    // A hierarchy link names a position in the file, so it is resolved after every entity
    // exists. `local_of` maps a file index to a loaded entity (skipped elements leave holes).
    var entity_ids : array<EntityId>
    var entity_tags : array<string>
    var parent_file : array<int>
    var local_of : table<int; int>
    entity_ids |> reserve(ent_count)
    entity_tags |> reserve(ent_count)
    parent_file |> reserve(ent_count)

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

        let parent_local_id = ParentLocalId(ent_obj?["parentLocalId"] ?? null)
        let parent_index = parent_local_id == SCENE_ROOT_PARENT ? -1 : int(parent_local_id)

        local_of[ent_idx] = total_loaded
        var new_eid : EntityId
        unsafe {
            new_eid = create_entity() @(eid : EntityId; var cmp : ComponentMap) {
                assume cur_ent_obj = ent_cf.value as _object
                for (field_name in keys(cur_ent_obj)) {
                    // The hierarchy is structural (written below, after every id exists), and
                    // runtime/derived components are never authored; `relationship` is rebuilt
                    // from `parentLocalId` right here.
                    if (field_name == "parentLocalId" || !IsSavedComponent(field_name)) {
                        continue
                    }
                    let cf = cur_ent_obj?[field_name] ?? null
                    let type_name = cf != null ? cf.typeName : ""
                    parse_and_apply_field(cmp, entity_tag, field_name, cf, warnings_count, type_name)
                }
                cmp |> set(RELATIONSHIP_FIELD, Relationship())
                ApplyWorldTransform(cmp)
            }
        }
        entity_ids |> push(new_eid)
        entity_tags |> push(entity_tag)
        parent_file |> push(parent_index)
        total_loaded++
    }

    commit()

    // Resolve file indices to loaded entities and build the sibling graph. A parent that is
    // missing, skipped or points at the entity itself leaves the entity as a root.
    var parent_local : array<int>
    resize(parent_local, total_loaded)
    for (i in range(total_loaded)) {
        let file_parent = parent_file[i]
        var local_parent = -1
        if (file_parent >= 0 && key_exists(local_of, file_parent)) {
            local_parent = local_of[file_parent]
            if (local_parent == i) {
                local_parent = -1
            }
        }
        if (file_parent >= 0 && local_parent < 0) {
            print("[SCENE WARNING] Entity '{entity_tags[i]}': parentLocalId {file_parent} does not name a loaded entity; treated as a root.\n")
            warnings_count++
        }
        parent_local[i] = local_parent
    }

    var first_child, prev_sibling, next_sibling : array<int>
    resize(first_child, total_loaded)
    resize(prev_sibling, total_loaded)
    resize(next_sibling, total_loaded)
    for (i in range(total_loaded)) {
        first_child[i] = -1
        prev_sibling[i] = -1
        next_sibling[i] = -1
    }

    var last_child : table<int; int>
    for (i in range(total_loaded)) {
        let p = parent_local[i]
        if (p < 0) {
            continue
        }
        if (key_exists(last_child, p)) {
            let previous = last_child[p]
            next_sibling[previous] = i
            prev_sibling[i] = previous
        } else {
            first_child[p] = i
        }
        last_child[p] = i
    }

    // The one relationship component goes straight into archetype storage: `update_entity`
    // would copy a whole entity row to change it.
    var relationship_index : table<int; int>
    for (i in range(total_loaded)) {
        let eid = entity_ids[i]
        let record = decsState.entityLookup[int(eid.id)]
        let arch_index = int(decsState.archetypeLookup[record.archetype]) - 1
        if (!key_exists(relationship_index, arch_index)) {
            relationship_index[arch_index] = decs::find_component_index(
                decsState.allArchetypes[arch_index], RELATIONSHIP_FIELD)
        }
        let p = parent_local[i]
        let c = first_child[i]
        let pv = prev_sibling[i]
        let nx = next_sibling[i]
        decs::set_direct_at(
            decsState.allArchetypes[arch_index],
            relationship_index[arch_index],
            int(record.index),
            Relationship(
                parent = p >= 0 ? entity_ids[p] : INVALID_ENTITY_ID,
                first_child = c >= 0 ? entity_ids[c] : INVALID_ENTITY_ID,
                prev = pv >= 0 ? entity_ids[pv] : INVALID_ENTITY_ID,
                next = nx >= 0 ? entity_ids[nx] : INVALID_ENTITY_ID
            )
        )
    }

    // A loaded scene is rendered in the frame it finishes, and the editor loads scenes after
    // the engine tick: compose the hierarchy once here instead of showing local transforms
    // for one frame.
    UpdateWorldTransforms()

    print("[SCENE] '{scene_context.scene_path}': {total_loaded} entities, {length(active_scene.requireMeshs)} mesh(es), {length(active_scene.requireMaterials)} material(s), {warnings_count} warning(s)\n")
    return true
}

// `_world` is derived, but it must exist from the moment the entity does: the transform pass
// never adds a component. Its value starts as the local transform; the pass that runs right
// after the load composes the hierarchy into it.
def private ApplyWorldTransform(var cmp : ComponentMap) {
    if (!decs::has(cmp, TRANSFORM_FIELD)) {
        return
    }
    ApplyEntityTransform(cmp, decs::get(cmp, TRANSFORM_FIELD, Transform()))
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
            AssetCache::Invalidate(g_values, target)
            print("[SCENE] Scene saved successfully to '{target}'.\n")
        }
    }
}

def private save_scene_to_config() : string {
    // The saved order defines `parentLocalId`, so collect the rows first and write after.
    var saved_arch : array<int>
    var saved_index : array<int64>
    var entity_ids : array<EntityId>
    var local_of : table<uint; int>

    for (arch_idx in range(length(decsState.allArchetypes))) {
        // The editor camera is editor state, not scene content.
        if (decs::has(decsState.allArchetypes[arch_idx], "editorCameraTag")) {
            continue
        }
        for (i in range64(decsState.allArchetypes[arch_idx].size)) {
            let eid = EntityIdAt(decsState.allArchetypes[arch_idx], i)
            saved_arch |> push(arch_idx)
            saved_index |> push(i)
            entity_ids |> push(eid)
            local_of[eid.id] = length(entity_ids) - 1
        }
    }

    var entities_list : array<StructField?>
    var relationship_index : table<int; int>
    for (entity_idx in range(length(entity_ids))) {
        let arch_idx = saved_arch[entity_idx]
        let row = saved_index[entity_idx]

        var entity_obj : table<string; StructField?>
        for (comp in decsState.allArchetypes[arch_idx].components) {
            // Pointers are runtime references; the policy decides the rest (runtime and
            // derived components such as `_world` and `relationship` are not authored).
            if (comp.info.basicType == Type.tPointer || !IsSavedComponent(comp.name)) {
                continue
            }

            var cf_val = DecsComponentToCF(comp, row)
            if (cf_val == null) {
                print("[SCENE WARNING] Component '{comp.name}' of type '{comp.info.fullName}' is not registered for saving; skipped. Mark the struct [DecsSerializable].\n")
                continue
            }
            cf_val.name = comp.name
            entity_obj[comp.name] = cf_val
        }

        // The hierarchy is authored as a local index, not as absolute entity ids.
        var parent_local = SCENE_ROOT_PARENT
        if (decs::has(decsState.allArchetypes[arch_idx], RELATIONSHIP_FIELD)) {
            if (!key_exists(relationship_index, arch_idx)) {
                relationship_index[arch_idx] = decs::find_component_index(
                    decsState.allArchetypes[arch_idx], RELATIONSHIP_FIELD)
            }
            let rel = RelationshipAt(
                decsState.allArchetypes[arch_idx], relationship_index[arch_idx], row)
            if (rel.parent != INVALID_ENTITY_ID && key_exists(local_of, rel.parent.id)) {
                let parent_idx = local_of[rel.parent.id]
                if (entity_ids[parent_idx].generation == rel.parent.generation) {
                    parent_local = uint(parent_idx)
                }
            }
        }
        var parent_cf = CF(parent_local)
        parent_cf.name = "parentLocalId"
        entity_obj["parentLocalId"] = parent_cf

        var entity_cf = CF(entity_obj)
        entity_cf.typeName = "Entity"
        entities_list |> push(entity_cf)
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
    AssetCache::Refresh(g_values, "scene", resolved)

    while (AssetCache::Advance(g_values, resolved, Content::ItemsField(DocumentKind.Scene),
                               @@CompileSceneDocument) == AssetCache::AssetState.Loading) {
        await_next_frame()
    }

    var empty_header : SceneHeader
    let document = CachedDocument(resolved)
    if (document == null) {
        print("[SCENE ERROR] Scene '{resolved}' is not a valid scene document.\n")
        yield <- empty_header
    }

    var header <- SceneHeader()
    header.name = Content::Stem(resolved)
    header.path = resolved
    clone(header.requireMeshs, document.deps.meshes)
    clone(header.requireMaterials, document.deps.materials)
    yield <- header
}
