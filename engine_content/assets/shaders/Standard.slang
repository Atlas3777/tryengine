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

// ============================================================================
// MATERIAL / FRAGMENT SHADER SECTION
// Cook-Torrance GGX, metallic/roughness workflow
// ============================================================================

struct PointLight
{
    float4 position_radius;
    float4 color_intensity;
};

struct FragmentData
{
    Camera*     camera;       // 0..7
    PointLight* lights;       // 8..15
    uint        lightCount;   // 16..19
    uint        _pad[3];      // 20..31  (скалярный массив, align=4 — см. примечание ниже)
    float4      ambientColor; // 32..47
    float4      viewPos;      // 48..63
};

[vk::push_constant] DrawPushConstants<FragmentData> pc;

[[vk::binding(0, 1)]]
cbuffer MaterialUBO
{
    float4 albedo_color;   // тонировка альбедо-текстуры (rgb) + прозрачность (a)
    float  roughness;      // 0 = зеркало, 1 = полностью матовый
    float  metallic;       // 0 = диэлектрик, 1 = металл
    float2 _padding;
};

[[vk::binding(1, 1)]]
Sampler2D albedo_map;

static const float PI            = 3.14159265358979;
static const float MIN_ROUGHNESS = 0.045;  // иначе блик уходит в субпиксель -> NaN/алиасинг
static const float DIELECTRIC_F0 = 0.04;

// --- BRDF terms ---

float3 fresnelSchlick(float cosTheta, float3 F0)
{
    float t = 1.0 - cosTheta;
    float t2 = t * t;
    return F0 + (1.0 - F0) * (t2 * t2 * t);
}

// Френель с учётом шероховатости — для ambient/IBL, чтобы матовый металл не «зеркалил» края
float3 fresnelSchlickRoughness(float cosTheta, float3 F0, float rough)
{
    float t = 1.0 - cosTheta;
    float t2 = t * t;
    return F0 + (max(float3(1.0 - rough), F0) - F0) * (t2 * t2 * t);
}

// NDF: roughness напрямую задаёт ширину и высоту блика
float distributionGGX(float NdotH, float rough)
{
    float a  = rough * rough;
    float a2 = a * a;
    float d  = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 1e-7);
}

float geometrySchlickGGX(float NdotX, float k)
{
    return NdotX / (NdotX * (1.0 - k) + k);
}

// Самозатенение микрограней: растёт с roughness
float geometrySmith(float NdotV, float NdotL, float rough)
{
    float r = rough + 1.0;
    float k = (r * r) / 8.0;               // k для прямых источников (Epic)
    return geometrySchlickGGX(NdotV, k) * geometrySchlickGGX(NdotL, k);
}

[shader("fragment")]
float4 fsMain(PBRVSOutput input) : SV_Target
{
    // ---- 1. Материал: albedo_color реально тонирует текстуру ----
    float4 baseColor = albedo_map.Sample(input.outTexCoord) * albedo_color;
    float3 albedo    = baseColor.rgb;

    // локальные имена, чтобы не затенять поля cbuffer
    float metal = saturate(metallic);
    float rough = clamp(roughness, MIN_ROUGHNESS, 1.0);

    float3 N = normalize(input.outNormal);
    float3 V = normalize(pc.fragment->viewPos.xyz - input.outFragPos);
    float  NdotV = max(dot(N, V), 1e-4);

    // ---- 2. metallic задаёт цвет отражения F0 ----
    // диэлектрик -> 0.04 (бесцветный блик), металл -> цвет самого альбедо
    float3 F0 = lerp(float3(DIELECTRIC_F0), albedo, metal);

    float3 diffuseAccum  = float3(0.0);
    float3 specularAccum = float3(0.0);

    for (uint i = 0; i < pc.fragment->lightCount; i++)
    {
        PointLight light = pc.fragment->lights[i];

        float3 lightVec = light.position_radius.xyz - input.outFragPos;
        float  dist     = length(lightVec);
        float  radius   = max(light.position_radius.w, 0.0001);
        float3 L        = lightVec / max(dist, 0.0001);
        float3 H        = normalize(V + L);

        float NdotL = dot(N, L);
        if (NdotL <= 0.0) continue;            // свет за поверхностью — ни диффуза, ни блика
        NdotL = saturate(NdotL);

        float NdotH = saturate(dot(N, H));
        float VdotH = saturate(dot(V, H));

        // затухание оставлено как в исходнике, чтобы не менять яркость сцены
        float  attenuation = saturate(1.0 - dist / radius);
        float3 radiance    = light.color_intensity.rgb * light.color_intensity.w * attenuation;

        float  D = distributionGGX(NdotH, rough);
        float  G = geometrySmith(NdotV, NdotL, rough);
        float3 F = fresnelSchlick(VdotH, F0);

        float3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 1e-4);

        // ---- 3. Сохранение энергии: металл не имеет диффуза, отражённое не рассеивается ----
        float3 kD = (1.0 - F) * (1.0 - metal);

        diffuseAccum  += kD * (albedo / PI) * radiance * NdotL;
        specularAccum += specular          * radiance * NdotL;
    }

    // ---- Ambient тоже зависит от материала (заглушка до появления IBL) ----
    float3 F_amb  = fresnelSchlickRoughness(NdotV, F0, rough);
    float3 kD_amb = (1.0 - F_amb) * (1.0 - metal);
    float3 ambient = pc.fragment->ambientColor.rgb * albedo * kD_amb;
    // дешёвый псевдо-IBL: без него металлический объект в тени был бы чёрным
    float3 ambientSpec = pc.fragment->ambientColor.rgb * F_amb * (1.0 - rough) * 0.5;

    float3 color = ambient + ambientSpec + diffuseAccum + specularAccum;

    // Если таргет не sRGB и нет пост-тонмапа — раскомментируй:
    // color = color / (color + 1.0);          // Reinhard
    // color = pow(saturate(color), 1.0 / 2.2);

    return float4(color, baseColor.a);       // альфа из albedo_color.a * tex.a (нужен blend state)
}