options gen2
options relaxed_assign = false

module Prefab shared public

//! Prefab format, compiled cache and spawning.
//!
//! A prefab is a flat pre-order list of entities. It stores only `parentLocalId`,
//! never DECS entity ids: runtime ids are assigned by `create_entity` during spawn.
//! `first_child`/`prev`/`next` are derived while spawning, so they are not data.
//!
//! Invariants checked here:
//!  * exactly one root (`parentLocalId == 0xFFFFFFFF`);
//!  * `localId`s are unique, the root is first;
//!  * a parent exists and is stored before its child (pre-order);
//!  * `eid` and `Relationship_*` fields are forbidden.
//!
//! Entity fields are plain ConfigFormat component names (`tag`, `meshGuid`,
//! `Transform_position`, `materialRef`, ...). Transforms are local to the parent;
//! spawn writes them to the authored Transform and UpdateWorldTransforms derives
//! the world transform from the hierarchy. The spawn origin is added to the root only.
//!
//! Files stay ConfigFormat text. `tryengine/Content` parses the document and collects its
//! mesh/material refs; this module validates the prefab shape and keeps a compiled
//! `PrefabTemplate` (entity configs with parent/sibling indices), so repeated spawns do no I/O
//! and no parsing. `LoadPrefabFile` reads one prefab on demand: the game caches exactly the
//! prefabs a `LoaderRequest` asked for, the editor loads the selected one. `Spawn` is a
//! synchronous cache hit and returns the root `EntityId`. A prefab is addressed by a path
//! relative to `content/` (`prefabl.prefab`) or by a full path; the extension is explicit.

require strings
require daslib/async_boost
require daslib/decs
require daslib/decs_boost
require daslib/strings_boost
require tryengine/Assets
require tryengine/ConfigFormat
require tryengine/ConfigFormatBoost
require tryengine/Content
require tryengine/FileReader
require tryengine/Profiling
require tryengine/Serializable
require tryengine/TransformSystem
require tryengine/Types

let public PREFAB_VERSION : uint = 1u
let public PREFAB_ROOT_PARENT : uint = 4294967295u

// One node of a compiled prefab. `config` points into the parsed ConfigFormat tree;
// the root pointer in `PrefabTemplate` keeps that tree alive. No tables are copied per
// spawn, and the sibling links are precomputed as indices into `PrefabTemplate.nodes`.
struct public PrefabNode {
    local_id    : uint
    parent      : int
    first_child : int
    prev        : int
    next        : int
    config      : StructField?
}

struct public PrefabTemplate {
    path  : string
    name  : string
    root  : StructField?  // owns the parsed tree referenced by `nodes[*].config`
    nodes : array<PrefabNode>
}

// Canonical path -> compiled prefab. Paths are relative to the repository root and rooted
// under game/content.
var private g_prefabs : table<string; PrefabTemplate?>

def private IsReservedEntityField(name : string) : bool {
    return name == "eid" || starts_with(name, "Relationship_")
}

def private EntityLocalId(cf : StructField?) : uint {
    return from_CF(cf, type<uint>)
}

def private EntityParentLocalId(cf : StructField?) : uint {
    return from_CF(cf, type<uint>)
}

// Local TRS of one prefab entity. Missing components are identity, so the array
// may describe a pure grouping node.
def private GetLocalTransform(entity_obj : table<string; StructField?> const) : Transform {
    var transform : Transform

    let position_cf = entity_obj?["Transform_position"] ?? null
    if (position_cf != null && position_cf.value is _float3) {
        transform.position = position_cf.value as _float3
    }

    let rotation_cf = entity_obj?["Transform_rotation"] ?? null
    if (rotation_cf != null && rotation_cf.value is _float4) {
        transform.rotation = rotation_cf.value as _float4
    }

    let scale_cf = entity_obj?["Transform_scale"] ?? null
    if (scale_cf != null && scale_cf.value is _float3) {
        transform.scale = scale_cf.value as _float3
    }

    return <- transform
}

// ---------------------------------------------------------------------------
// Compilation: parsed document -> validated PrefabTemplate
// ---------------------------------------------------------------------------

