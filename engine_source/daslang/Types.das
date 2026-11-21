options gen2
options relaxed_assign = false

module Types shared public

require daslib/decs_boost
require tryengine/EcsMacro
require tryengine/ConfigFormatBoost
require tryengine/Serializable
require tryengine/AssetKind

// Authoring reference to a material file. The wrapper (instead of a bare string) carries
// the asset-kind annotation, so the inspector can draw a picker from the field type alone.
// `path` is either a project path or "guid:<id>" for an imported material.
[asset(kind = "data", title = "Material", ext = ".mat"), DecsSerializable]
struct MaterialRef {
    path : string
}

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

// Transform, one component named "transform" when authored and one named "_world" when
// derived. The three TRS values travel as one struct: a write is one memcpy, a query reads
// one contiguous value, and the type is an ordinary copyable struct. The defaults are the
// identity, so `default<Transform>` and a missing field in a document are both usable instead
// of a zero scale. `_world` is what the renderer reads and TransformSystem writes; the leading
// underscore keeps it out of scene save and the Inspector.
[DecsSerializable]
struct Transform {
    position : float3 = float3(0.0f, 0.0f, 0.0f)
    rotation : float4 = float4(0.0f, 0.0f, 0.0f, 1.0f)
    scale    : float3 = float3(1.0f, 1.0f, 1.0f)
}

%ecs~("transform", type<Transform>) %%

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
