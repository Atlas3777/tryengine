options gen2
options relaxed_assign = false

module Prefab shared public

//! Prefab format, cache and spawning.
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
//! Files stay ConfigFormat text. The cache keeps a compiled, validated `PrefabData`
//! (entity configs with parent/sibling indices), so repeated spawns do no I/O and no
//! parsing. `RefreshContent` fills the cache from the content manifest (editor: scanned
//! trees, game: pak manifest) through `tryengine/Assets`; `Spawn` is a synchronous cache
//! hit and returns the root `EntityId`.

require strings
require math
require daslib/async_boost
require daslib/decs
require daslib/decs_boost
require daslib/strings_boost
require tryengine/Assets
require tryengine/FileReader
require tryengine/ConfigFormat
require tryengine/Profiling
require tryengine/ConfigFormatBoost
require tryengine/Serializable
require tryengine/TransformSystem
require tryengine/Types
require tryResources

let public PREFAB_VERSION : uint = 1u
let public PREFAB_ROOT_PARENT : uint = 4294967295u
let public PREFAB_CONTENT_ROOT : string = "game/content"

// One entity of a compiled prefab. `config` points into the parsed ConfigFormat tree;
// the root pointer in `PrefabData` keeps that tree alive. No tables are copied per
// spawn, and the sibling links are precomputed as indices into `PrefabData.entities`.
struct public PrefabEntity {
    local_id    : uint
    parent      : int
    first_child : int
    prev        : int
    next        : int
    config      : StructField?
}

struct public PrefabData {
    path     : string
    name     : string
    root     : StructField?  // owns the parsed tree referenced by `entities[*].config`
    entities : array<PrefabEntity>
}

// path -> compiled prefab. Paths are canonical: relative to the repository root and
// rooted under PREFAB_CONTENT_ROOT when possible.
var private g_prefabs : table<string; PrefabData?>
// Short name (file stem) and content-relative path without extension -> canonical path.
var private g_prefab_names : table<string; string>
// Names that occur more than once: they are removed from g_prefab_names and never
// resolve to an arbitrary file.
var private g_ambiguous_names : table<string; bool>
// Load order for UI/iteration (manifest order is not stable, so RefreshContent sorts).
var private g_prefab_paths : array<string>

def private NormalizePrefabPath(path : string) : string {
    var result = replace(path, "\\", "/")
    while (starts_with(result, "./")) {
        result = slice(result, 2, length(result))
    }
    return result
}

// Absolute or relative path under the content root becomes repo-relative. Prefabs
// outside the content root keep their path: they still cache and spawn, but
// RefreshContent will not discover them.
def private CanonicalPrefabPath(path : string) : string {
    var result = NormalizePrefabPath(path)
    let marker = "/{PREFAB_CONTENT_ROOT}/"
    let marker_index = rfind(result, marker)
    if (marker_index >= 0) {
        result = "{PREFAB_CONTENT_ROOT}/{slice(result, marker_index + length(marker), length(result))}"
    }
    return result
}

def private PrefabStem(path : string) : string {
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

// `game/content/props/sword.prefab` -> `props/sword`. Used as a stable, human key.
def private PrefabKey(path : string) : string {
    var key = path
    let prefix = "{PREFAB_CONTENT_ROOT}/"
    if (starts_with(key, prefix)) {
        key = slice(key, length(prefix), length(key))
    }
    let suffix = ".prefab"
    if (ends_with(key, suffix)) {
        key = slice(key, 0, length(key) - length(suffix))
    }
    return key
}

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
// Compilation: ConfigFormat text -> validated PrefabData
// ---------------------------------------------------------------------------

def private CompilePrefab(text : string; path : string) : PrefabData? {
    var error : string
    var root = read_config(text, error)
    if (length(error) > 0 || root == null) {
        print("[PREFAB ERROR] failed to parse config: {error}\n")
        return null
    }
    if (!(root.value is _object)) {
        print("[PREFAB ERROR] config root is not an object\n")
        return null
    }

    assume root_obj = root.value as _object

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
    var entities : array<PrefabEntity>

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

        var entity : PrefabEntity
        entity.local_id = local_id
        entity.parent = parent_index
        entity.first_child = -1
        entity.prev = -1
        entity.next = -1
        entity.config = entity_cf
        entities |> push(entity)

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
        let parent_index = entities[entity_index].parent
        if (key_exists(last_child, parent_index)) {
            let previous = last_child[parent_index]
            entities[previous].next = entity_index
            entities[entity_index].prev = previous
        } else {
            entities[parent_index].first_child = entity_index
        }
        last_child[parent_index] = entity_index
    }

    var name = from_CF(root_obj?["name"] ?? null, type<string>)
    if (empty(name)) {
        name = PrefabStem(path)
    }

    return new PrefabData(path = CanonicalPrefabPath(path), name = name, root = root, entities <- entities)
}

