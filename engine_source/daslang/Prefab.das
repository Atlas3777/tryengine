options gen2
options relaxed_assign = false

module Prefab shared public

//! Prefab format, compiled cache and spawning.
//!
//! A prefab is a flat pre-order list of nodes. A node with a parent stores `parentLocalId`;
//! the root has no parent and stores nothing for it. DECS entity ids never reach the file:
//! runtime ids are assigned by `create_entity` during spawn. `first_child`/`prev`/`next` are
//! derived while spawning, so they are not data.
//!
//! Invariants checked here:
//!  * exactly one root: the first node, which stores no `parentLocalId`; writing the root
//!    parent id is an error, the format has no compatibility path for it;
//!  * `localId`s are unique, the root is first;
//!  * a parent exists and is stored before its child (pre-order);
//!  * `eid`, `relationship` and every other runtime-only component are forbidden.
//!
//! Node fields are plain ConfigFormat component names (`tag`, `model`, `transform`, ...).
//! Transforms are local to the parent. The compiled template also
//! carries the derived `_world` component, composed once at compile time, so spawn never
//! runs the transform pass: it only shifts every node by the spawn origin, which is exactly
//! how the root moves to the spawn point.
//!
//! Files stay ConfigFormat text. `tryengine/Content` parses the document and collects its
//! mesh/material dependencies; this module validates the prefab shape and compiles it into a
//! `PrefabTemplate` (resolved component values plus parent/sibling indices). The parsed
//! document is dropped after that, so repeated spawns do no I/O, no parsing and no
//! ConfigFormat lookups. `LoadPrefabFile` reads one prefab on demand: the game caches exactly
//! the prefabs a `LoaderRequest` asked for, the editor loads the selected one. `Spawn` is a
//! synchronous cache hit and returns the root `EntityId`. A prefab is addressed by a path
//! relative to `content/` (`prefabl.prefab`) or by a full path; the extension is explicit.

require strings
require daslib/async_boost
require daslib/decs
require daslib/decs_boost
require daslib/strings_boost
require tryengine/AssetCache
require tryengine/Assets
require tryengine/ConfigFormat
require tryengine/ConfigFormatBoost
require tryengine/Content
require tryengine/FileReader
require tryengine/Model
require tryengine/Profiling
require tryengine/Serializable
require tryengine/TransformSystem
require tryengine/EcsMacro
require tryengine/Types
require tryResources

let public PREFAB_VERSION : uint = 7u
let public PREFAB_ROOT_PARENT : uint = 4294967295u

// One node of a compiled prefab. `values` is the component map already resolved from
// ConfigFormat, so spawn neither parses fields nor dispatches them by name. `local` is the
// authored transform and `world` the composed one: both are already in `values`, and kept
// here so spawn moves the subtree by the origin without a lookup. Sibling links are
// precomputed as indices into `PrefabTemplate.nodes`.
struct public PrefabNode {
    local_id    : uint
    parent      : int
    first_child : int
    prev        : int
    next        : int
    local       : Transform = Transform()
    world       : Transform = Transform()
    values      : decs::ComponentMap
    // The authored appearance, repeated here because spawn may not query the const
    // `values`: `mesh` is the guide, `material` the optional override.
    model       : ModelRender = ModelRender()
}

//! Nodes that share one component set. DECS creates a whole archetype row at once, so a
//! prefab spawns one group per archetype instead of one entity at a time.
struct public PrefabGroup {
    //! Indices into `PrefabTemplate.nodes`, in creation order.
    nodes : array<int>
}

struct public PrefabTemplate {
    path    : string
    name    : string
    // `ComponentValue.data` is raw bytes: a component value that holds a string copies the
    // pointer and never increments its count, so the template owns every string it baked in.
    strings : array<string>
    nodes   : array<PrefabNode>
    groups  : array<PrefabGroup>
}

// Compiled prefabs live in `tryengine/AssetCache`, one typed record per canonical path.
// The template is self-contained: it holds resolved components and the strings they
// reference, so neither the parsed document nor a separate document cache is needed.

