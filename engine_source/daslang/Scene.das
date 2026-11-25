options gen2
options relaxed_assign = false

module Scene shared public

//! Scene document: application, saving and the active-scene resource set.
//!
//! A scene stores authored components and `parentLocalId` (a local index in `entities`) for
//! entities that have a parent; a root stores no such field. `eid` and the derived
//! `relationship` never reach the file.
//!
//! A scene is applied from its compiled entity set; the text is read and compiled by
//! `Loader::Load`. `Scene::Load` is the synchronous apply step: it never reads the disk and
//! never waits.

require tryengine/AssetCache
require tryengine/ConfigFormat
require tryengine/ConfigFormatBoost
require tryengine/Content
require tryengine/EcsMacro
require tryengine/EntitySet
require tryengine/FileReader
require tryengine/Model
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

// Scenes store the hierarchy as a local index (`parentLocalId`) on entities that have a
// parent, exactly like a prefab: DECS assigns fresh `EntityId`s on load, so an absolute id
// from the file would be stale. A root has no parent and stores no field; writing the root
// parent id is an error, the format has no compatibility path for it.
let private SCENE_ROOT_PARENT : uint = 4294967295u

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
// Compilation: parsed document -> ready-to-create entity set
// ---------------------------------------------------------------------------

//! Compiles a parsed scene document into its runtime form: resolved component bytes per
//! entity, the sibling graph, the baked world transforms and the archetype groups. The parsed
//! document is consumed here, so what stays in the cache is the form the world is created
//! from, not a ConfigFormat tree: applying a scene neither parses a field nor dispatches one
//! by name, and an invalid document fails at load instead of in the middle of an apply.
//!
//! A scene stores `parentLocalId` as an index into the file's `entities` order, and a parent
//! may come after its child, so the hierarchy is resolved after every node exists. An entity
//! that is not an object is skipped with a warning, and a parent that does not name a loaded
//! entity leaves the node a root.
def public CompileScene(var document : Content::Document?) : EntitySet::EntitySetTemplate? {
    if (document == null || document.root == null || !(document.root.value is _object)) {
        print("[SCENE ERROR] scene document root is not an object.\n")
        return null
    }

    assume root_obj = document.root.value as _object
    if (!root_obj |> key_exists("entities")) {
        print("[SCENE ERROR] scene document is missing 'entities'.\n")
        return null
    }

    let entities_cf = root_obj?["entities"] ?? null
    if (entities_cf == null || !(entities_cf.value is _array)) {
        print("[SCENE ERROR] 'entities' field is not an array.\n")
        return null
    }
    assume entities_arr = entities_cf.value as _array

    var strings : array<string>
    EntitySet::CollectStrings(document.root, strings)

    // A root stores no `parentLocalId`; spelling the root parent id out is an error, not a
    // second way to say the same thing.
    for (ent_idx in range(length(entities_arr))) {
        let ent_cf = entities_arr[ent_idx]
        if (ent_cf == null || !(ent_cf.value is _object)) {
            continue
        }
        assume ent_obj = ent_cf.value as _object
        let parent_cf = ent_obj?["parentLocalId"] ?? null
        if (parent_cf != null && from_CF(parent_cf, type<uint>) == SCENE_ROOT_PARENT) {
            print("[SCENE ERROR] Element #{ent_idx}: a root must not store parentLocalId.\n")
            return null
        }
    }

    var nodes : array<EntitySet::EntityNode>
    // File index of a node -> index in `nodes`; a skipped element leaves a hole.
    var node_of : table<int; int>
    var file_parent : array<int>
    var warnings_count = 0

    for (ent_idx in range(length(entities_arr))) {
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

        var parent_index = -1
        let parent_cf = ent_obj?["parentLocalId"] ?? null
        if (parent_cf != null) {
            parent_index = int(from_CF(parent_cf, type<uint>))
        }

        var node <- EntitySet::EntityNode()
        node.parent = -1
        node.first_child = -1
        node.prev = -1
        node.next = -1

        // The defaults come first so every entity has an identity transform and an unlinked
        // relationship even when the file omits them; the derived `_world` is added below.
        node.values |> set(TRANSFORM_FIELD, Transform())
        node.values |> set(RELATIONSHIP_FIELD, Relationship())

        for (field_name in keys(ent_obj)) {
            // `eid` is assigned by DECS, and the hierarchy is structural (written below).
            if (field_name == "eid" || field_name == "parentLocalId") {
                continue
            }
            let cf = ent_obj?[field_name] ?? null
            // Only components declared `save = true` are authored data. A runtime component in
            // a hand-edited scene is dropped like an unknown field.
            if (!EcsShouldSave(field_name)) {
                print("[SCENE WARNING] Entity '{entity_tag}': field '{field_name}' is not a saved component. Skipped.\n")
                warnings_count++
                continue
            }
            if (cf == null || cf.value is _null) {
                print("[SCENE WARNING] Entity '{entity_tag}': Field '{field_name}' is null. Skipped.\n")
                warnings_count++
                continue
            }
            if (!ApplyDecsConfigField(node.values, field_name, cf, cf.typeName)) {
                print("[SCENE WARNING] Entity '{entity_tag}': Field '{field_name}' has unsupported ConfigFormat type. Skipped.\n")
                warnings_count++
            }
        }

        node.model = decs::get(node.values, Model::MODEL_FIELD, ModelRender())

        node_of[ent_idx] = length(nodes)
        nodes |> emplace(node)
        file_parent |> push(parent_index)
    }

    // Resolve file indices to node indices. A parent that is missing, skipped or points at the
    // node itself leaves the entity a root.
    for (node_index in range(length(nodes))) {
        let file_index = file_parent[node_index]
        var parent = -1
        if (file_index >= 0 && key_exists(node_of, file_index)) {
            parent = node_of[file_index]
            if (parent == node_index) {
                parent = -1
            }
        }
        if (file_index >= 0 && parent < 0) {
            print("[SCENE WARNING] Element #{file_index} does not name a loaded entity; treated as a root.\n")
            warnings_count++
        }
        nodes[node_index].parent = parent
    }

    // Derived relationship graph: only parents are stored on disk, and the file order is also
    // sibling order.
    var last_child : table<int; int>
    for (node_index in range(length(nodes))) {
        let parent = nodes[node_index].parent
        if (parent < 0) {
            continue
        }
        if (key_exists(last_child, parent)) {
            let previous = last_child[parent]
            nodes[previous].next = node_index
            nodes[node_index].prev = previous
        } else {
            nodes[parent].first_child = node_index
        }
        last_child[parent] = node_index
    }

    EntitySet::BakeWorldTransforms(nodes)

    var deps : Content::Dependencies
    clone(deps.meshes, document.deps.meshes)
    clone(deps.materials, document.deps.materials)
    clone(deps.textures, document.deps.textures)
    clone(deps.fonts, document.deps.fonts)

    if (warnings_count > 0) {
        print("[SCENE] '{document.path}': {warnings_count} warning(s) while compiling\n")
    }

    var groups <- EntitySet::BuildGroups(nodes)
    return new EntitySet::EntitySetTemplate(
        path = document.path,
        name = Content::Stem(document.path),
        strings <- strings,
        nodes <- nodes,
        groups <- groups,
        deps <- deps
    )
}

