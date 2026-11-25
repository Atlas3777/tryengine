options gen2
options relaxed_assign = false

module Model shared public

//! Runtime resolution of the authored `model` component.
//!
//! `ModelRender` holds a mesh guide plus an optional `.mat` path. The pointers a draw call
//! needs live in two runtime-only components (`mesh`, `material`), exactly the pair the
//! renderer reads. Resolution happens when an entity is created (`Prefab::Spawn`,
//! `Scene::Load`) or when the editor re-applies a material — never in the frame loop:
//! loading is the loader's job (`Loader::Load`), and a model that is not ready is an error,
//! not a pop-in.
//!
//! The material id is the override path when it is set, and the mesh artifact default
//! otherwise. A derived (imported) material therefore never appears in authored content:
//! `guid:<id>` in the material field is rejected, the default is the way to reach it.

require strings
require daslib/decs
require daslib/decs_boost
require tryengine/Types
require tryResources

let public MODEL_FIELD : string = "model"
let public MESH_POINTER_FIELD : string = "mesh"
let public MATERIAL_POINTER_FIELD : string = "material"

let private GUID_REF_PREFIX : string = "guid:"

//! Authored pair resolved to resource ids. `ok` is false when the material field is not an
//! authored path; `material` is 0 when the mesh has no default and nothing overrides it.
struct public ModelRefs {
    mesh     : uint64 = 0ul
    material : uint64 = 0ul
    ok       : bool = false
}

//! Authoritative-material default of a mesh, as baked into its artifact. 0 while the mesh is
//! not loaded, or when the source primitive had no material.
def public MeshMaterial(mesh_id : uint64) : uint64 {
    if (mesh_id == 0ul) {
        return 0ul
    }
    return MeshDefaultMaterial(mesh_id)
}

//! Resolves the authored pair. Pure: it reads the registry and the loaded mesh artifact, and
//! never requests or waits, so a caller can decide what "not ready" means for it.
def public Resolve(mesh_id : uint64; material_path : string) : ModelRefs {
    var refs = ModelRefs()
    refs.mesh = mesh_id
    refs.ok = true

    if (!empty(material_path)) {
        if (starts_with(material_path, GUID_REF_PREFIX)) {
            // A derived material is addressed through the mesh default, not by guid.
            refs.ok = false
            return refs
        }
        refs.material = ResolveAssetPath(material_path)
        // An unknown path is an error, not an empty override: silently falling back to the
        // mesh default would hide a typo.
        refs.ok = refs.material != 0ul
        return refs
    }

    refs.material = MeshMaterial(mesh_id)
    return refs
}

//! Pure readiness check: no request, no wait. A scene checks this before it touches the
//! world; the resources are resident because the loader still holds them.
def public IsReady(refs : ModelRefs) : bool {
    if (!refs.ok || refs.mesh == 0ul || GetMeshPointer(refs.mesh) == null) {
        return false
    }
    if (refs.material == 0ul) {
        // Geometry without a material: the entity has no draw call until it gets one.
        return true
    }
    return GetMaterialPointer(refs.material) != null
}

//! Requests everything the pair needs and reports whether it is usable right now: no waiting,
//! no coroutine. `false` means a resource is missing or still loading, and the caller must not
//! create the entity. This is the editor's on-demand path (ProBuilder's grid material is
//! requested when the tool is used); runtime spawn uses `Hold` and never starts a read.
def public Reserve(refs : ModelRefs) : bool {
    if (!refs.ok || refs.mesh == 0ul) {
        return false
    }

    RequestLoadMesh(refs.mesh)
    if (refs.material != 0ul) {
        RequestLoadMaterial(refs.material)
    }
    return IsReady(refs)
}

//! Reference-only counterpart of `Reserve` for the runtime: the loader preloaded the pair, the
//! spawn only takes its own reference. `false` is a preload error, not a reason to read the
//! disk from the spawn path.
def public Hold(refs : ModelRefs) : bool {
    if (!refs.ok || refs.mesh == 0ul) {
        return false
    }

    if (!HoldMeshResource(refs.mesh)) {
        return false
    }
    if (refs.material != 0ul && !HoldMaterialResource(refs.material)) {
        return false
    }
    return IsReady(refs)
}

