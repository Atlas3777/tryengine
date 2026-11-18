options gen2
options relaxed_assign = false

module Prefab shared public

//! Prefab format and spawning.
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

require strings
require math
require daslib/async_boost
require daslib/strings_boost
require daslib/decs
require daslib/decs_boost
require tryengine/ConfigFormat
require tryengine/ConfigFormatBoost
require tryengine/Serializable
require tryengine/TransformSystem
require tryengine/Types
require tryengine/FileReader

let public PREFAB_VERSION : uint = 1u
let public PREFAB_ROOT_PARENT : uint = 4294967295u

def private IsReservedEntityField(name : string) : bool {
    return name == "eid" || starts_with(name, "Relationship_")
}

def private EntityLocalId(cf : StructField?) : uint {
    return from_CF(cf, type<uint>)
}

def private EntityParentLocalId(cf : StructField?) : uint {
    return from_CF(cf, type<uint>)
}

def private GetRelationshipId(var links : table<uint; uint>; local_id : uint) : uint {
    if (key_exists(links, local_id)) {
        return links[local_id]
    }
    return PREFAB_ROOT_PARENT
}

def private GetEntityId(var ids : table<uint; EntityId>; local_id : uint) : EntityId {
    if (local_id == PREFAB_ROOT_PARENT || !key_exists(ids, local_id)) {
        return INVALID_ENTITY_ID
    }
    return ids[local_id]
}