// ---------------------------------------------------------------------------
// Cache
// ---------------------------------------------------------------------------

def private RegisterPrefabName(name : string; path : string) {
    if (empty(name)) {
        return
    }
    if (key_exists(g_ambiguous_names, name)) {
        return
    }
    if (key_exists(g_prefab_names, name)) {
        if (g_prefab_names[name] != path) {
            print("[PREFAB WARNING] name '{name}' is ambiguous: '{g_prefab_names[name]}' and '{path}'\n")
            g_ambiguous_names[name] = true
            erase(g_prefab_names, name)
        }
        return
    }
    g_prefab_names[name] = path
}

def private StorePrefab(var data : PrefabData?) {
    let path = data.path
    if (!key_exists(g_prefabs, path)) {
        g_prefab_paths |> push(path)
    }
    g_prefabs[path] = data

    let key = PrefabKey(path)
    RegisterPrefabName(key, path)
    RegisterPrefabName(PrefabStem(key), path)
}

// Parses and validates `text`, then stores the compiled prefab under `path`.
// Used by LoadPrefabFile, SavePrefabFromEntity and tests; does no file I/O.
def public CachePrefabText(path : string; text : string) : bool {
    let canonical = CanonicalPrefabPath(path)
    var data = CompilePrefab(text, canonical)
    if (data == null) {
        return false
    }
    StorePrefab(data)
    return true
}

def public ClearPrefabCache() {
    clear(g_prefabs)
    clear(g_prefab_names)
    clear(g_ambiguous_names)
    clear(g_prefab_paths)
}

def public PrefabCount() : int {
    return length(g_prefab_paths)
}

// Iteration for UI: paths are sorted by RefreshContent, and `data` is never null
// for an entry in the list.
def public ForEachPrefab(blk : block<(path : string; data : PrefabData? const) : void>) {
    for (path in g_prefab_paths) {
        invoke(blk, path, g_prefabs[path])
    }
}

def public ResolvePrefabPath(name_or_path : string) : string {
    if (empty(name_or_path)) {
        return ""
    }

    var key = NormalizePrefabPath(name_or_path)
    if (ends_with(key, ".prefab")) {
        let direct = CanonicalPrefabPath(key)
        if (key_exists(g_prefabs, direct)) {
            return direct
        }
        let rooted = CanonicalPrefabPath("{PREFAB_CONTENT_ROOT}/{key}")
        if (key_exists(g_prefabs, rooted)) {
            return rooted
        }
        return ""
    }

    if (key_exists(g_prefab_names, key)) {
        return g_prefab_names[key]
    }

    let rooted = CanonicalPrefabPath("{PREFAB_CONTENT_ROOT}/{key}.prefab")
    if (key_exists(g_prefabs, rooted)) {
        return rooted
    }
    return ""
}

def public GetPrefabData(name_or_path : string) : PrefabData? {
    let path = ResolvePrefabPath(name_or_path)
    if (empty(path)) {
        return null
    }
    return g_prefabs[path]
}

//! Resolves an authored prefab name or path against the content manifest without
//! reading every prefab. Used by explicit loading, which caches only the prefabs the
//! game asks for; an ambiguous short name resolves to "" rather than an arbitrary file.
def public FindInContent(name_or_path : string) : string {
    if (empty(name_or_path)) {
        return ""
    }

    let direct = CanonicalPrefabPath(name_or_path)
    let manifest = Assets::ContentManifest()
    var direct_match = ""
    var key_match = ""
    var stem_match = ""
    var stem_count = 0

    for (path in split(manifest, "\n")) {
        if (empty(path) || !ends_with(path, ".prefab")) {
            continue
        }
        if (path == direct) {
            direct_match = path
        }
        if (PrefabKey(path) == name_or_path) {
            key_match = path
        }
        if (PrefabStem(path) == name_or_path) {
            stem_count++
            stem_match = path
        }
    }

    if (!empty(direct_match)) {
        return direct_match
    }
    if (!empty(key_match)) {
        return key_match
    }
    if (stem_count == 1) {
        return stem_match
    }
    if (stem_count > 1) {
        print("[PREFAB WARNING] name '{name_or_path}' is ambiguous; use a content path\n")
    }
    return ""
}

