struct VSInput {
    float3 inPos : POSITION;
    float4 inNormal : NORMAL;
    float4 inColor : COLOR;
    float2 inTexCoord : TEXCOORD0;
};

struct VSOutput {
    float3 outFragPos : TEXCOORD1;
    float3 outNormal : NORMAL;
    float4 outColor : COLOR0;
    float2 outTexCoord : TEXCOORD0;

    float4 position : SV_Position;
};

[[vk::binding(0, 1)]]
cbuffer UBO {
    float4x4 view;
    float4x4 proj;
    float4x4 model;
    float4x4 normalMatrix;
};

[shader("vertex")]
VSOutput main(VSInput input) {
    VSOutput output;

    float4 worldPos = mul(float4(input.inPos, 1.0), model);
    output.outFragPos = worldPos.xyz;

    output.outNormal = normalize(mul(input.inNormal.xyz, (float3x3)normalMatrix));

    output.outColor = input.inColor;
    output.outTexCoord = input.inTexCoord;

    // 2. Переводим в View Space (v * view)
    float4 viewPos = mul(worldPos, view);

    // 3. Переводим в Clip Space (v * proj)
    output.position = mul(viewPos, proj);

    return output;
}