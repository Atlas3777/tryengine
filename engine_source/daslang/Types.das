options gen2

module Types shared public

require math
require daslib/decs_boost

[decs_template]
struct Light{
    radius : float
    color : float3
    intensity : float
}

[decs_template]
struct Transform {
    position : float3
    rotation : float4
    scale    : float3
}

[decs_template]
struct Relationship {
    parent : EntityId
    first_child : EntityId
    prev : EntityId
    next : EntityId
}


struct MeshFilter {
    asset_id : uint64
}

struct MeshRenderer{
    asset_id : uint64
}

struct LightComponent{
    color     : float3 = float3(1.0, 1.0, 1.0)
    intensity : float = 1.0f
    radius    : float = 10.0f
}

struct Camera{
    view_matrix : float4x4
    fov         : float = 45.0f
    near_plane  : float = 0.1f
    far_plane   : float = 100.0f
}
