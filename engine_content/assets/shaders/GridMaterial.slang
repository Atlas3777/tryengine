// Grid material for level primitives (the ProBuilder tool in the editor).
//
// Unlike the editor's infinite Grid.slang, this one is an ordinary opaque material over a
// mesh: it draws a one-metre grid on every face of a box. The grid lives in object space
// multiplied by the object's scale, so a box scaled to 2 m shows two metre cells no matter
// how the entity is transformed, and the lines stay one metre apart in world units.
//
// The vertex stage repeats the Standard.slang fetch path (BDA positions and packed
// attributes) because shaders with entry points are not modules and cannot be imported.

static const uint ATTR_BIT_NORMAL = 0;

struct GeometryPointers
{
    float3*   positions;        // BDA: Позиции (12 байт / вершина)
    uint32_t* attributes;       // BDA: Буфер 32-битных слов упакованных атрибутов
    uint      attribute_flags;  // Битовая маска MeshAttributeFlags
    uint      attribute_stride; // Размер атрибутов вершины в БАЙТАХ
};

struct ObjectData
{
    float4x4 model;
    float4x4 normalMatrix;
    GeometryPointers geometry;
    uint2 _pad;
};

struct Camera
{
    float4x4 view;
    float4x4 proj;
};

struct PointLight
{
    float4 position_radius;
    float4 color_intensity;
};

// Mirrors FragmentDataGPU in OpaquePass.cpp; the pass fills the same push-constant block for
// every material shader.
struct FragmentData
{
    Camera*     camera;
    PointLight* lights;
    uint        lightCount;
    uint        _pad[3];
    float4      ambientColor;
    float4      viewPos;
};

struct DrawPushConstants<TFragmentData>
{
    ObjectData*    objects;
    TFragmentData* fragment;
};

[vk::push_constant] DrawPushConstants<FragmentData> pc;

// Material parameters, set from the material file (`data/materials/grid.mat`). The material
// editor reflects this block and exposes the same fields.
[[vk::binding(0, 1)]]
cbuffer MaterialUBO
{
    float4 albedo_color;   // base face colour
    float4 line_color;     // grid line colour
    float  cell_size;      // grid cell in metres
    float  line_width;     // line thickness in metres
    float2 _padding;
};

float4 unpackSnorm4x8(uint val)
{
    int4 i = int4(
        (int)(val << 24) >> 24,
        (int)(val << 16) >> 24,
        (int)(val << 8)  >> 24,
        (int)val         >> 24
    );
    return max(float4(-1.0), float4(i) * (1.0 / 127.0));
}

uint getAttributeDwordOffset(uint attrFlags, uint bitIndex)
{
    uint mask = attrFlags & ((1u << bitIndex) - 1u);
    return countbits(mask);
}

struct GridVSOutput
{
    float3 localPosition : TEXCOORD0;
    float3 worldPosition : TEXCOORD1;
    float3 objectScale   : TEXCOORD2;
    float3 normal        : NORMAL;
    float4 position      : SV_Position;
};

[shader("vertex")]
GridVSOutput mainVS(uint vertexID : SV_VertexID, uint drawID : SV_DrawIndex)
{
    ObjectData* object = &pc.objects[drawID];

    float3 local = object->geometry.positions[vertexID];
    float4 world = mul(float4(local, 1.0), object->model);

    GridVSOutput output;
    output.localPosition = local;
    output.worldPosition = world.xyz;
    // Rows of the row-vector model matrix are the transformed basis axes, so their lengths
    // are the per-axis scales. The grid is built in metres from them.
    output.objectScale = float3(length(object->model[0].xyz), length(object->model[1].xyz),
                                length(object->model[2].xyz));

    // Normal: the same SNorm8x4 unpack and normal-matrix transform Standard.slang uses.
    float3 localNormal = float3(0.0, 0.0, 1.0);
    if ((object->geometry.attribute_flags & (1u << ATTR_BIT_NORMAL)) != 0)
    {
        uint baseDwordOffset = vertexID * (object->geometry.attribute_stride >> 2);
        uint dwordOffset = getAttributeDwordOffset(object->geometry.attribute_flags, ATTR_BIT_NORMAL);
        localNormal = unpackSnorm4x8(object->geometry.attributes[baseDwordOffset + dwordOffset]).xyz;
    }
    output.normal = normalize(mul(localNormal, (float3x3)object->normalMatrix));

    output.position = mul(mul(world, pc.fragment->camera->view), pc.fragment->camera->proj);
    return output;
}

// Distance in metres from a coordinate to the nearest grid line, antialiased by the pixel
// footprint. `line_width` is the half-width the material asks for; the derivative floor
// keeps the line at least one pixel wide when the face is far away.
float GridLine(float2 coord, float cell, float halfWidth)
{
    float2 cellCoord = coord / cell;
    float2 toLine = (0.5 - abs(frac(cellCoord) - 0.5)) * cell;

    float2 footprint = float2(abs(ddx(coord.x)) + abs(ddy(coord.x)),
                              abs(ddx(coord.y)) + abs(ddy(coord.y)));
    float aa = max(max(footprint.x, footprint.y), halfWidth);
    return 1.0 - saturate(min(toLine.x, toLine.y) / max(aa, 1e-5));
}

[shader("fragment")]
float4 fsMain(GridVSOutput input) : SV_Target
{
    float3 normal = normalize(input.normal);
    float3 axis = abs(normal);
    float3 scale = input.objectScale;

    // Pick the face plane from the dominant local normal. The tangent coordinates are the
    // object-space axes of that plane in metres, shifted so 0 sits on the box border: a box
    // scaled to 1 m therefore shows its edge lines, a 3 m box shows three cells across.
    float2 coord;
    float2 faceScale;
    if (axis.x >= axis.y && axis.x >= axis.z)
    {
        coord = float2(input.localPosition.y * scale.y, input.localPosition.z * scale.z);
        faceScale = float2(scale.y, scale.z);
    }
    else if (axis.y >= axis.z)
    {
        coord = float2(input.localPosition.x * scale.x, input.localPosition.z * scale.z);
        faceScale = float2(scale.x, scale.z);
    }
    else
    {
        coord = float2(input.localPosition.x * scale.x, input.localPosition.y * scale.y);
        faceScale = float2(scale.x, scale.y);
    }
    coord += faceScale * 0.5;

    float cell = max(cell_size, 0.0001);
    float line = GridLine(coord, cell, max(line_width, 0.001) * 0.5);

    float3 albedo = lerp(albedo_color.rgb, line_color.rgb, line);

    // Simple directional key plus the scene ambient: level blocks read as solid surfaces
    // without pretending to be a PBR material.
    float3 lightDir = normalize(float3(0.35, 0.5, 0.8));
    float diffuse = saturate(dot(normal, lightDir));
    float3 color = albedo * (pc.fragment->ambientColor.rgb + 0.55 * diffuse + 0.18);

    return float4(color, albedo_color.a);
}
