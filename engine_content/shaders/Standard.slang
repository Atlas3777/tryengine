// Default opaque PBR material (Cook-Torrance GGX, metallic/roughness workflow).
//
// Vertex data, the object table, the fragment block and the push-constant root come from
// engine.opaque / engine.geometry; this file owns only the material parameters, the vertex
// entry point and the BRDF.

import engine.opaque;
import engine.geometry;

// The fragment stage samples albedo_map with the mesh UV and lights with the mesh normal.
// The material picker only offers this shader for meshes that have both.
public static const uint MESH_ATTRIBUTES = ATTR_BIT_NORMAL | ATTR_BIT_TEXCOORD0;

// ============================================================================
// MATERIAL
// ============================================================================

[[vk::binding(0, 1)]]
cbuffer MaterialUBO
{
    float4 albedo_color;   // albedo texture tint (rgb) + opacity (a)
    float  roughness;      // 0 = mirror, 1 = fully rough
    float  metallic;       // 0 = dielectric, 1 = metal
    float2 _padding;
};

[[vk::binding(1, 1)]]
Sampler2D albedo_map;

static const float PI            = 3.14159265358979;
static const float MIN_ROUGHNESS = 0.045;  // otherwise the highlight falls below a pixel -> NaN/aliasing
static const float DIELECTRIC_F0 = 0.04;

// --- BRDF terms ---

float3 fresnelSchlick(float cosTheta, float3 F0)
{
    float t = 1.0 - cosTheta;
    float t2 = t * t;
    return F0 + (1.0 - F0) * (t2 * t2 * t);
}

// Fresnel with roughness for ambient/IBL, so rough metal does not mirror its rim.
float3 fresnelSchlickRoughness(float cosTheta, float3 F0, float rough)
{
    float t = 1.0 - cosTheta;
    float t2 = t * t;
    return F0 + (max(float3(1.0 - rough), F0) - F0) * (t2 * t2 * t);
}

// NDF: roughness directly sets the width and height of the highlight.
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

// Microfacet self-shadowing; grows with roughness.
float geometrySmith(float NdotV, float NdotL, float rough)
{
    float r = rough + 1.0;
    float k = (r * r) / 8.0;               // k for direct lights (Epic)
    return geometrySchlickGGX(NdotV, k) * geometrySchlickGGX(NdotL, k);
}

// ============================================================================
// ENTRY POINTS
// ============================================================================

[shader("vertex")]
PBRVSOutput mainVS(uint vertexID : SV_VertexID, uint drawID : SV_DrawIndex)
{
    return vsMainGeneric<PBRVSOutput>(vertexID, drawID);
}

[shader("fragment")]
float4 fsMain(PBRVSOutput input) : SV_Target
{
    // ---- 1. Material: albedo_color tints the actual texture ----
    float4 baseColor = albedo_map.Sample(input.outTexCoord) * albedo_color;
    float3 albedo    = baseColor.rgb;

    // Local names so the fields are not shadowed by the cbuffer.
    float metal = saturate(metallic);
    float rough = clamp(roughness, MIN_ROUGHNESS, 1.0);

    float3 N = normalize(input.outNormal);
    float3 V = normalize(pc.fragment->viewPos.xyz - input.outFragPos);
    float  NdotV = max(dot(N, V), 1e-4);

    // ---- 2. metallic sets the reflection colour F0 ----
    // dielectric -> 0.04 (colourless highlight), metal -> the albedo colour itself
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
        if (NdotL <= 0.0) continue;            // light behind the surface: neither diffuse nor specular
        NdotL = saturate(NdotL);

        float NdotH = saturate(dot(N, H));
        float VdotH = saturate(dot(V, H));

        // Attenuation is kept as in the original, so scene brightness does not change.
        float  attenuation = saturate(1.0 - dist / radius);
        float3 radiance    = light.color_intensity.rgb * light.color_intensity.w * attenuation;

        float  D = distributionGGX(NdotH, rough);
        float  G = geometrySmith(NdotV, NdotL, rough);
        float3 F = fresnelSchlick(VdotH, F0);

        float3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 1e-4);

        // ---- 3. Energy conservation: metal has no diffuse, reflected light is not scattered ----
        float3 kD = (1.0 - F) * (1.0 - metal);

        diffuseAccum  += kD * (albedo / PI) * radiance * NdotL;
        specularAccum += specular          * radiance * NdotL;
    }

    // ---- Ambient also depends on the material (a stub until IBL lands) ----
    float3 F_amb  = fresnelSchlickRoughness(NdotV, F0, rough);
    float3 kD_amb = (1.0 - F_amb) * (1.0 - metal);
    float3 ambient = pc.fragment->ambientColor.rgb * albedo * kD_amb;
    // Cheap pseudo-IBL: without it a metal object in shadow would be black.
    float3 ambientSpec = pc.fragment->ambientColor.rgb * F_amb * (1.0 - rough) * 0.5;

    float3 color = ambient + ambientSpec + diffuseAccum + specularAccum;

    return float4(color, baseColor.a);       // alpha = albedo_color.a * tex.a (needs blend state)
}
