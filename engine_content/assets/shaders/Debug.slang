struct DebugLine {
    float4 start;
    float4 end;
    uint colorPacked; // 4 байта RGBA
    uint3 _padding;
};

// Storage Buffer привязан на slot 0 (t0)
[[vk::binding(0, 0)]]
StructuredBuffer<DebugLine> debugLines : register(t0, space0);

// Camera UBO привязан через PushGPUVertexUniformData slot 0 (b0)
[[vk::binding(0, 1)]]
cbuffer Camera : register(b0, space0) {
    float4x4 view;
    float4x4 proj;
};

struct VSOutput {
    float4 position : SV_Position;
    float4 color    : COLOR0;
};

[shader("vertex")]
VSOutput vsMain(uint vertexID : SV_VertexID) {
    VSOutput output;

    uint lineIndex = vertexID / 6;
    uint quadVertex = vertexID % 6;

    DebugLine lineData = debugLines[lineIndex];

    // Переводим концы отрезка в View Space
    float4 vStart = mul(lineData.start, view);
    float4 vEnd   = mul(lineData.end, view);

    // Вычисляем направление линии и перпендикуляр для создания толщины
    float3 dir = normalize(vEnd.xyz - vStart.xyz);
    float3 normal = normalize(cross(dir, float3(0, 0, 1)));
    float thickness = 0.1; // Толщина линии в мировых/view юнитах

    // Смещение для 6 вершин прямоугольника (Quad
    float3 offsets[6] = {
        -normal * thickness,
         normal * thickness,
        -normal * thickness + (vEnd.xyz - vStart.xyz),
        -normal * thickness + (vEnd.xyz - vStart.xyz),
         normal * thickness,
         normal * thickness + (vEnd.xyz - vStart.xyz)
    };

    float4 finalViewPos = float4(vStart.xyz + offsets[quadVertex], 1.0);
    output.position = mul(finalViewPos, proj);

    // Распаковка цвета RGBA (uint8_t[4])
    uint c = lineData.colorPacked;
    output.color = float4(
        float(c & 0xFFu) / 255.0,
        float((c >> 8u) & 0xFFu) / 255.0,
        float((c >> 16u) & 0xFFu) / 255.0,
        float((c >> 24u) & 0xFFu) / 255.0
    );

    return output;
}

[shader("fragment")]
float4 fsMain(VSOutput input) : SV_Target {
    return input.color;
}