options gen2
options relaxed_assign = false

module Types shared public

require daslib/decs_boost
require tryengine/EcsMacro
require tryengine/ConfigFormatBoost
require tryengine/Serializable

// Authored appearance of one entity, one component. `mesh` is the imported mesh guide per
// primitive; `material` is an optional override to an authoritative `.mat` — a derived
// (imported) material is reached through its mesh artifact default, never addressed from
// authored content. An empty path means "the material the mesh artifact carries by default".
// The pair of pointers a draw call needs lives in the runtime-only `mesh`/`material`
// components, resolved once at entity creation — the frame loop never loads anything.
[DecsSerializable]
struct ModelRender {
    mesh     : uint64 = 0ul
    @asset = "data:mat"
    material : string
}

// A [decs_template] component: one group in the Inspector, one component per field
// (`PointLight_color`, ...). Fields are saved by their own types, so the struct needs no
// [DecsSerializable]; the group is declared with %ecs at the bottom of this file.
[decs_template]
struct PointLight {
    color     : float3
    @min = 0.0
    intensity : float
    @min = 0.0
    radius    : float
}

// Authored local transform, one component named "transform". [DecsSerializable] is the
// physical ability to write the value; %ecs below makes it a component and leaves both flags
// at their defaults, so it is saved and shown.
[DecsSerializable]
struct Transform {
    position : float3 = float3(0.0f, 0.0f, 0.0f)
    rotation : float4 = float4(0.0f, 0.0f, 0.0f, 1.0f)
    scale    : float3 = float3(1.0f, 1.0f, 1.0f)
}

// Derived world transform, one component named "_world". It is transient and hidden:
// TransformSystem writes it, Render reads it, and it never reaches a scene, a prefab or the
// Inspector. No [DecsSerializable] either: there is nothing to write.
struct WorldTransform {
    position : float3 = float3(0.0f, 0.0f, 0.0f)
    rotation : float4 = float4(0.0f, 0.0f, 0.0f, 1.0f)
    scale    : float3 = float3(1.0f, 1.0f, 1.0f)
}

// Relationship, one component named "relationship". Derived from the authored hierarchy
// (`parentLocalId` in scenes and prefabs), so it is transient; the Inspector still shows it
// through a custom draw.
struct Relationship {
    parent      : EntityId = INVALID_ENTITY_ID
    first_child : EntityId = INVALID_ENTITY_ID
    prev        : EntityId = INVALID_ENTITY_ID
    next        : EntityId = INVALID_ENTITY_ID
}

let public RELATIONSHIP_FIELD : string = "relationship"

[decs_template]
struct Camera {
    @min = 1.0
    @max = 179.0
    fov         : float = 70.0f
    @min = 0.001
    @max = 10000.0
    near_plane  : float = 0.1f
    far_plane   : float = 1000.0f
}

// The engine component set. %ecs declares one named value; %ecs_template declares a
// [decs_template] component, whose type is also its name. Everything is saved and shown
// unless it says otherwise.
%ecs~("model", type<ModelRender>) %%
%ecs~("transform", type<Transform>) %%
%ecs~("_world", type<WorldTransform>, hidden = true, transient = true) %%
%ecs~("relationship", type<Relationship>, transient = true) %%
%ecs_template~(type<Camera>) %%
%ecs_template~(type<PointLight>) %%
%ecs~("HP", type<uint>) %%
%ecs~("tag", type<string>) %%
