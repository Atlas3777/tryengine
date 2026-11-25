options gen2
options relaxed_assign = false

module EntitySet shared public

//! Compiled entity set: the runtime form of a scene or a prefab document.
//!
//! A document lists authored components per item. Resolving ConfigFormat field by field when
//! the entities are created is what made a scene load walk the whole document again on every
//! apply; both formats compile into this value instead: the resolved `ComponentValue` bytes of
//! every node, the derived sibling graph and the archetype groups, with the world transform
//! composed once at compile time. Creating entities copies the prepared bytes into archetype
//! storage, so it neither parses ConfigFormat nor dispatches fields by name.
//!
//! The template owns every string it baked in: `ComponentValue.data` is raw bytes, and a
//! string copied into it copies the pointer without touching its count, so `strings` is what
//! keeps them alive.
//!
//! A format module owns its own rules: `Prefab` validates `localId`/pre-order, `Scene` maps
//! `parentLocalId` as a file index. Both hand the finished nodes over.

require daslib/decs
require daslib/decs_boost
require tryengine/ConfigFormat
require tryengine/Content
require tryengine/Model
require tryengine/Profiling
require tryengine/TransformSystem
require tryengine/Types
require tryResources

//! One compiled node. Component values are already resolved from ConfigFormat, so creating an
//! entity neither looks a field up by name nor converts a value. `local` is the authored
//! transform and `world` the composed one; both are already in `values`, and kept here so the
//! whole set can be shifted by a spawn origin without a component lookup.
struct public EntityNode {
    //! Authoring id of the node inside its document (a prefab's `localId`). A scene names a
    //! parent by file index and stores nothing here.
    local_id    : uint
    parent      : int
    first_child : int
    prev        : int
    next        : int
    local       : Transform = Transform()
    world       : Transform = Transform()
    values      : decs::ComponentMap
    //! The authored appearance, repeated here because creating entities may not query the
    //! const `values`: `mesh` is the guide, `material` the optional override.
    model       : ModelRender = ModelRender()
}

//! Nodes that share one component set. DECS creates a whole archetype row at once, so a
//! template creates one group per archetype instead of one entity at a time.
struct public EntityGroup {
    //! Indices into `EntitySetTemplate.nodes`, in creation order.
    nodes : array<int>
}

struct public EntitySetTemplate {
    //! Canonical path of the source document (diagnostics only).
    path    : string
    name    : string
    //! Every string the source document held. The component bytes below point into it.
    strings : array<string>
    nodes   : array<EntityNode>
    groups  : array<EntityGroup>
    //! Resource ids the source document needs before its entities can be created. Collecting
    //! them once at compile time is what lets creation stay synchronous: the caller checks
    //! them, nobody parses the document again.
    deps    : Content::Dependencies
}