//! Binary assets one cached prefab needs. Missing or legacy material fields are read
//! the same way the scene collector reads them, so loading sees one dependency set.
def public CollectRefs(data : PrefabData? const) : AssetRefs {
    var refs : AssetRefs
    if (data == null) {
        return <- refs
    }
    refs.parsed = true

    for (entity in data.entities) {
        let config = entity.config
        if (config == null || !(config.value is _object)) {
            continue
        }
        assume entity_obj = config.value as _object

        AddMeshRef(refs, from_CF(entity_obj?["meshGuid"] ?? null, type<uint64>))

        let material_cf = entity_obj?["materialRef"] ?? null
        if (material_cf != null && material_cf.value is _object) {
            let material = from_CF(material_cf, type<MaterialRef>)
            if (!empty(material.path)) {
                AddMaterialRef(refs, ResolveAssetRef(material.path))
            }
        }
    }

    return <- refs
}

def public IsPrefabCached(name_or_path : string) : bool {
    return !empty(ResolvePrefabPath(name_or_path))
}

def public InvalidatePrefab(name_or_path : string) {
    let path = ResolvePrefabPath(name_or_path)
    if (empty(path)) {
        return
    }

    if (key_exists(g_prefabs, path)) {
        erase(g_prefabs, path)
    }
    let index = find_index(g_prefab_paths, path)
    if (index >= 0) {
        g_prefab_paths |> erase(index)
    }

    // Rebuild the name table from the remaining paths: the erased prefab may have
    // owned a short name that another prefab can now claim.
    clear(g_prefab_names)
    clear(g_ambiguous_names)
    for (remaining in g_prefab_paths) {
        let key = PrefabKey(remaining)
        RegisterPrefabName(key, remaining)
        RegisterPrefabName(PrefabStem(key), remaining)
    }
}

// ---------------------------------------------------------------------------
// Spawning
// ---------------------------------------------------------------------------