def private NodeLocalId(cf : StructField?) : uint {
    return from_CF(cf, type<uint>)
}

def private NodeParentLocalId(cf : StructField?) : uint {
    return from_CF(cf, type<uint>)
}

// ---------------------------------------------------------------------------
// Compilation: parsed document -> validated PrefabTemplate
// ---------------------------------------------------------------------------

//! Collects every string the document holds, so the compiled template can keep them alive.
//! `ComponentValue.data` is raw bytes: copying a `string` into it copies the pointer without
//! touching the count, so the array returned here is the owner the component bytes rely on.
def private CollectStrings(cf : StructField?; var out : array<string>) {
    if (cf == null) {
        return
    }

    assume value = cf.value
    if (value is _string) {
        out |> push(value as _string)
        return
    }
    if (value is _object) {
        assume obj = value as _object
        for (name in keys(obj)) {
            CollectStrings(obj?[name] ?? null, out)
        }
        return
    }
    if (value is _array) {
        assume arr = value as _array
        for (element in arr) {
            CollectStrings(element, out)
        }
    }
}

//! Validates a parsed document and compiles it into the runtime form: resolved component
//! values per node, the sibling graph and the archetype groups. The document is consumed and
//! its ConfigFormat tree is dropped, so the result owns everything it needs.
def public CompilePrefab(var document : Content::Document?) : PrefabTemplate? {
    if (document == null || document.root == null || !(document.root.value is _object)) {
        print("[PREFAB ERROR] document root is not an object\n")
        return null
    }

    assume root_obj = document.root.value as _object

    let version = from_CF(root_obj?["version"] ?? null, type<uint>)
    if (version != PREFAB_VERSION) {
        print("[PREFAB ERROR] unsupported version {version}, expected {PREFAB_VERSION}\n")
        return null
    }

    var nodes_cf = root_obj?["nodes"] ?? null
    if (nodes_cf == null || !(nodes_cf.value is _array)) {
        print("[PREFAB ERROR] 'nodes' is not an array\n")
        return null
    }
    assume nodes_arr = nodes_cf.value as _array

    var local_ids : table<uint; int>
    var nodes : array<PrefabNode>
    var strings : array<string>
    CollectStrings(document.root, strings)

    var roots = 0
    var root_index = -1

    let node_count = length(nodes_arr)
    for (node_index in range(node_count)) {
        var node_cf = nodes_arr[node_index]
        if (node_cf == null || !(node_cf.value is _object)) {
            print("[PREFAB ERROR] node #{node_index} is not an object\n")
            return null
        }

        assume node_obj = node_cf.value as _object
        let local_id = NodeLocalId(node_obj?["localId"] ?? null)
        if (local_id == PREFAB_ROOT_PARENT) {
            print("[PREFAB ERROR] node #{node_index}: localId {PREFAB_ROOT_PARENT} is reserved\n")
            return null
        }
        if (key_exists(local_ids, local_id)) {
            print("[PREFAB ERROR] node #{node_index}: duplicate localId {local_id}\n")
            return null
        }

        // A child names its parent by local id. The root names nothing: an explicit
        // `PREFAB_ROOT_PARENT` in the file is an error, not a second way to spell it.
        let parent_cf = node_obj?["parentLocalId"] ?? null
        var parent_local_id = PREFAB_ROOT_PARENT
        if (parent_cf != null) {
            parent_local_id = NodeParentLocalId(parent_cf)
            if (parent_local_id == PREFAB_ROOT_PARENT) {
                print("[PREFAB ERROR] node #{node_index}: the root must not store parentLocalId\n")
                return null
            }
        }

        var parent_index = -1
        if (parent_local_id == PREFAB_ROOT_PARENT) {
            roots++
            if (node_index != 0) {
                print("[PREFAB ERROR] node #{node_index}: the root must be the first node\n")
                return null
            }
            root_index = node_index
        } else {
            if (!key_exists(local_ids, parent_local_id)) {
                print(
                    "[PREFAB ERROR] node #{node_index}: parent {parent_local_id} must be stored before the child\n"
                )
                return null
            }
            parent_index = local_ids[parent_local_id]
        }

        var node <- PrefabNode()
        node.local_id = local_id
        node.parent = parent_index
        node.first_child = -1
        node.prev = -1
        node.next = -1

        // Resolve every authored field once, here. Spawn used to do this per entity and per
        // field: name lookup, type dispatch, `from_CF` and `make_component`. The defaults
        // come first so every node has an unlinked relationship and an identity transform
        // even when the file omits them.
        node.values |> set(RELATIONSHIP_FIELD, Relationship())
        node.values |> set(TRANSFORM_FIELD, Transform())
        for (field_name in keys(node_obj)) {
            if (field_name == "localId" || field_name == "parentLocalId") {
                continue
            }
            if (field_name == "eid") {
                print("[PREFAB ERROR] node #{node_index}: field '{field_name}' is forbidden in a prefab\n")
                return null
            }

            let cf = node_obj?[field_name] ?? null
            // Only components declared `save = true` may appear in a prefab; a runtime
            // component in the document is an authoring mistake, not a value to drop.
            if (!EcsShouldSave(field_name)) {
                print("[PREFAB ERROR] node #{node_index}: field '{field_name}' is not a saved component and is forbidden in a prefab\n")
                return null
            }
            if (!ApplyDecsConfigField(node.values, field_name, cf, cf != null ? cf.typeName : "")) {
                print("[PREFAB ERROR] node #{node_index}: field '{field_name}' has no supported value\n")
                return null
            }
        }

        node.model = decs::get(node.values, Model::MODEL_FIELD, ModelRender())

        nodes |> emplace(node)

        local_ids[local_id] = node_index
    }

    if (roots != 1 || root_index != 0) {
        print("[PREFAB ERROR] expected exactly one root, found {roots}\n")
        return null
    }

    // Derived relationship graph: only parents are stored on disk. The array is
    // pre-order, which is also sibling order, so the links follow from the indices.
    var last_child : table<int; int>
    for (node_index in range(1, node_count)) {
        let parent_index = nodes[node_index].parent
        if (key_exists(last_child, parent_index)) {
            let previous = last_child[parent_index]
            nodes[previous].next = node_index
            nodes[node_index].prev = previous
        } else {
            nodes[parent_index].first_child = node_index
        }
        last_child[parent_index] = node_index
    }

    // The derived world transform is baked here, once, and becomes part of the node's
    // components. The transform pass never adds `_world` to an existing entity (that moves it
    // to another archetype and copies its whole row), so a spawned prefab must be born with
    // it, and a spawn is then correctly placed in the same call. Nodes are pre-order, so a
    // parent is always composed before its children.
    for (node_index in range(node_count)) {
        nodes[node_index].local = decs::get(nodes[node_index].values, TRANSFORM_FIELD, Transform())

        let parent_index = nodes[node_index].parent
        if (parent_index < 0) {
            nodes[node_index].world = Transform(
                position = nodes[node_index].local.position,
                rotation = nodes[node_index].local.rotation,
                scale = nodes[node_index].local.scale
            )
        } else {
            nodes[node_index].world = ComposeWorld(nodes[parent_index].world, nodes[node_index].local)
        }

        nodes[node_index].values |> set(WORLD_FIELD, nodes[node_index].world)
    }

    var name = from_CF(root_obj?["name"] ?? null, type<string>)
    if (empty(name)) {
        name = Content::Stem(document.path)
    }

    // Nodes with the same component set share an archetype, so spawn creates them as one bulk
    // row. The key is the sorted component names the values already carry.
    var groups : array<PrefabGroup>
    var group_of_key : table<string; int>
    for (node_index in range(node_count)) {
        var key = ""
        for (cv in nodes[node_index].values) {
            key += cv.name
            key += "|"
        }

        var group_index = -1
        if (key_exists(group_of_key, key)) {
            group_index = group_of_key[key]
        } else {
            group_index = length(groups)
            group_of_key[key] = group_index
            var new_group : PrefabGroup
            groups |> emplace(new_group)
        }
        groups[group_index].nodes |> push(node_index)
    }

    return new PrefabTemplate(
        path = document.path,
        name = name,
        strings <- strings,
        nodes <- nodes,
        groups <- groups
    )
}

