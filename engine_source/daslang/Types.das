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
[EcsComponent(name = "model"), DecsSerializable]
struct ModelRender {
    mesh     : uint64 = 0ul
    @asset = "data:mat"
    material : string
}

%ecs~("HP", type<uint>) %%
%ecs~("Mana", type<int?>) %%

[decs_template, EcsTemplate, DecsSerializable]
struct PointLight {
    color     : float3
    @min = 0.0
    intensity : float
    @min = 0.0
    radius    : float
}

// Authored local transform. The type is [DecsSerializable], so a component of this type is
// written to scenes and prefabs; [EcsComponent] registers it in the Inspector's component
// list under "transform".
[DecsSerializable, EcsComponent(name = "transform")]
struct Transform {
    position : float3 = float3(0.0f, 0.0f, 0.0f)
    rotation : float4 = float4(0.0f, 0.0f, 0.0f, 1.0f)
    scale    : float3 = float3(1.0f, 1.0f, 1.0f)
}

// Derived world transform. Same TRS shape as Transform, but a distinct type without
// [DecsSerializable]: a component of this type has no writer, so scene/prefab save and
// prefab authoring skip it by type. TransformSystem writes `_world`, Render reads it.
struct WorldTransform {
    position : float3 = float3(0.0f, 0.0f, 0.0f)
    rotation : float4 = float4(0.0f, 0.0f, 0.0f, 1.0f)
    scale    : float3 = float3(1.0f, 1.0f, 1.0f)
}

// Relationship, one component named "relationship". A plain struct has no writer, so the
// links are runtime-only by type; the Inspector still shows them through a custom draw.
// The links are derived from the authored hierarchy (`parentLocalId` in scenes and prefabs).
[EcsComponent(name = "relationship")]
struct Relationship {
    parent      : EntityId = INVALID_ENTITY_ID
    first_child : EntityId = INVALID_ENTITY_ID
    prev        : EntityId = INVALID_ENTITY_ID
    next        : EntityId = INVALID_ENTITY_ID
}

let public RELATIONSHIP_FIELD : string = "relationship"

[decs_template, EcsTemplate, DecsSerializable]
struct Camera {
    @min = 1.0
    @max = 179.0
    fov         : float = 70.0f
    @min = 0.001
    @max = 10000.0
    near_plane  : float = 0.1f
    far_plane   : float = 1000.0f
}