//! The resolved pointers, valid only after `Reserve`/`IsReady` reported the pair usable.
def public MeshPointer(refs : ModelRefs) : TryMesh? {
    return refs.mesh != 0ul ? GetMeshPointer(refs.mesh) : null
}

def public MaterialPointer(refs : ModelRefs) : TryMat? {
    return refs.material != 0ul ? GetMaterialPointer(refs.material) : null
}

// Last appearance applied by `ApplyToEntity`: entity plus the ids it was resolved to.
var private g_last_model_eid : uint = 0u
var private g_last_model_mesh : uint64 = 0ul
var private g_last_model_material : uint64 = 0ul
var private g_reapply_pending : bool = false

//! Re-resolves the authored `model` of every entity into the runtime pointer components.
//!
//! The DECS heap archive used by Play/Stop and hot reload restores component rows but does
//! not preserve raw pointers: the `mesh`/`material` values come back null while the
//! components themselves stay in the archetype. Resources are resident at that point (the
//! scene and ProBuilder hold references), so this is one registry lookup per entity. The
//! `ApplyToEntity` cache is dropped too, otherwise selecting an entity whose pair was
//! already applied would skip the write and leave it invisible.
def public ReapplyPointers() {
    g_reapply_pending = false
    query() $(eid : EntityId; model : ModelRender) {
        let refs = Resolve(model.mesh, model.material)
        if (!refs.ok || refs.mesh == 0ul) {
            return
        }

        // After a reimport the resource is invalidated: RequestLoad builds it again under
        // the same id. While it is not ready the old pointer stays in the archetype and keeps
        // drawing; retry until every resource has arrived.
        var mesh_ptr = MeshPointer(refs)
        if (mesh_ptr == null) {
            RequestLoadMesh(refs.mesh)
            g_reapply_pending = true
            return
        }
        var material_ptr = MaterialPointer(refs)
        if (refs.material != 0ul && material_ptr == null) {
            RequestLoadMaterial(refs.material)
            g_reapply_pending = true
            return
        }

        update_entity(eid) @(eid, cmp) {
            cmp |> set(MESH_POINTER_FIELD, mesh_ptr)
            if (material_ptr != null) {
                cmp |> set(MATERIAL_POINTER_FIELD, material_ptr)
            }
        }
    }
    commit()
    g_last_model_eid = 0u
}

//! true while ReapplyPointers is waiting for a resource to reload after a reimport.
def public HasPendingReapply() : bool {
    return g_reapply_pending
}

//! Writes the resolved pointers into an existing entity. Used by the editor after the model
//! component changed; `Spawn` and `Scene::Load` write them while the entity is created, so
//! they do not move the entity between archetypes afterwards.
def public ApplyToEntity(target_eid : EntityId; verbose : bool = true) : bool {
    var refs = ModelRefs()
    var has_model = false
    query(target_eid) $(let model : ModelRender) {
        refs = Resolve(model.mesh, model.material)
        has_model = true
    }

    if (!has_model) {
        return false
    }
    if (!Reserve(refs)) {
        if (verbose) {
            print("[MODEL ERROR] entity {target_eid.id:d}: mesh {refs.mesh:d} / material {refs.material:d} is not ready or the material path is unknown\n")
        }
        return false
    }

    // The editor calls this every frame for the selected entity, so an unchanged model must
    // neither rewrite the row nor repeat the load requests (each request increments the
    // reference count). The pair last applied is what "unchanged" means.
    if (target_eid.id == g_last_model_eid && refs.mesh == g_last_model_mesh &&
        refs.material == g_last_model_material) {
        return true
    }

    var mesh_ptr := MeshPointer(refs)
    var material_ptr := MaterialPointer(refs)
    let resolved_mesh = refs.mesh
    let resolved_material = refs.material
    update_entity(target_eid) @(eid, cmp) {
        if (mesh_ptr != null) {
            cmp |> set(MESH_POINTER_FIELD, mesh_ptr)
        }
        if (material_ptr != null) {
            cmp |> set(MATERIAL_POINTER_FIELD, material_ptr)
        }
    }
    commit()

    g_last_model_eid = target_eid.id
    g_last_model_mesh = resolved_mesh
    g_last_model_material = resolved_material
    return true
}