// ---------------------------------------------------------------------------
// AssetCache kind
// ---------------------------------------------------------------------------

var private g_values : table<string; PrefabTemplate?>

def private RequestAsset(path : string) : bool {
    return AssetCache::Request("prefab", path)
}

def private AdvanceAsset(path : string) : AssetCache::AssetState {
    return AssetCache::Advance(g_values, path, Content::ItemsField(DocumentKind.Prefab),
                               @@CompilePrefab)
}

def private AssetDeps(path : string) : Content::Dependencies {
    return <- AssetCache::Dependencies(path)
}

def private CancelAsset(path : string) {
    AssetCache::Cancel(g_values, path)
}

def private ClearAssets() {
    AssetCache::Clear(g_values, "prefab")
}

[init]
def private RegisterAssetKind() {
    AssetCache::RegisterKind("prefab", AssetCache::AssetKindOps(
        request = @@RequestAsset,
        advance = @@AdvanceAsset,
        deps = @@AssetDeps,
        expands = @@AssetCache::NoExpansion,
        cancel = @@CancelAsset,
        clear = @@ClearAssets
    ))
}

def public GetPrefabTemplate(name_or_path : string) : PrefabTemplate? {
    let path = Content::ResolvePath(DocumentKind.Prefab, name_or_path)
    if (AssetCache::State(path) != AssetCache::AssetState.Ready) {
        return null
    }
    return g_values[path]
}