//! Collects every string the document holds, so the compiled template can keep them alive.
def public CollectStrings(cf : StructField?; var out : array<string>) {
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

//! Composes the derived `_world` component of every node and bakes it into its values.
//!
//! A prefab is pre-order, so one forward pass composes everything; a scene names a parent by
//! file index and may store it after the child. Both settle the same way: repeat the pass
//! until no node is composed, so a parent that comes later is still composed first. A node
//! held out by a hierarchy cycle keeps its local transform as its world, which is what the
//! runtime transform pass leaves for an entity that no root reaches.
def public BakeWorldTransforms(var nodes : array<EntityNode>) {
    let count = length(nodes)
    var done : array<bool>
    resize(done, count)
    var composed = 0

    while (composed < count) {
        var progress = false
        for (node_index in range(count)) {
            if (done[node_index]) {
                continue
            }
            let parent_index = nodes[node_index].parent
            if (parent_index >= 0 && !done[parent_index]) {
                continue
            }

            nodes[node_index].local = decs::get(nodes[node_index].values, TRANSFORM_FIELD, Transform())
            if (parent_index < 0) {
                nodes[node_index].world = nodes[node_index].local
            } else {
                nodes[node_index].world = ComposeWorld(nodes[parent_index].world, nodes[node_index].local)
            }
            nodes[node_index].values |> set(WORLD_FIELD, nodes[node_index].world)
            done[node_index] = true
            composed++
            progress = true
        }
        if (!progress) {
            break
        }
    }

    // `_world` must exist from birth for every node, even one a cycle kept out of the walk.
    for (node_index in range(count)) {
        if (done[node_index]) {
            continue
        }
        nodes[node_index].local = decs::get(nodes[node_index].values, TRANSFORM_FIELD, Transform())
        nodes[node_index].world = nodes[node_index].local
        nodes[node_index].values |> set(WORLD_FIELD, nodes[node_index].world)
    }
}

//! Groups nodes by the sorted names of the components they carry: the nodes of one group
//! share an archetype, so creating them is one bulk row.
def public BuildGroups(var nodes : array<EntityNode>) : array<EntityGroup> {
    var groups : array<EntityGroup>
    var group_of_key : table<string; int>
    for (node_index in range(length(nodes))) {
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
            var new_group : EntityGroup
            groups |> emplace(new_group)
        }
        groups[group_index].nodes |> push(node_index)
    }
    return <- groups
}

//! Resolves the authored model of every node and checks that its resources are resident, so a
//! caller can report a preload error before it touches the world. `out_refs` is index-aligned
//! with `data.nodes`; it is what `CreateEntities` writes the draw pointers from. False means
//! at least one node cannot be drawn now, and nothing was created.
def public ResolveModels(data : EntitySetTemplate?; var out_refs : array<Model::ModelRefs>) : bool {
    if (data == null) {
        return false
    }

    let entity_count = length(data.nodes)
    resize(out_refs, entity_count)
    for (node_index in range(entity_count)) {
        let model = data.nodes[node_index].model
        let refs = Model::Resolve(model.mesh, model.material)
        if (!refs.ok) {
            print("[ENTITY SET ERROR] '{data.path}' node {node_index}: material '{model.material}' is not an existing authored .mat path; a derived material is reached through the mesh default\n")
            return false
        }
        if (model.mesh != 0ul && !Model::IsReady(refs)) {
            print("[ENTITY SET ERROR] '{data.path}' node {node_index}: mesh {model.mesh:d} / material {refs.material:d} is not loaded; preload the document first\n")
            return false
        }
        out_refs[node_index] = refs
    }
    return true
}

// Scratch of the bulk fill: `create_entities_from_cmp` reports ids through a block, and a
// block cannot carry an array back out (daslang captures arrays by move). Creating runs on
// the script thread and never nests, so module-level scratch is enough.
var private g_ids : array<EntityId>
var private g_component_indices : array<int>
var private g_world_index : int
var private g_mesh_index : int
var private g_material_index : int

