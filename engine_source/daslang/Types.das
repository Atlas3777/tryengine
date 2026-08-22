options gen2

module Types shared public

require math
require daslib/decs_boost
require tryeditor
require tryResources

[decs_template]
struct PointLight{
    color : float3
    intensity : float
    radius : float
}

[decs_template]
struct Transform {
    position : float3 = float3(0, 0, 0)
    rotation : float4 = quat_from_euler(float3(0, 0, 0))
    scale    : float3 = float3(1, 1, 1)
}

[decs_template]
struct Relationship {
    parent : EntityId = INVALID_ENTITY_ID
    first_child : EntityId = INVALID_ENTITY_ID
    prev : EntityId = INVALID_ENTITY_ID
    next : EntityId = INVALID_ENTITY_ID
}

[decs_template]
struct Camera {
    fov         : float = 70.0f
    near_plane  : float = 0.1f
    far_plane   : float = 1000.0f
}