// ---------------------------------------------------------------------------
// AssetCache kind
// ---------------------------------------------------------------------------

var private g_values : table<string; EntitySet::EntitySetTemplate?>

//! Starts loading one scene document. False means the storage layer does not know the path.
def public Request(path : string) : bool {
    return AssetCache::Request("scene", path)
}

//! One step of the scene state machine: reads the text once and compiles the document.
def public Advance(path : string) : AssetCache::AssetState {
    return AssetCache::Advance(g_values, path, Content::ItemsField(DocumentKind.Scene),
                               @@CompileScene)
}

//! Reading the scene's own table is a plain non-generic access; the generic cache only writes
//! values, so the returned set keeps its non-const type.
def private CachedTemplate(resolved : string) : EntitySet::EntitySetTemplate? {
    if (AssetCache::State(resolved) != AssetCache::AssetState.Ready) {
        return null
    }
    return g_values[resolved]
}

//! Abandons a scene that is still loading; a compiled scene stays cached.
def public Cancel(path : string) {
    AssetCache::Cancel(g_values, path)
}

//! Drops the compiled scene so the next load re-reads and re-validates the file. An editor
//! action that may have changed the document on disk calls this before it starts a request:
//! the loader trusts the cache and would otherwise apply the previous version.
def public Invalidate(path : string) {
    AssetCache::Invalidate(g_values, ScenePath(path))
}