def public IsPrefabCached(name_or_path : string) : bool {
    let path = Content::ResolvePath(DocumentKind.Prefab, name_or_path)
    return AssetCache::State(path) == AssetCache::AssetState.Ready
}

//! Drops the compiled prefab so the next load re-reads and re-validates the document.
def public InvalidatePrefab(name_or_path : string) {
    let path = Content::ResolvePath(DocumentKind.Prefab, name_or_path)
    AssetCache::Invalidate(g_values, path)
}

// ---------------------------------------------------------------------------
// Spawning
// ---------------------------------------------------------------------------

// Scratch of the bulk fill: `create_entities_from_cmp` reports ids through a block, and a
// block cannot carry an array back out (daslang captures arrays by move). Spawn runs on the
// script thread and never nests, so module-level scratch is enough.
var private g_spawn_ids : array<EntityId>
var private g_spawn_component_indices : array<int>
var private g_spawn_world_index : int
var private g_spawn_mesh_index : int
var private g_spawn_material_index : int

[profile]
def public SpawnPrefabTemplate(data : PrefabTemplate?; origin : float3) : EntityId {
    if (data == null) {
        return INVALID_ENTITY_ID
    }

    let entity_count = length(data.nodes)
    if (entity_count == 0 || data.nodes[0].parent != -1) {
        print("[PREFAB ERROR] compiled prefab '{data.path}' has no root\n")
        return INVALID_ENTITY_ID
    }
    if (empty(data.groups)) {
        print("[PREFAB ERROR] prefab '{data.path}' has no archetype groups; compile it with CompilePrefab\n")
        return INVALID_ENTITY_ID
    }

    // Appearance is resolved and reserved before anything is created: the frame loop never
    // loads, so a model that is not resident is a spawn error. The reservation is the
    // entity's own reference, which outlives the loader's.
    var model_refs : array<Model::ModelRefs>
    resize(model_refs, entity_count)
    for (node_index in range(entity_count)) {
        let model = data.nodes[node_index].model
        let refs = Model::Resolve(model.mesh, model.material)
        if (!refs.ok) {
            print("[PREFAB ERROR] prefab '{data.path}' node {node_index}: material '{model.material}' is not an existing authored .mat path; a derived material is reached through the mesh default\n")
            return INVALID_ENTITY_ID
        }
        if (model.mesh != 0ul && !Model::Reserve(refs)) {
            print("[PREFAB ERROR] prefab '{data.path}': mesh {model.mesh:d} / material {refs.material:d} is not loaded; add the prefab to the LoaderRequest first\n")
            return INVALID_ENTITY_ID
        }
        model_refs[node_index] = refs
    }

    resize(g_spawn_ids, entity_count)

    // One archetype row per group: DECS resolves the archetype and grows the component arrays
    // once per group, then every entity is written straight into storage.
    for (group_index in range(length(data.groups))) {
        let first_node = data.groups[group_index].nodes[0]

        var prototype : decs::ComponentMap
        for (cv in data.nodes[first_node].values) {
            prototype |> push(cv)
        }
        // The draw pointers are part of every spawned entity from birth: they are written
        // below with `set_direct_at`, so no entity changes archetype after creation.
        var no_mesh : TryMesh? = null
        var no_material : TryMat? = null
        prototype |> set(Model::MESH_POINTER_FIELD, no_mesh)
        prototype |> set(Model::MATERIAL_POINTER_FIELD, no_material)
        g_spawn_component_indices |> clear()

        let group_size = length(data.groups[group_index].nodes)
        create_entities_from_cmp(group_size, prototype) $(var arch : Archetype; idx : int; eid : EntityId; i : int) {
            let node_index = data.groups[group_index].nodes[i]
            if (empty(g_spawn_component_indices)) {
                g_spawn_component_indices |> push(arch.eidIndex)
                for (cv in data.nodes[node_index].values) {
                    g_spawn_component_indices |> push(decs::find_component_index(arch, cv.name))
                }
                g_spawn_world_index = decs::find_component_index(arch, WORLD_FIELD)
                g_spawn_mesh_index = decs::find_component_index(arch, Model::MESH_POINTER_FIELD)
                g_spawn_material_index = decs::find_component_index(arch, Model::MATERIAL_POINTER_FIELD)
            }

            decs::set_direct_at(arch, g_spawn_component_indices[0], idx, eid)

            // The prepared bytes are already the component layout, so they are copied as is.
            var slot = 1
            for (cv in data.nodes[node_index].values) {
                let component_index = g_spawn_component_indices[slot]
                slot++
                unsafe {
                    memcpy(
                        addr(arch.components[component_index].data[idx * arch.components[component_index].stride]),
                        addr(cv.data),
                        arch.components[component_index].stride
                    )
                }
            }

            let refs = model_refs[node_index]
            if (refs.mesh != 0ul) {
                var mesh_ptr := Model::MeshPointer(refs)
                if (mesh_ptr != null) {
                    decs::set_direct_at(arch, g_spawn_mesh_index, idx, mesh_ptr)
                }
            }
            if (refs.material != 0ul) {
                var material_ptr := Model::MaterialPointer(refs)
                if (material_ptr != null) {
                    decs::set_direct_at(arch, g_spawn_material_index, idx, material_ptr)
                }
            }

            // The baked world transform contains the authored root position; the spawn point
            // translates the whole subtree, so every node moves by `origin`.
            decs::set_direct_at(
                arch,
                g_spawn_world_index,
                idx,
                Transform(
                    position = data.nodes[node_index].world.position + origin,
                    rotation = data.nodes[node_index].world.rotation,
                    scale = data.nodes[node_index].world.scale
                )
            )
            g_spawn_ids[node_index] = eid
        }
    }

    commit()

    // Only the root's local transform moves to the spawn point; every node's world position
    // was already shifted in the fill above. The root may live in another group than the last
    // one spawned, so its component index is resolved from its own archetype.
    let root_record = decsState.entityLookup[int(g_spawn_ids[0].id)]
    let root_arch_index = int(decsState.archetypeLookup[root_record.archetype]) - 1
    decs::set_direct_at(
        decsState.allArchetypes[root_arch_index],
        decs::find_component_index(decsState.allArchetypes[root_arch_index], TRANSFORM_FIELD),
        int(root_record.index),
        Transform(
            position = origin + data.nodes[0].local.position,
            rotation = data.nodes[0].local.rotation,
            scale = data.nodes[0].local.scale
        )
    )

    // The links are known only once the ids exist, and `update_entity` would copy a whole
    // entity row to change one component: write it straight into archetype storage, with
    // the component index resolved once per archetype.
    var relationship_index : table<int; int>
    for (entity_index in range(entity_count)) {
        let eid = g_spawn_ids[entity_index]
        let record = decsState.entityLookup[int(eid.id)]
        let arch_index = int(decsState.archetypeLookup[record.archetype]) - 1

        let parent_index = data.nodes[entity_index].parent
        let first_child = data.nodes[entity_index].first_child
        let prev_index = data.nodes[entity_index].prev
        let next_index = data.nodes[entity_index].next

        if (!key_exists(relationship_index, arch_index)) {
            relationship_index[arch_index] = decs::find_component_index(
                decsState.allArchetypes[arch_index], RELATIONSHIP_FIELD)
        }
        decs::set_direct_at(
            decsState.allArchetypes[arch_index],
            relationship_index[arch_index],
            int(record.index),
            Relationship(
                parent = parent_index >= 0 ? g_spawn_ids[parent_index] : INVALID_ENTITY_ID,
                first_child = first_child >= 0 ? g_spawn_ids[first_child] : INVALID_ENTITY_ID,
                prev = prev_index >= 0 ? g_spawn_ids[prev_index] : INVALID_ENTITY_ID,
                next = next_index >= 0 ? g_spawn_ids[next_index] : INVALID_ENTITY_ID
            )
        )
    }

    return g_spawn_ids[0]
}