// Local TRS of one prefab entity. Missing components are identity, so the array
// may describe a pure grouping node.
def private GetLocalTransform(entity_obj : table<string; StructField?>) : Transform {
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

//! Spawns a prefab parsed from ConfigFormat text into the current DECS scene.
//! Returns the root entity id, or INVALID_ENTITY_ID if validation failed.
def public SpawnPrefabFromText(text : string; origin : float3) : EntityId {
    var error : string
    var root = read_config(text, error)
    if (length(error) > 0 || root == null) {
        print("[PREFAB ERROR] failed to parse config: {error}\n")
        return INVALID_ENTITY_ID
    }
    if (!(root.value is _object)) {
        print("[PREFAB ERROR] config root is not an object\n")
        return INVALID_ENTITY_ID
    }

    assume root_obj = root.value as _object

    let version = from_CF(root_obj?["version"] ?? null, type<uint>)
    if (version != PREFAB_VERSION) {
        print("[PREFAB ERROR] unsupported version {version}, expected {PREFAB_VERSION}\n")
        return INVALID_ENTITY_ID
    }

    let config_type = from_CF(root_obj?["configType"] ?? null, type<string>)
    let config_row = from_CF(root_obj?["configRow"] ?? null, type<string>)
    if (!empty(config_type) && empty(config_row)) {
        print("[PREFAB ERROR] configRow is required when configType is set ('{config_type}')\n")
        return INVALID_ENTITY_ID
    }

    let entities_cf = root_obj?["entities"] ?? null
    if (entities_cf == null || !(entities_cf.value is _array)) {
        print("[PREFAB ERROR] 'entities' is not an array\n")
        return INVALID_ENTITY_ID
    }
    assume entities_arr = entities_cf.value as _array

    var local_ids : table<uint; int>
    var entity_indices : array<int>

    var roots = 0
    var root_local : uint = PREFAB_ROOT_PARENT

    let entity_count = length(entities_arr)
    for (entity_index in range(entity_count)) {
        let entity_cf = entities_arr[entity_index]
        if (entity_cf == null || !(entity_cf.value is _object)) {
            print("[PREFAB ERROR] entity #{entity_index} is not an object\n")
            return INVALID_ENTITY_ID
        }

        assume entity_obj = entity_cf.value as _object
        let local_id = EntityLocalId(entity_obj?["localId"] ?? null)
        if (local_id == PREFAB_ROOT_PARENT) {
            print("[PREFAB ERROR] entity #{entity_index}: localId {PREFAB_ROOT_PARENT} is reserved\n")
            return INVALID_ENTITY_ID
        }
        if (key_exists(local_ids, local_id)) {
            print("[PREFAB ERROR] entity #{entity_index}: duplicate localId {local_id}\n")
            return INVALID_ENTITY_ID
        }

        let parent_cf = entity_obj?["parentLocalId"] ?? null
        if (parent_cf == null) {
            print("[PREFAB ERROR] entity #{entity_index}: parentLocalId is required\n")
            return INVALID_ENTITY_ID
        }
        let parent_local_id = EntityParentLocalId(parent_cf)
        if (parent_local_id == PREFAB_ROOT_PARENT) {
            roots++
            if (entity_index != 0) {
                print("[PREFAB ERROR] entity #{entity_index}: the root must be the first entity\n")
                return INVALID_ENTITY_ID
            }
            root_local = local_id
        } else {
            if (!key_exists(local_ids, parent_local_id)) {
                print("[PREFAB ERROR] entity #{entity_index}: parent {parent_local_id} must be stored before the child\n")
                return INVALID_ENTITY_ID
            }
        }

        for (field_name in keys(entity_obj)) {
            if (field_name == "localId" || field_name == "parentLocalId") {
                continue
            }
            if (IsReservedEntityField(field_name)) {
                print("[PREFAB ERROR] entity #{entity_index}: field '{field_name}' is forbidden in a prefab\n")
                return INVALID_ENTITY_ID
            }
        }

        local_ids[local_id] = entity_index
        entity_indices |> push(entity_index)
    }

    if (roots != 1 || root_local == PREFAB_ROOT_PARENT) {
        print("[PREFAB ERROR] expected exactly one root, found {roots}\n")
        return INVALID_ENTITY_ID
    }

    // Derived relationship graph: store only parentLocalId, the sibling links follow
    // from the array order (the array is pre-order, so it is also sibling order).
    var first_child : table<uint; uint>
    var prev_sibling : table<uint; uint>
    var next_sibling : table<uint; uint>
    var last_child : table<uint; uint>

    for (entity_index in entity_indices) {
        let entity_cf = entities_arr[entity_index]
        assume entity_obj = entity_cf.value as _object
        let local_id = EntityLocalId(entity_obj?["localId"] ?? null)
        let parent_local_id = EntityParentLocalId(entity_obj?["parentLocalId"] ?? null)
        if (parent_local_id == PREFAB_ROOT_PARENT) {
            continue
        }

        if (key_exists(last_child, parent_local_id)) {
            let previous = last_child[parent_local_id]
            next_sibling[previous] = local_id
            prev_sibling[local_id] = previous
        } else {
            first_child[parent_local_id] = local_id
        }
        last_child[parent_local_id] = local_id
    }

    var entity_ids : table<uint; EntityId>

    for (entity_index in entity_indices) {
        let entity_cf = entities_arr[entity_index]
        assume entity_obj = entity_cf.value as _object
        let local_id = EntityLocalId(entity_obj?["localId"] ?? null)
        let parent_local_id = EntityParentLocalId(entity_obj?["parentLocalId"] ?? null)

        // The prefab stores local TRS; only the root is moved to the spawn point.
        // Children keep their authored local transform, and UpdateWorldTransforms
        // derives the world transform from the Relationship hierarchy.
        var local_transform = GetLocalTransform(entity_obj)
        if (parent_local_id == PREFAB_ROOT_PARENT) {
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
                    print("[PREFAB WARNING] entity {local_id}: field '{field_name}' was not applied\n")
                }
            }
        }
        entity_ids[local_id] = eid
    }

    for (entity_index in entity_indices) {
        let entity_cf = entities_arr[entity_index]
        assume entity_obj = entity_cf.value as _object
        let local_id = EntityLocalId(entity_obj?["localId"] ?? null)
        let parent_local_id = EntityParentLocalId(entity_obj?["parentLocalId"] ?? null)

        let first_local = GetRelationshipId(first_child, local_id)
        let prev_local = GetRelationshipId(prev_sibling, local_id)
        let next_local = GetRelationshipId(next_sibling, local_id)

        let entity_eid = GetEntityId(entity_ids, local_id)
        let parent_eid = GetEntityId(entity_ids, parent_local_id)
        let first_eid = GetEntityId(entity_ids, first_local)
        let prev_eid = GetEntityId(entity_ids, prev_local)
        let next_eid = GetEntityId(entity_ids, next_local)

        update_entity(entity_eid) @(eid, cmp) {
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
    return GetEntityId(entity_ids, root_local)
}

//! Reads a .prefab file and spawns it. Use this from game code or from the editor.
[async]
def public SpawnPrefab(path : string; origin : float3) : EntityId {
    let file_size : uint64 = await(FileReader::GetFileSize(path))
    if (file_size == 0ul) {
        print("[PREFAB ERROR] file '{path}' does not exist or is empty\n")
        yield INVALID_ENTITY_ID
    }

    let bytes : array<uint8> <- await(FileReader::ReadFile(path, 0ul, uint(file_size)))
    yield SpawnPrefabFromText(string(bytes), origin)
}
