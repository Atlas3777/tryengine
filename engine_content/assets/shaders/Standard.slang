// ============================================================================
// MODULE: Geometry & Common Types
// ============================================================================

static const uint ATTR_BIT_NORMAL    = 0; // SNorm8x4 (4 байта)
static const uint ATTR_BIT_TANGENT   = 1; // SNorm8x4 (4 байта)
static const uint ATTR_BIT_TEXCOORD0 = 2; // Half2    (4 байта)
static const uint ATTR_BIT_TEXCOORD1 = 3; // Half2    (4 байта)
static const uint ATTR_BIT_COLOR0    = 4; // UNorm8x4 (4 байта)
static const uint ATTR_BIT_JOINTS0   = 5; // UInt8x4  (4 байта)
static const uint ATTR_BIT_WEIGHTS0  = 6; // UNorm8x4 (4 байта)

struct GeometryPointers
{
    float3*   positions;        // BDA: Позиции (12 байт / вершина)
    uint32_t* attributes;       // BDA: Буфер 32-битных слов упакованных атрибутов
    uint      attribute_flags;  // Битовая маска MeshAttributeFlags
    uint      attribute_stride; // Размер атрибутов вершины в БАЙТАХ
};

struct ObjectData
{
    float4x4 model;        // 64 байта
    float4x4 normalMatrix; // 64 байта
    GeometryPointers geometry; // 24 байта
    uint2 _pad;
};

struct Camera
{
    float4x4 view;
    float4x4 proj;
};

// Обобщенная структура Push Constants (2 BDA указателя = 16 байт)
struct DrawPushConstants<TFragmentData>
{
    ObjectData*    objects;  // Указатель на буфер объектов
    TFragmentData* fragment; // Указатель на структуру с фрагментными данными
};

// --- Unpack Helpers (Совместимые с Slang / HLSL) ---

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

float4 unpackUnorm4x8(uint val)
{
    uint4 u = uint4(
        val & 0xFFu,
        (val >> 8u) & 0xFFu,
        (val >> 16u) & 0xFFu,
        (val >> 24u) & 0xFFu
    );
    return float4(u) * (1.0 / 255.0);
}

float2 unpackHalf2x16(uint val)
{
    return float2(
        f16tof32(val),
        f16tof32(val >> 16u)
    );
}

uint getAttributeDwordOffset(uint attrFlags, uint bitIndex)
{
    uint mask = attrFlags & ((1u << bitIndex) - 1u);
    return countbits(mask);
}

// --- Vertex Fetch Context ---

struct VertexContext
{
    uint        vertexID;
    ObjectData* object;
    Camera*     camera;

    uint baseDwordOffset;
    uint flags;

    static VertexContext create(uint vertexID, ObjectData* object, Camera* camera)
    {
        VertexContext ctx;
        ctx.vertexID = vertexID;
        ctx.object = object;
        ctx.camera = camera;
        ctx.baseDwordOffset = vertexID * (object->geometry.attribute_stride >> 2);
        ctx.flags = object->geometry.attribute_flags;
        return ctx;
    }

    float3 getLocalPosition() { return object->geometry.positions[vertexID]; }
    float4 getWorldPosition() { return mul(float4(getLocalPosition(), 1.0), object->model); }
    float4 getClipPosition(float4 worldPos) { return mul(mul(worldPos, camera->view), camera->proj); }

    float3 getNormal(float3 defaultVal = float3(0.0, 1.0, 0.0))
    {
        if ((flags & (1u << ATTR_BIT_NORMAL)) == 0) return defaultVal;
        uint dwordOffset = getAttributeDwordOffset(flags, ATTR_BIT_NORMAL);
        uint rawVal = object->geometry.attributes[baseDwordOffset + dwordOffset];
        float3 localNormal = unpackSnorm4x8(rawVal).xyz;
        return normalize(mul(localNormal, (float3x3)object->normalMatrix));
    }

    // Сохранен для использования во внешнем модуле/других пайплайнах
    float4 getColor(float4 defaultVal = float4(1.0))
    {
        if ((flags & (1u << ATTR_BIT_COLOR0)) == 0) return defaultVal;
        uint dwordOffset = getAttributeDwordOffset(flags, ATTR_BIT_COLOR0);
        uint rawVal = object->geometry.attributes[baseDwordOffset + dwordOffset];
        return unpackUnorm4x8(rawVal);
    }