[profile]
def public SpawnPrefabData(data : PrefabData? const; origin : float3) : EntityId {
    if (data == null) {
        return INVALID_ENTITY_ID
    }

    let entity_count = length(data.entities)
    if (entity_count == 0 || data.entities[0].parent != -1) {
        print("[PREFAB ERROR] compiled prefab '{data.path}' has no root\n")
        return INVALID_ENTITY_ID
    }

    var entity_ids : array<EntityId>
    resize(entity_ids, entity_count)

    for (entity_index in range(entity_count)) {
        let entity = data.entities[entity_index]
        assume entity_obj = entity.config.value as _object

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
                        "[PREFAB WARNING] entity {entity.local_id}: field '{field_name}' was not applied\n"
                    )
                }
            }
        }
        entity_ids[entity_index] = eid
    }

    for (entity_index in range(entity_count)) {
        let entity = data.entities[entity_index]
        let parent_eid = entity.parent >= 0 ? entity_ids[entity.parent] : INVALID_ENTITY_ID
        let first_eid = entity.first_child >= 0 ? entity_ids[entity.first_child] : INVALID_ENTITY_ID
        let prev_eid = entity.prev >= 0 ? entity_ids[entity.prev] : INVALID_ENTITY_ID
        let next_eid = entity.next >= 0 ? entity_ids[entity.next] : INVALID_ENTITY_ID

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

//! Spawns a prefab parsed from ConfigFormat text into the current DECS scene.
//! Returns the root entity id, or INVALID_ENTITY_ID if validation failed.
def public SpawnPrefabFromText(text : string; origin : float3) : EntityId {
    var data = CompilePrefab(text, "")
    if (data == null) {
        return INVALID_ENTITY_ID
    }
    return SpawnPrefabData(data, origin)
}

//! Synchronous spawn from the prefab cache. Call RefreshContent, LoadPrefabFile or
//! a LoaderRequest with AddPrefab first; an unknown or not-yet-loaded prefab returns
//! INVALID_ENTITY_ID.
//! Accepts a short name ("sword"), a content-relative key ("props/sword"), or a path.
def public Spawn(name_or_path : string; origin : float3 = float3(0.0f, 0.0f, 0.0f)) : EntityId {
    let path = ResolvePrefabPath(name_or_path)
    if (empty(path)) {
        print("[PREFAB ERROR] prefab '{name_or_path}' is not cached; call Prefab::RefreshContent() or add it to a LoaderRequest first\n")
        return INVALID_ENTITY_ID
    }
    return SpawnPrefabData(g_prefabs[path], origin)
}

// ---------------------------------------------------------------------------
// File I/O
// ---------------------------------------------------------------------------

//! Reads, validates and caches one .prefab file through the asset API.
[async]
def public LoadPrefabFile(path : string) : bool {
    let canonical = CanonicalPrefabPath(path)
    let text : string <- await(Assets::ReadText(canonical))
    if (empty(text)) {
        print("[PREFAB ERROR] file '{canonical}' is missing or empty\n")
        yield false
    }

    yield CachePrefabText(canonical, text)
}

//! Reads and caches a .prefab file, then spawns it. The editor uses this for drops,
//! so an edited file is re-read instead of using a stale cache entry.
[async]
def public SpawnPrefab(path : string; origin : float3) : EntityId {
    let loaded : bool <- await(LoadPrefabFile(path))
    if (!loaded) {
        yield INVALID_ENTITY_ID
    }

    let canonical = CanonicalPrefabPath(path)
    yield SpawnPrefabData(g_prefabs[canonical], origin)
}

def private PrefabPaths() : array<string> {
    let manifest = Assets::ContentManifest()
    var paths : array<string>
    for (path in split(manifest, "\n")) {
        if (!empty(path) && ends_with(path, ".prefab")) {
            paths |> push(path)
        }
    }
    sort(paths)
    return <- paths
}

//! Fills the cache from the content manifest: every .prefab path listed there is read
//! through Assets::ReadText. The editor's manifest is built from the scanned content
//! tree, the game's comes from the pak, so this code is identical in both.
[async]
def public RefreshContent() : void {
    ClearPrefabCache()

    let paths <- PrefabPaths()
    var loaded = 0
    for (path in paths) {
        let loaded_now : bool <- await(LoadPrefabFile(path))
        if (loaded_now) {
            loaded++
        }
    }

    print("[PREFAB] cached {loaded}/{length(paths)} prefabs\n")
}

//! Synchronous variant for the editor and tools, where a loose file can be read on the
//! spot. The game no longer preloads every prefab: `LoaderRequest::AddPrefab` reads only the
//! prefabs the game asks for, so calling this in a pak build caches nothing.
def public RefreshContentCached() {
    ClearPrefabCache()

    let paths <- PrefabPaths()
    var loaded = 0
    for (path in paths) {
        let text = Assets::ReadTextCached(path)
        if (empty(text)) {
            print("[PREFAB ERROR] '{path}' is not available\n")
        } elif (CachePrefabText(path, text)) {
            loaded++
        }
    }

    print("[PREFAB] cached {loaded}/{length(paths)} prefabs (synchronous)\n")
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

def private ComponentToCF(comp : Component&; index : int64) : StructField? {
    let offset = index * int64(comp.stride)
    unsafe {
        let ptr = reinterpret<void?>(addr(comp.data[offset]))
        return DecsWriteComponent(ptr, comp.info.fullName, comp.name)
    }
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
    if (length(order) == 0) {
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

            var cf_val = ComponentToCF(comp, rec_index)
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

        var local_id_cf = CF(uint(entity_index))
        local_id_cf.name = "localId"
        entity_obj["localId"] = local_id_cf

        var parent_id_cf = CF(parents[entity_index])
        parent_id_cf.name = "parentLocalId"
        entity_obj["parentLocalId"] = parent_id_cf

        var entity_cf = CF(entity_obj)
        entity_cf.typeName = "PrefabEntity"
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
    entities_cf.typeName = "PrefabEntity[]"
    entities_cf.name = "entities"
    root_obj["entities"] = entities_cf

    var text = write_config(CF(root_obj))
    print("[PREFAB] saved '{name}': {length(order)} entities, skipped {skipped_components} components\n")
    return text
}

//! Serializes the selected entity subtree and overwrites `path`.
//! The saved text is cached immediately, so Spawn() sees the new version.
[async]
def public SavePrefabFromEntity(eid : EntityId; path : string) : bool {
    var canonical = CanonicalPrefabPath(path)
    if (!ends_with(canonical, ".prefab")) {
        canonical = "{canonical}.prefab"
    }

    let name = PrefabStem(canonical)
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

    let cached = CachePrefabText(canonical, text)
    print("[PREFAB] wrote '{canonical}'\n")
    yield cached
}