def private CompilePrefab(var document : Content::Document?) : PrefabTemplate? {
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

    let config_type = from_CF(root_obj?["configType"] ?? null, type<string>)
    let config_row = from_CF(root_obj?["configRow"] ?? null, type<string>)
    if (!empty(config_type) && empty(config_row)) {
        print("[PREFAB ERROR] configRow is required when configType is set ('{config_type}')\n")
        return null
    }

    var entities_cf = root_obj?["entities"] ?? null
    if (entities_cf == null || !(entities_cf.value is _array)) {
        print("[PREFAB ERROR] 'entities' is not an array\n")
        return null
    }
    assume entities_arr = entities_cf.value as _array

    var local_ids : table<uint; int>
    var nodes : array<PrefabNode>

    var roots = 0
    var root_index = -1

    let entity_count = length(entities_arr)
    for (entity_index in range(entity_count)) {
        var entity_cf = entities_arr[entity_index]
        if (entity_cf == null || !(entity_cf.value is _object)) {
            print("[PREFAB ERROR] entity #{entity_index} is not an object\n")
            return null
        }

        assume entity_obj = entity_cf.value as _object
        let local_id = EntityLocalId(entity_obj?["localId"] ?? null)
        if (local_id == PREFAB_ROOT_PARENT) {
            print("[PREFAB ERROR] entity #{entity_index}: localId {PREFAB_ROOT_PARENT} is reserved\n")
            return null
        }
        if (key_exists(local_ids, local_id)) {
            print("[PREFAB ERROR] entity #{entity_index}: duplicate localId {local_id}\n")
            return null
        }

        let parent_cf = entity_obj?["parentLocalId"] ?? null
        if (parent_cf == null) {
            print("[PREFAB ERROR] entity #{entity_index}: parentLocalId is required\n")
            return null
        }
        let parent_local_id = EntityParentLocalId(parent_cf)

        var parent_index = -1
        if (parent_local_id == PREFAB_ROOT_PARENT) {
            roots++
            if (entity_index != 0) {
                print("[PREFAB ERROR] entity #{entity_index}: the root must be the first entity\n")
                return null
            }
            root_index = entity_index
        } else {
            if (!key_exists(local_ids, parent_local_id)) {
                print(
                    "[PREFAB ERROR] entity #{entity_index}: parent {parent_local_id} must be stored before the child\n"
                )
                return null
            }
            parent_index = local_ids[parent_local_id]
        }

        for (field_name in keys(entity_obj)) {
            if (field_name == "localId" || field_name == "parentLocalId") {
                continue
            }
            if (IsReservedEntityField(field_name)) {
                print("[PREFAB ERROR] entity #{entity_index}: field '{field_name}' is forbidden in a prefab\n")
                return null
            }
        }

        var node : PrefabNode
        node.local_id = local_id
        node.parent = parent_index
        node.first_child = -1
        node.prev = -1
        node.next = -1
        node.config = entity_cf
        nodes |> push(node)

        local_ids[local_id] = entity_index
    }

    if (roots != 1 || root_index != 0) {
        print("[PREFAB ERROR] expected exactly one root, found {roots}\n")
        return null
    }

    // Derived relationship graph: only parents are stored on disk. The array is
    // pre-order, which is also sibling order, so the links follow from the indices.
    var last_child : table<int; int>
    for (entity_index in range(1, entity_count)) {
        let parent_index = nodes[entity_index].parent
        if (key_exists(last_child, parent_index)) {
            let previous = last_child[parent_index]
            nodes[previous].next = entity_index
            nodes[entity_index].prev = previous
        } else {
            nodes[parent_index].first_child = entity_index
        }
        last_child[parent_index] = entity_index
    }

    var name = from_CF(root_obj?["name"] ?? null, type<string>)
    if (empty(name)) {
        name = Content::Stem(document.path)
    }

    return new PrefabTemplate(path = document.path, name = name, root = document.root, nodes <- nodes)
}

// ---------------------------------------------------------------------------
// Cache
// ---------------------------------------------------------------------------

def private StorePrefab(var data : PrefabTemplate?) {
    if (data == null) {
        return
    }
    g_prefabs[data.path] = data
}

//! Validates a parsed prefab document and stores the compiled form. Does no file I/O.
def public CachePrefabDocument(var document : Content::Document?) : bool {
    var data = CompilePrefab(document)
    if (data == null) {
        return false
    }
    StorePrefab(data)
    return true
}

