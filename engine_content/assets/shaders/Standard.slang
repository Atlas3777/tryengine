[__AttributeUsage(_AttributeTargets.Var)]
struct PerObjAttribute {}

[__AttributeUsage(_AttributeTargets.Var)]
struct PassAttribute {}

[__AttributeUsage(_AttributeTargets.Var)]
struct MaterialAttribute {}


struct VSInput {
    float3 inPos      : POSITION;
    float4 inNormal   : NORMAL;
    float4 inColor    : COLOR;
    float2 inTexCoord : TEXCOORD0;
};

struct VSOutput {
    float3 outFragPos  : TEXCOORD1;
    float3 outNormal   : NORMAL;
    float4 outColor    : COLOR0;
    float2 outTexCoord : TEXCOORD0;

    float4 position : SV_Position;
};

// Vertex Shader UBO 0 (Set 1, Binding 0)
[PerObj]
[[vk::binding(0, 1)]]
cbuffer FrameUBO {
    float4x4 model;
    float4x4 normalMatrix;
};

// Vertex Shader UBO 1 (Set 1, Binding 1) -- перенесено из Set 2 в Set 1
[Pass]
[[vk::binding(1, 1)]]
cbuffer Camera {
    float4x4 view;
    float4x4 proj;
};

[shader("vertex")]
VSOutput vsMain(VSInput input) {
    VSOutput output;

    float4 worldPos = mul(float4(input.inPos, 1.0), model);
    output.outFragPos = worldPos.xyz;

    output.outNormal = normalize(mul(input.inNormal.xyz, (float3x3)normalMatrix));
    output.outColor = input.inColor;
    output.outTexCoord = input.inTexCoord;

    float4 viewPos = mul(worldPos, view);
    output.position = mul(viewPos, proj);

    return output;
}

// Fragment Shader Sampler 0 (Set 2, Binding 0)
[Material]
[[vk::binding(0, 2)]]
Sampler2D albedo_map;

struct PointLight {
    float4 position_radius;
    float4 color_intensity;
};

// Fragment Shader Storage Buffer 0 (Set 2, Binding 1)
[Pass]
[[vk::binding(1, 2)]]
StructuredBuffer<PointLight> PointLightBuffer;

// Fragment Shader UBO 0 (Set 3, Binding 0)
[Pass]
[[vk::binding(0, 3)]]
cbuffer GlobalLightUBO {
    float4 ambientColor;
    float4 viewPos;
};

// Fragment Shader UBO 1 (Set 3, Binding 1)
[Material]
[[vk::binding(1, 3)]]
cbuffer MaterialUBO {
    float4 albedo_color;
    float  roughness;
    float  metallic;
    float2 _padding;
};

[shader("fragment")]
float4 fsMain(VSOutput input) : SV_Target {
    float3 normal = normalize(input.outNormal);
    float3 viewDir = normalize(viewPos.xyz - input.outFragPos);

    float3 ambient = ambientColor.rgb;
    float3 diffuseAccum = float3(0.0);
    float3 specularAccum = float3(0.0);

    uint lightCount, stride;
    PointLightBuffer.GetDimensions(lightCount, stride);

    for (uint i = 0; i < lightCount; i++) {
        PointLight light = PointLightBuffer[i];

        float3 lightVec = light.position_radius.xyz - input.outFragPos;
        float dist = length(lightVec);
        float radius = light.position_radius.w;

        if (dist > radius || radius <= 0.0) continue;

        float3 lightDir = lightVec / dist;

        float diff = max(dot(normal, lightDir), 0.0);

        float3 halfDir = normalize(lightDir + viewDir);
        float spec = pow(max(dot(normal, halfDir), 0.0), 32.0);

        float attenuation = 1.0 - (dist / radius);
        float intensity = light.color_intensity.w;

        diffuseAccum += light.color_intensity.rgb * diff * intensity * attenuation;
        specularAccum += light.color_intensity.rgb * spec * intensity * attenuation * 0.5;
    }

    float4 texColor = albedo_map.Sample(input.outTexCoord);
    if (length(texColor.rgb) == 0.0) {
        texColor = (length(input.outColor.rgb) > 0.0) ? input.outColor : float4(1.0, 1.0, 1.0, 1.0);
    }

    float3 finalLighting = ambient + diffuseAccum + specularAccum;
    return float4(finalLighting, 1.0) * texColor * albedo_color;
}