//! Same spawn, plus the created ids in node order: `out_ids[i]` is the entity of
//! `data.nodes[i]`. The editor's undo remaps the ids a deleted subtree was recorded with, and
//! it can only do that when it knows which new entity is which node.
def public SpawnPrefabTemplateIds(data : PrefabTemplate?; origin : float3;
                                  var out_ids : array<EntityId>) : EntityId {
    let root = SpawnPrefabTemplate(data, origin)
    clone(out_ids, g_spawn_ids)
    return root
}

//! Synchronous spawn from the prefab cache. The spawned entities get their authored
//! `Transform`, the baked `_world` (already moved to `origin`) and the sibling graph, so
//! they are correctly placed in the same call and need no transform pass. The prefab must
//! have been loaded by a `LoaderRequest` or by `LoadPrefabFile` first; an unknown prefab
//! returns INVALID_ENTITY_ID.
//! Accepts a path relative to `content/` ("prefabl.prefab", "props/sword.prefab"), a
//! `content/...` path, or a repo-relative/absolute .prefab path. The extension is
//! required: nothing is appended.
def public Spawn(name_or_path : string; origin : float3 = float3(0.0f, 0.0f, 0.0f)) : EntityId {
    let data = GetPrefabTemplate(name_or_path)
    if (data == null) {
        print("[PREFAB ERROR] prefab '{name_or_path}' was not preloaded; add it to the LoaderRequest or call Prefab::LoadPrefabFile() first\n")
        return INVALID_ENTITY_ID
    }
    return SpawnPrefabTemplate(data, origin)
}

