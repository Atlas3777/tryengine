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
//! ConfigFormat lookups. Only `Loader::Load` reads a prefab: the cache holds exactly the
//! prefabs a `LoaderRequest` asked for, and `Spawn` is a synchronous cache hit that returns
//! the root `EntityId`. A prefab is addressed by a path relative to `content/`
//! (`prefabl.prefab`) or by a full path; the extension is explicit.

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
require tryengine/EntitySet
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
//!
//! The compiled form itself is `EntitySet::EntitySetTemplate`: a scene compiles into the same
//! value, and creating entities from it is one shared routine. What a prefab adds is its
//! format rules (`localId`, one root, pre-order parents), checked in `CompilePrefab`.
typedef public PrefabTemplate = EntitySet::EntitySetTemplate

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
    var nodes : array<EntityNode>
    var strings : array<string>
    EntitySet::CollectStrings(document.root, strings)

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

        var node <- EntityNode()
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
    // components, so a spawned prefab is correctly placed in the same call without the
    // transform pass.
    EntitySet::BakeWorldTransforms(nodes)

    var name = from_CF(root_obj?["name"] ?? null, type<string>)
    if (empty(name)) {
        name = Content::Stem(document.path)
    }

    // Nodes with the same component set share an archetype, so spawn creates them as one bulk
    // row.
    var groups <- EntitySet::BuildGroups(nodes)

    var deps : Content::Dependencies
    clone(deps.meshes, document.deps.meshes)
    clone(deps.materials, document.deps.materials)
    clone(deps.textures, document.deps.textures)
    clone(deps.fonts, document.deps.fonts)

    return new EntitySet::EntitySetTemplate(
        path = document.path,
        name = name,
        strings <- strings,
        nodes <- nodes,
        groups <- groups,
        deps <- deps
    )
}

// ---------------------------------------------------------------------------
// AssetCache kind
// ---------------------------------------------------------------------------

var private g_values : table<string; PrefabTemplate?>

//! Starts loading one prefab document. False means the storage layer does not know the path.
def public Request(path : string) : bool {
    return AssetCache::Request("prefab", path)
}

//! One step of the prefab state machine: reads the text once, validates the shape and
//! compiles the template into the cache.
def public Advance(path : string) : AssetCache::AssetState {
    return AssetCache::Advance(g_values, path, Content::ItemsField(DocumentKind.Prefab),
                               @@CompilePrefab)
}

//! Abandons a prefab that is still loading; a compiled template stays cached.
def public Cancel(path : string) {
    AssetCache::Cancel(g_values, path)
}

//! Drops every compiled prefab, ready or failed.
def public Clear() {
    AssetCache::Clear(g_values, "prefab")
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

// One spawn for both entry points: resolve and reserve the models, then let the shared
// entity-set routine create the rows. The reservation is the entity's own reference, which
// outlives the loader's; a model that is not resident is a spawn error, because the frame
// loop never loads.
def private SpawnTemplate(data : PrefabTemplate?; origin : float3;
                          var out_ids : array<EntityId>) : EntityId {
    var model_refs : array<Model::ModelRefs>
    if (!EntitySet::ResolveModels(data, model_refs)) {
        return INVALID_ENTITY_ID
    }

    for (refs in model_refs) {
        if (refs.mesh != 0ul && !Model::Hold(refs)) {
            print("[PREFAB ERROR] prefab '{data.path}': mesh {refs.mesh:d} / material {refs.material:d} is not loaded; add the prefab to the LoaderRequest first\n")
            return INVALID_ENTITY_ID
        }
    }

    return EntitySet::CreateEntities(data, origin, model_refs, out_ids)
}

//! Spawns a compiled prefab: the spawned entities get their authored `Transform`, the baked
//! `_world` (already moved to `origin`) and the sibling graph, so they are correctly placed in
//! the same call and need no transform pass.
[profile]
def public SpawnPrefabTemplate(data : PrefabTemplate?; origin : float3) : EntityId {
    var ids : array<EntityId>
    return SpawnTemplate(data, origin, ids)
}

//! Same spawn, plus the created ids in node order: `out_ids[i]` is the entity of
//! `data.nodes[i]`. The editor's undo remaps the ids a deleted subtree was recorded with, and
//! it can only do that when it knows which new entity is which node.
def public SpawnPrefabTemplateIds(data : PrefabTemplate?; origin : float3;
                                  var out_ids : array<EntityId>) : EntityId {
    return SpawnTemplate(data, origin, out_ids)
}

//! Synchronous spawn from the prefab cache. The spawned entities get their authored
//! `Transform`, the baked `_world` (already moved to `origin`) and the sibling graph, so
//! they are correctly placed in the same call and need no transform pass. The prefab must
//! have been loaded by a `LoaderRequest` first; an unknown prefab
//! returns INVALID_ENTITY_ID.
//! Accepts a path relative to `content/` ("prefabl.prefab", "props/sword.prefab"), a
//! `content/...` path, or a repo-relative/absolute .prefab path. The extension is
//! required: nothing is appended.
def public Spawn(name_or_path : string; origin : float3 = float3(0.0f, 0.0f, 0.0f)) : EntityId {
    let data = GetPrefabTemplate(name_or_path)
    if (data == null) {
        print("[PREFAB ERROR] prefab '{name_or_path}' was not preloaded; add it to the LoaderRequest first\n")
        return INVALID_ENTITY_ID
    }
    return SpawnPrefabTemplate(data, origin)
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
