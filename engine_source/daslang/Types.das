options gen2

module Types shared public

require daslib/decs_boost
require tryengine/EcsMacro
require tryengine/ConfigFormatBoost
require tryengine/Serializable

%ecs~("HP", type<uint>) %%
%ecs~("Mana", type<int?>) %%
// %ecs~("Inventory", type<array<int>>) %%

// Шаблоны ниже помечены DecsSerializable: для них генерируется схема полей для
// инспектора (apply на этапе компиляции) и adder, раскладывающий значения по
// префиксным компонентам при загрузке. Аннотации полей (@min, @max, @title, ...)
// понимает инспектор.

[decs_template, EcsTemplate, DecsSerializable]
struct PointLight {
    color     : float3
    @min = 0.0
    intensity : float
    @min = 0.0
    radius    : float
}

[decs_template, EcsTemplate, DecsSerializable]
struct Transform {
    position : float3
    rotation : float4
    scale    : float3
}

[decs_template, EcsTemplate, DecsSerializable]
struct Relationship {
    parent      : EntityId = INVALID_ENTITY_ID
    first_child : EntityId = INVALID_ENTITY_ID
    prev        : EntityId = INVALID_ENTITY_ID
    next        : EntityId = INVALID_ENTITY_ID
}

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