//! Drops every compiled scene, ready or failed.
def public Clear() {
    AssetCache::Clear(g_values, "scene")
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
    EcsApplyRegistry()
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
        if (!HoldMeshResource(mesh)) {
            print("[SCENE ERROR] mesh {mesh:d} is not resident while restoring the editor scene\n")
        }
    }
    for (mat in active_scene.requireMaterials) {
        if (!HoldMaterialResource(mat)) {
            print("[SCENE ERROR] material {mat:d} is not resident while restoring the editor scene\n")
        }
    }
}

//! Synchronous scene apply from the compiled cache. It never reads the disk and never waits:
//! the caller must have preloaded the scene first (`Loader::Load` with `AddScene`). Returns
//! false and prints what was missing when the scene was not preloaded or its resources are not
//! resident.
def public Load(path : string) : bool {
    let resolved = ScenePath(path)
    let compiled = CachedTemplate(resolved)
    if (compiled == null) {
        print("[SCENE ERROR] Scene '{resolved}' was not preloaded; add it to the LoaderRequest before Scene::Load.\n")
        return false
    }

    if (!ApplyTemplate(compiled)) {
        return false
    }

    NotifySceneLoaded(scene_context.scene_name, resolved, false)
    return true
}

//! Swaps the world for a compiled scene: validates that every model it names is resident,
//! installs the resource set and creates the entities. Everything is checked before the world
//! is touched, so a cold cache is one clear error instead of a half-applied scene.
def private ApplyTemplate(compiled : EntitySet::EntitySetTemplate?) : bool {
    if (compiled == null) {
        return false
    }

    // Appearance is resolved before the world is touched: the frame loop never loads, so a
    // model that is not resident is a load error, not a pop-in.
    var model_refs : array<Model::ModelRefs>
    if (!EntitySet::ResolveModels(compiled, model_refs)) {
        print("[SCENE ERROR] Scene '{compiled.path}' was not fully preloaded; add it to the LoaderRequest before Scene::Load.\n")
        return false
    }

    // The header may list ids no entity uses; they are still the document's dependencies and
    // the loader was asked for them.
    for (mesh_id in compiled.deps.meshes) {
        if (!ResourceIsReadyMesh(mesh_id)) {
            print("[SCENE ERROR] mesh {mesh_id:d} is not loaded; preload the scene first.\n")
            return false
        }
    }
    for (mat_id in compiled.deps.materials) {
        if (!ResourceIsReadyMaterial(mat_id)) {
            print("[SCENE ERROR] material {mat_id:d} is not loaded; preload the scene first.\n")
            return false
        }
    }

    UnloadActiveScene()
    scene_context.scene_name = Content::Stem(compiled.path)
    scene_context.scene_path = compiled.path

    // The dependency set was collected once while compiling, so the active scene releases
    // exactly what it requested.
    clone(active_scene.requireMeshs, compiled.deps.meshes)
    clone(active_scene.requireMaterials, compiled.deps.materials)

    // The document lists the explicit overrides; a model without one uses the mesh artifact
    // default, so the scene takes its own reference on that material as well and releases it
    // on unload together with everything else.
    for (refs in model_refs) {
        if (refs.material != 0ul && !active_scene.requireMaterials.contains(refs.material)) {
            active_scene.requireMaterials |> push(refs.material)
        }
    }

    // Reference-only: the loader already brought every id the document lists. A hold that
    // fails means the resource was not preloaded; the frame path never starts a read.
    for (mesh_id in active_scene.requireMeshs) {
        if (!HoldMeshResource(mesh_id)) {
            print("[SCENE ERROR] mesh {mesh_id:d} is not resident; preload the scene first.\n")
        }
    }
    for (mat_id in active_scene.requireMaterials) {
        if (!HoldMaterialResource(mat_id)) {
            print("[SCENE ERROR] material {mat_id:d} is not resident; preload the scene first.\n")
        }
    }

    var entity_ids : array<EntityId>
    if (length(compiled.nodes) > 0) {
        let root = EntitySet::CreateEntities(compiled, float3(0.0f, 0.0f, 0.0f), model_refs, entity_ids)
        if (root == INVALID_ENTITY_ID) {
            return false
        }
    }

    print("[SCENE] '{scene_context.scene_path}': {length(entity_ids)} entities, {length(active_scene.requireMeshs)} mesh(es), {length(active_scene.requireMaterials)} material(s)\n")
    return true
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
            // `eid` is structural; pointers are runtime references. The declaration decides
            // the rest: only a component with `save = true` reaches the file.
            if (comp.name == "eid" || comp.info.basicType == Type.tPointer) {
                continue
            }
            if (!EcsShouldSave(comp.name)) {
                continue
            }

            var cf_val = DecsComponentToCF(comp, row)
            if (cf_val == null) {
                print("[SCENE WARNING] Component '{comp.name}' of type '{comp.info.fullName}' is declared save = true but has no writer; skipped.\n")
                continue
            }
            cf_val.name = comp.name
            entity_obj[comp.name] = cf_val
        }

        // The hierarchy is authored as a local index, not as absolute entity ids. A root has
        // no parent, so it stores no `parentLocalId`.
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
        if (parent_local != SCENE_ROOT_PARENT) {
            var parent_cf = CF(parent_local)
            parent_cf.name = "parentLocalId"
            entity_obj["parentLocalId"] = parent_cf
        }

        var entity_cf = CF(entity_obj)
        entity_cf.typeName = "Entity"
        entities_list |> push(entity_cf)
    }

    active_scene.requireMeshs |> clear()
    active_scene.requireMaterials |> clear()

    // The scene stores authored references, not ids: a mesh guide and an optional path to an
    // authoritative `.mat`. The effective material (an override, or the mesh artifact
    // default) is what the runtime releases, so the header carries the resolved id.
    var material_refs : array<string>
    query() $(model : ModelRender) {
        if (model.mesh != 0ul && !active_scene.requireMeshs.contains(model.mesh)) {
            active_scene.requireMeshs |> push(model.mesh)
        }
        if (!empty(model.material) && !material_refs.contains(model.material)) {
            material_refs |> push(model.material)
        }
        let refs = Model::Resolve(model.mesh, model.material)
        if (refs.material != 0ul && !active_scene.requireMaterials.contains(refs.material)) {
            active_scene.requireMaterials |> push(refs.material)
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

//! Header of an already loaded scene: stem, path and the resource ids it needs. The caller
//! loads the scene first (`Loader::Load` with `AddScene`) and reads this from the cache; the
//! build preview requests every scene of a build once and then asks for their headers.
def public GetSceneHeader(path : string) : SceneHeader {
    let resolved = ScenePath(path)
    var header <- SceneHeader()
    header.name = Content::Stem(resolved)
    header.path = resolved

    let compiled = CachedTemplate(resolved)
    if (compiled == null) {
        print("[SCENE ERROR] Scene '{resolved}' is not loaded; request it before reading its header.\n")
        return <- header
    }

    clone(header.requireMeshs, compiled.deps.meshes)
    clone(header.requireMaterials, compiled.deps.materials)
    return <- header
}
