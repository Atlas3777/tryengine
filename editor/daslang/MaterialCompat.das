options gen2
options relaxed_assign = false

module MaterialCompat shared public

//! Compatibility between a material and a mesh.
//!
//! A material is just data for a shader; the shader, in turn, reads a fixed set of mesh
//! attributes (`MESH_ATTRIBUTES` in the .slang source). Offering every .mat for every mesh is
//! how a material with an albedo texture ends up on a mesh without UVs: the engine has no way
//! to show it, and the shader silently falls back to defaults. This module answers the one
//! question the pickers need:
//!
//!   required_attributes(material.shader) & ~mesh.attribute_flags == 0
//!
//! Attribute masks come from C++ (ShaderReflectionData and Mesh::attribute_flags) so the
//! bit layout lives in one place. The cache is keyed by the .mat path and is dropped when the
//! data tree changes; while a shader is loading the answer is `PENDING` and the caller treats
//! it as compatible (hiding a valid material until its shader is resident is worse).

require TryEditor
require tryResources

//! Shader is not resident yet: compatibility is unknown.
let public PENDING : uint = 0xFFFFFFFFu

//! Shader is not a mesh material (UI, editor pass). Such a material must never be offered
//! for a mesh, even though it reads no vertex attributes.
let public NOT_MESH : uint = 0xFFFFFFFEu

var private g_required : table<string; uint>
var private g_revision : uint64 = 0ul
var private g_generation : uint64 = 0ul

def private EnsureCache() {
    let revision = get_tree_revision(StorageDomain.Data)
    let generation = AssetReimportGeneration()
    if (revision != g_revision || generation != g_generation) {
        clear(g_required)
        g_revision = revision
        g_generation = generation
    }
}

//! Mesh attributes the material's shader reads; PENDING until the shader is loaded.
def public RequiredAttributes(mat_path : string) : uint {
    EnsureCache()
    if (key_exists(g_required, mat_path)) {
        return g_required[mat_path]
    }
    let required = GetMaterialFileAttributes(mat_path)
    if (required != PENDING) {
        g_required[mat_path] = required
    }
    return required
}

//! True when the mesh has every attribute the material's shader asks for. A pending shader or
//! a mesh without a guide is treated as compatible: the limitation is transient, not a reason
//! to hide the asset.
def public IsCompatible(mat_path : string; mesh_guid : uint64) : bool {
    if (mesh_guid == 0ul || GetMeshPointer(mesh_guid) == null) {
        // No mesh yet (or it is still loading): do not hide everything while the answer is
        // unknown. Once the mesh is resident the list narrows to what it actually supports.
        return true
    }
    let required = RequiredAttributes(mat_path)
    if (required == PENDING) {
        return true
    }
    if (required == NOT_MESH) {
        return false
    }
    if (required == 0u) {
        return true
    }
    let have = GetMeshAttributeFlags(mesh_guid)
    return (required & have) == required
}

//! Attributes the shader reads but the mesh does not have.
def public MissingAttributes(mat_path : string; mesh_guid : uint64) : uint {
    if (mesh_guid == 0ul) {
        return 0u
    }
    let required = RequiredAttributes(mat_path)
    if (required == PENDING) {
        return 0u
    }
    if (required == NOT_MESH) {
        return NOT_MESH
    }
    return required & (~GetMeshAttributeFlags(mesh_guid))
}

def public RequiredAttributesText(mat_path : string) : string {
    let required = RequiredAttributes(mat_path)
    if (required == PENDING) {
        return "shader loading"
    }
    if (required == NOT_MESH) {
        return "not a mesh material"
    }
    return DescribeMeshAttributes(required)
}

def public MeshAttributesText(mesh_guid : uint64) : string {
    return DescribeMeshAttributes(GetMeshAttributeFlags(mesh_guid))
}

def public MissingAttributesText(mat_path : string; mesh_guid : uint64) : string {
    return DescribeMeshAttributes(MissingAttributes(mat_path, mesh_guid))
}