//! Creates every node of the set and returns the root entity, or INVALID when the set is
//! malformed or its models were not resolved. `refs` is the successful output of
//! `ResolveModels`; the caller owns the references it took, this call only writes pointers.
//! `out_ids[i]` is the entity of `data.nodes[i]` (the editor's undo remaps a deleted subtree
//! with them).
[profile]
def public CreateEntities(data : EntitySetTemplate?; origin : float3;
                          refs : array<Model::ModelRefs>;
                          var out_ids : array<EntityId>) : EntityId {
    if (data == null) {
        return INVALID_ENTITY_ID
    }

    let entity_count = length(data.nodes)
    if (entity_count == 0 || empty(data.groups)) {
        print("[ENTITY SET ERROR] '{data.path}' has no entities or no archetype groups\n")
        return INVALID_ENTITY_ID
    }
    if (length(refs) != entity_count) {
        print("[ENTITY SET ERROR] '{data.path}': {length(refs)} resolved models for {entity_count} nodes\n")
        return INVALID_ENTITY_ID
    }

    resize(g_ids, entity_count)

    // One archetype row per group: DECS resolves the archetype and grows the component arrays
    // once per group, then every entity is written straight into storage.
    for (group_index in range(length(data.groups))) {
        let first_node = data.groups[group_index].nodes[0]

        var prototype : decs::ComponentMap
        for (cv in data.nodes[first_node].values) {
            prototype |> push(cv)
        }
        // The draw pointers are part of every created entity from birth: they are written
        // below with `set_direct_at`, so no entity changes archetype after creation.
        var no_mesh : TryMesh? = null
        var no_material : TryMat? = null
        prototype |> set(Model::MESH_POINTER_FIELD, no_mesh)
        prototype |> set(Model::MATERIAL_POINTER_FIELD, no_material)
        g_component_indices |> clear()

        let group_size = length(data.groups[group_index].nodes)
        create_entities_from_cmp(group_size, prototype) $(var arch : Archetype; idx : int; eid : EntityId; i : int) {
            let node_index = data.groups[group_index].nodes[i]
            if (empty(g_component_indices)) {
                g_component_indices |> push(arch.eidIndex)
                for (cv in data.nodes[node_index].values) {
                    g_component_indices |> push(decs::find_component_index(arch, cv.name))
                }
                g_world_index = decs::find_component_index(arch, WORLD_FIELD)
                g_mesh_index = decs::find_component_index(arch, Model::MESH_POINTER_FIELD)
                g_material_index = decs::find_component_index(arch, Model::MATERIAL_POINTER_FIELD)
            }

            decs::set_direct_at(arch, g_component_indices[0], idx, eid)

            // The prepared bytes are already the component layout, so they are copied as is.
            var slot = 1
            for (cv in data.nodes[node_index].values) {
                let component_index = g_component_indices[slot]
                slot++
                unsafe {
                    memcpy(
                        addr(arch.components[component_index].data[idx * arch.components[component_index].stride]),
                        addr(cv.data),
                        arch.components[component_index].stride
                    )
                }
            }

            let refs_at = refs[node_index]
            if (refs_at.mesh != 0ul) {
                var mesh_ptr := Model::MeshPointer(refs_at)
                if (mesh_ptr != null) {
                    decs::set_direct_at(arch, g_mesh_index, idx, mesh_ptr)
                }
            }
            if (refs_at.material != 0ul) {
                var material_ptr := Model::MaterialPointer(refs_at)
                if (material_ptr != null) {
                    decs::set_direct_at(arch, g_material_index, idx, material_ptr)
                }
            }

            // The baked world transform contains the authored root position; the spawn point
            // translates the whole subtree, so every node moves by `origin`.
            decs::set_direct_at(
                arch,
                g_world_index,
                idx,
                Transform(
                    position = data.nodes[node_index].world.position + origin,
                    rotation = data.nodes[node_index].world.rotation,
                    scale = data.nodes[node_index].world.scale
                )
            )
            g_ids[node_index] = eid
        }
    }

    commit()

    // Only the root's local transform moves to the spawn point; every node's world position
    // was already shifted in the fill above. The root may live in another group than the last
    // one created, so its component index is resolved from its own archetype.
    let root_record = decsState.entityLookup[int(g_ids[0].id)]
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
    // entity row to change one component: write it straight into archetype storage, with the
    // component index resolved once per archetype.
    var relationship_index : table<int; int>
    for (entity_index in range(entity_count)) {
        let eid = g_ids[entity_index]
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
                parent = parent_index >= 0 ? g_ids[parent_index] : INVALID_ENTITY_ID,
                first_child = first_child >= 0 ? g_ids[first_child] : INVALID_ENTITY_ID,
                prev = prev_index >= 0 ? g_ids[prev_index] : INVALID_ENTITY_ID,
                next = next_index >= 0 ? g_ids[next_index] : INVALID_ENTITY_ID
            )
        )
    }

    clone(out_ids, g_ids)
    return g_ids[0]
}
