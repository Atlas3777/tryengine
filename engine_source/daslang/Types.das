options gen2

module Types shared public

require daslib/decs_boost
require tryengine/EcsMacro

%ecs~("HP", type<uint>) %%
%ecs~("Mana", type<int?>) %%
// %ecs~("Inventory", type<array<int>>) %%

[decs_template, EcsTemplate]
struct PointLight{
    color : float3
    intensity : float
    radius : float
}

[decs_template, EcsTemplate]
struct Transform {
    position : float3
    rotation : float4
    scale    : float3
}

[decs_template, EcsTemplate]
struct Relationship {
    parent : EntityId = INVALID_ENTITY_ID
    first_child : EntityId = INVALID_ENTITY_ID
    prev : EntityId = INVALID_ENTITY_ID
    next : EntityId = INVALID_ENTITY_ID
}

[decs_template, EcsTemplate]
struct Camera {
    fov         : float = 70.0f
    near_plane  : float = 0.1f
    far_plane   : float = 1000.0f
}