// ---------------------------------------------------------------------------
// File I/O
// ---------------------------------------------------------------------------

//! Reads, validates and caches one .prefab file through the content layer.
[async]
def public LoadPrefabFile(path : string) : bool {
    let canonical = Content::ResolvePath(DocumentKind.Prefab, path)
    AssetCache::Refresh(g_values, "prefab", canonical)

    while (AssetCache::Advance(g_values, canonical, Content::ItemsField(DocumentKind.Prefab),
                               @@CompilePrefab) == AssetCache::AssetState.Loading) {
        await_next_frame()
    }

    let loaded = AssetCache::State(canonical) == AssetCache::AssetState.Ready
    if (!loaded) {
        print("[PREFAB ERROR] file '{canonical}' is missing, empty or not a valid prefab\n")
    }
    yield loaded
}

//! Reads and caches a .prefab file, then spawns it. The editor uses this for drops,
//! so an edited file is re-read instead of using a stale cache entry.
[async]
def public SpawnPrefab(path : string; origin : float3) : EntityId {
    let loaded : bool <- await(LoadPrefabFile(path))
    if (!loaded) {
        yield INVALID_ENTITY_ID
    }

    // Drop-in spawn has no Loader behind it, so it waits for the resources itself; the sync
    // `Spawn` above requires them to be resident already.
    let canonical = Content::ResolvePath(DocumentKind.Prefab, path)
    var deps <- AssetCache::Dependencies(canonical)
    var meshes : array<uint64>
    var materials : array<uint64>
    for (mesh in deps.meshes) {
        meshes |> push(mesh)
    }
    for (material in deps.materials) {
        materials |> push(material)
    }

    let resources_ready : bool <- await(Model::AwaitDeps(meshes, materials))
    if (!resources_ready) {
        print("[PREFAB ERROR] '{canonical}': meshes or materials failed to load\n")
        yield INVALID_ENTITY_ID
    }

    yield SpawnPrefabTemplate(GetPrefabTemplate(path), origin)
}