// Canonical content path of a cached prefab, or "" when it is not cached.
def public ResolvePrefabPath(name_or_path : string) : string {
    let path = Content::ResolvePath(ContentKind.Prefab, name_or_path)
    if (empty(path) || !key_exists(g_prefabs, path)) {
        return ""
    }
    return path
}

def public GetPrefabTemplate(name_or_path : string) : PrefabTemplate? {
    let path = ResolvePrefabPath(name_or_path)
    if (empty(path)) {
        return null
    }
    return g_prefabs[path]
}

def public IsPrefabCached(name_or_path : string) : bool {
    return !empty(ResolvePrefabPath(name_or_path))
}

//! Drops the compiled prefab so the next load re-reads and re-validates the document.
def public InvalidatePrefab(name_or_path : string) {
    let path = Content::ResolvePath(ContentKind.Prefab, name_or_path)
    if (!empty(path)) {
        erase(g_prefabs, path)
    }
}

// ---------------------------------------------------------------------------
// Spawning
// ---------------------------------------------------------------------------

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

    var entity_ids : array<EntityId>
    resize(entity_ids, entity_count)

    for (entity_index in range(entity_count)) {
        let node = data.nodes[entity_index]
        assume entity_obj = node.config.value as _object

        // The prefab stores local TRS; only the root is moved to the spawn point.
        // Children keep their authored local transform, and UpdateWorldTransforms
        // derives the world transform from the Relationship hierarchy.
        var local_transform = GetLocalTransform(entity_obj)
        if (entity_index == 0) {
            local_transform.position = origin + local_transform.position
        }

        let eid = create_entity() @(eid, cmp) {
            apply_decs_template(cmp, Relationship())
            apply_decs_template(cmp, local_transform)

            for (field_name in keys(entity_obj)) {
                if (field_name == "localId" || field_name == "parentLocalId" || starts_with(field_name, "Transform_")) {
                    continue
                }

                let cf = entity_obj?[field_name] ?? null
                if (!ApplyDecsConfigField(cmp, field_name, cf, cf != null ? cf.typeName : "")) {
                    print(
                        "[PREFAB WARNING] entity {node.local_id}: field '{field_name}' was not applied\n"
                    )
                }
            }
        }
        entity_ids[entity_index] = eid
    }

    for (entity_index in range(entity_count)) {
        let node = data.nodes[entity_index]
        let parent_eid = node.parent >= 0 ? entity_ids[node.parent] : INVALID_ENTITY_ID
        let first_eid = node.first_child >= 0 ? entity_ids[node.first_child] : INVALID_ENTITY_ID
        let prev_eid = node.prev >= 0 ? entity_ids[node.prev] : INVALID_ENTITY_ID
        let next_eid = node.next >= 0 ? entity_ids[node.next] : INVALID_ENTITY_ID

        update_entity(entity_ids[entity_index]) @(eid, cmp) {
            apply_decs_template(cmp, Relationship(
                parent = parent_eid,
                first_child = first_eid,
                prev = prev_eid,
                next = next_eid
            ))
        }
    }

    commit()
    UpdateWorldTransforms()
    return entity_ids[0]
}

//! Synchronous spawn from the prefab cache. The prefab must have been loaded by a
//! `LoaderRequest` or by `LoadPrefabFile` first; an unknown prefab returns
//! INVALID_ENTITY_ID.
//! Accepts a path relative to `content/` ("prefabl.prefab", "props/sword.prefab"), a
//! `content/...` path, or a repo-relative/absolute .prefab path. The extension is
//! required: nothing is appended.
def public Spawn(name_or_path : string; origin : float3 = float3(0.0f, 0.0f, 0.0f)) : EntityId {
    let path = ResolvePrefabPath(name_or_path)
    if (empty(path)) {
        print("[PREFAB ERROR] prefab '{name_or_path}' was not preloaded; add it to the LoaderRequest or call Prefab::LoadPrefabFile() first\n")
        return INVALID_ENTITY_ID
    }
    return SpawnPrefabTemplate(g_prefabs[path], origin)
}

// ---------------------------------------------------------------------------
// File I/O
// ---------------------------------------------------------------------------