    float2 getTexCoord0(float2 defaultVal = float2(0.0))
    {
        if ((flags & (1u << ATTR_BIT_TEXCOORD0)) == 0) return defaultVal;
        uint dwordOffset = getAttributeDwordOffset(flags, ATTR_BIT_TEXCOORD0);
        uint rawVal = object->geometry.attributes[baseDwordOffset + dwordOffset];
        return unpackHalf2x16(rawVal);
    }
};

// --- Generic Interface ---

interface IVertexOutput
{
    static This assemble(VertexContext ctx);
};

// Обобщенный вершинный хелпер
TOutput vsMainGeneric<TOutput : IVertexOutput>(uint vertexID, uint drawID)
{
    ObjectData* obj = &pc.objects[drawID];
    Camera* camera = pc.fragment->camera;

    VertexContext ctx = VertexContext.create(vertexID, obj, camera);
    return TOutput.assemble(ctx);
}


// ============================================================================
// MATERIAL / FRAGMENT SHADER SECTION
// ============================================================================

struct PointLight
{
    float4 position_radius;
    float4 color_intensity;
};

// Фрагментная структура данных для этого конкретного шейдера
struct FragmentData
{
    Camera*     camera;       // 8 байт (0..7)
    PointLight* lights;       // 8 байт (8..15)
    uint        lightCount;   // 4 байта (16..19)
    uint3       _pad;         // 12 байт (20..31) -> выравнивает следующий float4 по 32 байтам
    float4      ambientColor; // 16 байт (32..47)
    float4      viewPos;      // 16 байт (48..63)
};

// Специализация Push Constants текущей фрагментной структурой
[vk::push_constant] DrawPushConstants<FragmentData> pc;

[[vk::binding(0, 1)]]
cbuffer MaterialUBO
{
    float4 albedo_color;
    float  roughness;
    float  metallic;
    float2 _padding;
};

[[vk::binding(1, 1)]]
Sampler2D albedo_map;

struct PBRVSOutput : IVertexOutput
{
    float3 outFragPos  : TEXCOORD1;
    float3 outNormal   : NORMAL;
    float2 outTexCoord : TEXCOORD0;

    float4 position    : SV_Position;

    static PBRVSOutput assemble(VertexContext ctx)
    {
        PBRVSOutput output;

        float4 worldPos = ctx.getWorldPosition();
        output.outFragPos  = worldPos.xyz;
        output.outNormal   = ctx.getNormal();
        output.outTexCoord = ctx.getTexCoord0();
        output.position    = ctx.getClipPosition(worldPos);

        return output;
    }
};

[shader("vertex")]
PBRVSOutput mainVS(uint vertexID : SV_VertexID, uint drawID : SV_DrawIndex)
{
    return vsMainGeneric<PBRVSOutput>(vertexID, drawID);
}

[shader("fragment")]
float4 fsMain(PBRVSOutput input) : SV_Target
{
    float3 normal = normalize(input.outNormal);
    float3 viewDir = normalize(pc.fragment->viewPos.xyz - input.outFragPos);

    float3 ambient = pc.fragment->ambientColor.rgb;
    float3 diffuseAccum = float3(0.0);
    float3 specularAccum = float3(0.0);

    float specPower = exp2(10.0 * (1.0 - roughness) + 1.0);

    for (uint i = 0; i < pc.fragment->lightCount; i++)
    {
        PointLight light = pc.fragment->lights[i];

        float3 lightVec = light.position_radius.xyz - input.outFragPos;
        float dist = length(lightVec);
        float radius = light.position_radius.w;

        float safeDist = max(dist, 0.0001);
        float3 lightDir = lightVec / safeDist;

        float attenuation = saturate(1.0 - (dist / max(radius, 0.0001)));
        float diff = max(dot(normal, lightDir), 0.0);

        float3 halfDir = normalize(lightDir + viewDir);
        float spec = pow(max(dot(normal, halfDir), 0.0), specPower);

        float intensity = light.color_intensity.w;
        float3 lightColor = light.color_intensity.rgb * intensity * attenuation;

        diffuseAccum += lightColor * diff;
        specularAccum += lightColor * spec * (1.0 - metallic) * 0.5;
    }

    float4 texColor = albedo_map.Sample(input.outTexCoord);
    float3 finalLighting = ambient + diffuseAccum + specularAccum;

    return float4(finalLighting, 1.0) * texColor;
}