// ---------------------------------------------------------------------------
// Saving authored prefabs
// ---------------------------------------------------------------------------

// Same lookup as editor/Inspector: eid -> archetype row. Kept here so authoring does
// not depend on editor types.
def private GetEntityLocation(eid : EntityId; var out_arch_idx : int&; var out_index : int64&) : bool {
    if (eid.id >= uint(length(decsState.entityLookup)) ||
        decsState.entityLookup[int(eid.id)].generation != eid.generation) {
        return false
    }

    let rec = decsState.entityLookup[int(eid.id)]
    if (!key_exists(decsState.archetypeLookup, rec.archetype)) {
        return false
    }

    let found = decsState.archetypeLookup[rec.archetype]
    if (found == 0) {
        return false
    }

    out_arch_idx = found - 1
    out_index = rec.index
    return true
}

def private CollectSubtree(eid : EntityId;
                           parent_local : uint;
                           var order : array<EntityId>;
                           var parents : array<uint>;
                           var visited : table<uint; bool>) {
    if (eid == INVALID_ENTITY_ID || key_exists(visited, eid.id)) {
        return
    }
    visited[eid.id] = true

    let local_id = uint(length(order))
    order |> push(eid)
    parents |> push(parent_local)

    var child = INVALID_ENTITY_ID
    query(eid) $(relationship : Relationship) {
        child = relationship.first_child
    }

    while (child != INVALID_ENTITY_ID) {
        CollectSubtree(child, local_id, order, parents, visited)

        var next = INVALID_ENTITY_ID
        query(child) $(relationship : Relationship) {
            next = relationship.next
        }
        child = next
    }
}