//! Reads, validates and caches one .prefab file through the content layer.
[async]
def public LoadPrefabFile(path : string) : bool {
    let canonical = Content::ResolvePath(ContentKind.Prefab, path)
    let text : string <- await(Assets::ReadText(canonical))
    if (empty(text)) {
        print("[PREFAB ERROR] file '{canonical}' is missing or empty\n")
        yield false
    }

    var document = Content::Cache(ContentKind.Prefab, canonical, text)
    yield CachePrefabDocument(document)
}

//! Reads and caches a .prefab file, then spawns it. The editor uses this for drops,
//! so an edited file is re-read instead of using a stale cache entry.
[async]
def public SpawnPrefab(path : string; origin : float3) : EntityId {
    let loaded : bool <- await(LoadPrefabFile(path))
    if (!loaded) {
        yield INVALID_ENTITY_ID
    }

    let canonical = Content::ResolvePath(ContentKind.Prefab, path)
    yield SpawnPrefabTemplate(g_prefabs[canonical], origin)
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

// Runtime-only components and the derived hierarchy are never authored into a prefab.
def private IsSkippedPrefabComponent(name : string) : bool {
    return IsReservedEntityField(name) || starts_with(name, "_") || name == "eid" || name == "editorCameraTag"
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
    query(eid) $(rel : Relationship) {
        child = rel.first_child
    }

    while (child != INVALID_ENTITY_ID) {
        CollectSubtree(child, local_id, order, parents, visited)

        var next = INVALID_ENTITY_ID
        query(child) $(rel : Relationship) {
            next = rel.next
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

    var entities : array<StructField?>
    var skipped_components = 0

    for (entity_index in range(length(order))) {
        var arch_idx = -1
        var rec_index : int64 = 0l
        if (!GetEntityLocation(order[entity_index], arch_idx, rec_index)) {
            print("[PREFAB ERROR] cannot save: entity {order[entity_index].id} disappeared\n")
            return ""
        }

        var entity_obj : table<string; StructField?>
        for (comp in decsState.allArchetypes[arch_idx].components) {
            if (comp.info.basicType == Type.tPointer) {
                continue
            }
            if (IsSkippedPrefabComponent(comp.name)) {
                continue
            }

            var cf_val = DecsComponentToCF(comp, rec_index)
            if (cf_val == null) {
                print("[PREFAB WARNING] component '{comp.name}' is not serializable; skipped\n")
                skipped_components++
                continue
            }
            if (entity_index == 0 && comp.name == "Transform_position") {
                cf_val = CF(float3(0.0f, 0.0f, 0.0f))
            }
            cf_val.name = comp.name
            entity_obj[comp.name] = cf_val
        }

        var local_id_cf = CF(entity_index)
        local_id_cf.name = "localId"
        entity_obj["localId"] = local_id_cf

        var parent_id_cf = CF(parents[entity_index])
        parent_id_cf.name = "parentLocalId"
        entity_obj["parentLocalId"] = parent_id_cf

        var entity_cf = CF(entity_obj)
        entity_cf.typeName = "PrefabNode"
        entities |> push(entity_cf)
    }

    var root_obj : table<string; StructField?>

    var version_cf = CF(uint(PREFAB_VERSION))
    version_cf.name = "version"
    root_obj["version"] = version_cf

    var name_cf = CF(name)
    name_cf.name = "name"
    root_obj["name"] = name_cf

    var config_type_cf = CF("")
    config_type_cf.name = "configType"
    root_obj["configType"] = config_type_cf

    var config_row_cf = CF("")
    config_row_cf.name = "configRow"
    root_obj["configRow"] = config_row_cf

    var entities_cf = CF(entities)
    entities_cf.typeName = "PrefabNode[]"
    entities_cf.name = "entities"
    root_obj["entities"] = entities_cf

    let text = write_config(CF(root_obj))
    print("[PREFAB] saved '{name}': {length(order)} entities, skipped {skipped_components} components\n")
    return text
}

//! Serializes the selected entity subtree and overwrites `path`.
//! The saved text is parsed and cached immediately, so Spawn() sees the new version.
[async]
def public SavePrefabFromEntity(eid : EntityId; path : string) : bool {
    let canonical = Content::ResolvePath(ContentKind.Prefab, path)
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

    var document = Content::Cache(ContentKind.Prefab, canonical, text)
    let cached = CachePrefabDocument(document)
    print("[PREFAB] wrote '{canonical}'\n")
    yield cached
}