//! Serializes the selected entity and its subtree into ConfigFormat prefab text.
//! The root position is rebased to the origin (rotation/scale are kept), so spawning
//! at origin places the prefab exactly where it was spawned from. Returns "" on error.
def public BuildPrefabTextFromEntity(eid : EntityId; name : string) : string {
    var probe_arch = -1
    var probe_index : int64 = 0l
    if (!GetEntityLocation(eid, probe_arch, probe_index)) {
        print("[PREFAB ERROR] cannot save: entity {eid.id} is not alive\n")
        return ""
    }

    var order : array<EntityId>
    var parents : array<uint>
    var visited : table<uint; bool>
    CollectSubtree(eid, PREFAB_ROOT_PARENT, order, parents, visited)
    if (empty(order)) {
        return ""
    }

    var nodes : array<StructField?>

    for (node_index in range(length(order))) {
        var arch_idx = -1
        var rec_index : int64 = 0l
        if (!GetEntityLocation(order[node_index], arch_idx, rec_index)) {
            print("[PREFAB ERROR] cannot save: entity {order[node_index].id} disappeared\n")
            return ""
        }

        var node_obj : table<string; StructField?>
        for (comp in decsState.allArchetypes[arch_idx].components) {
            if (comp.name == "eid" || comp.info.basicType == Type.tPointer) {
                continue
            }
            if (!EcsShouldSave(comp.name)) {
                continue
            }

            var cf_val = DecsComponentToCF(comp, rec_index)
            if (cf_val == null) {
                print("[PREFAB WARNING] component '{comp.name}' is declared save = true but has no writer; skipped\n")
                continue
            }
            cf_val.name = comp.name
            node_obj[comp.name] = cf_val
        }

        // The root position is rebased to the origin, so spawning at origin puts the prefab
        // where it was; rotation and scale are kept as authored.
        if (node_index == 0) {
            var root_local = Transform()
            let ci = decs::find_component_index(decsState.allArchetypes[arch_idx], TRANSFORM_FIELD)
            unsafe {
                let component & = decsState.allArchetypes[arch_idx].components[ci]
                memcpy(addr(root_local), addr(component.data[rec_index * int64(component.stride)]),
                       typeinfo sizeof(root_local))
            }
            root_local.position = float3(0.0f, 0.0f, 0.0f)
            unsafe {
                var cf_local = WriteValue(type<Transform>, addr(root_local))
                cf_local.name = TRANSFORM_FIELD
                node_obj[TRANSFORM_FIELD] = cf_local
            }
        }

        var local_id_cf = CF(node_index)
        local_id_cf.name = "localId"
        node_obj["localId"] = local_id_cf

        // A child names its parent by local index. The root has none, so nothing is stored.
        if (parents[node_index] != PREFAB_ROOT_PARENT) {
            var parent_id_cf = CF(parents[node_index])
            parent_id_cf.name = "parentLocalId"
            node_obj["parentLocalId"] = parent_id_cf
        }

        var node_cf = CF(node_obj)
        node_cf.typeName = "PrefabNode"
        nodes |> push(node_cf)
    }

    var root_obj : table<string; StructField?>

    var version_cf = CF(uint(PREFAB_VERSION))
    version_cf.name = "version"
    root_obj["version"] = version_cf

    var name_cf = CF(name)
    name_cf.name = "name"
    root_obj["name"] = name_cf

    var nodes_cf = CF(nodes)
    nodes_cf.typeName = "PrefabNode[]"
    nodes_cf.name = "nodes"
    root_obj["nodes"] = nodes_cf

    // Building the text is also the editor's copy/undo snapshot, so it stays silent: only
    // the caller that actually saves a file reports.
    return write_config(CF(root_obj))
}

//! Serializes the selected entity subtree and overwrites `path`.
//! The saved text is parsed and cached immediately, so Spawn() sees the new version.
[async]
def public SavePrefabFromEntity(eid : EntityId; path : string) : bool {
    let canonical = Content::ResolvePath(DocumentKind.Prefab, path)
    if (!ends_with(canonical, ".prefab")) {
        print("[PREFAB ERROR] save path must end with .prefab: '{path}'\n")
        yield false
    }

    let name = Content::Stem(canonical)
    let text = BuildPrefabTextFromEntity(eid, name)
    if (empty(text)) {
        yield false
    }

    // The file manager creates parent directories itself; scripts never call the
    // filesystem directly (editor-only operation).
    let written : bool <- await(FileReader::WriteFile(canonical, text))
    if (!written) {
        print("[PREFAB ERROR] failed to write '{canonical}'\n")
        yield false
    }

    var document = Content::ParseFor(DocumentKind.Prefab, canonical, text)
    if (document == null) {
        yield false
    }

    var data = CompilePrefab(document)
    if (data == null) {
        yield false
    }

    AssetCache::Store(g_values, "prefab", canonical, data, document.deps)
    print("[PREFAB] wrote '{canonical}'\n")
    yield true
}
