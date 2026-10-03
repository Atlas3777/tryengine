struct DebugLine
{
    float3 start; // 12
    uint colorPacked; // 4
    float3 end;   // 12
    uint _pad; // 4
}; // 32

struct Camera
{
    float4x4 view;
    float4x4 proj;
};

struct VSOutput
{
    float4 position : SV_Position;
    float4 color    : COLOR0;
};

struct DebugPushConstants
{
    DebugLine* lines;      // BDA указатель на массив линий
    Camera*    camera;     // BDA указатель на Камеру
    float2     screenSize; // Размер вьюпорта (ширина, высота)
    float      thickness;  // Толщина линии в ПИКСЕЛЯХ (например, 20.0)
    uint       _padding;
};

[vk::push_constant] DebugPushConstants pc;

[shader("vertex")]
VSOutput vsMain(uint vertexID : SV_VertexID)
{
    VSOutput output;

    uint lineIndex = vertexID / 6;
    uint quadVertex = vertexID % 6;

    DebugLine lineData = pc.lines[lineIndex];

    // 1. Перевод точек в Clip Space с правильным порядком умножения (как в вашем PBR-шейдере)
    float4 clipStart = mul(mul(float4(lineData.start, 1.0), pc.camera->view), pc.camera->proj);
    float4 clipEnd   = mul(mul(float4(lineData.end, 1.0), pc.camera->view), pc.camera->proj);

    // 2. Перевод в NDC (-1..1)
    float2 ndcStart = clipStart.xy / max(clipStart.w, 0.00001);
    float2 ndcEnd   = clipEnd.xy / max(clipEnd.w, 0.00001);

    // 3. Вектор направления линии на экране с учётом Aspect Ratio
    float2 deltaNDC = ndcEnd - ndcStart;
    float aspect = pc.screenSize.x / max(pc.screenSize.y, 1.0);

    // Корректируем направления под Aspect Ratio
    float2 screenDelta = float2(deltaNDC.x * aspect, deltaNDC.y);
    float len = length(screenDelta);
    float2 screenDir = (len > 1e-5) ? (screenDelta / len) : float2(1.0, 0.0);

    // Перпендикуляр в экранных координатах
    float2 screenNormal = float2(-screenDir.y, screenDir.x);

    // Перевод перпендикуляра обратно в NDC смещение
    float2 ndcNormal = float2(screenNormal.x / aspect, screenNormal.y);

    // 4. Определяем вершину квада
    // 0, 1, 4 -> старт линии; 2, 3, 5 -> конец линии
    bool isEnd = (quadVertex == 2 || quadVertex == 3 || quadVertex == 5);
    // 1, 4, 5 -> смещение вправо; 0, 2, 3 -> смещение влево
    float side = (quadVertex == 1 || quadVertex == 4 || quadVertex == 5) ? 1.0 : -1.0;

    float4 baseClip = isEnd ? clipEnd : clipStart;

    // Смещение в NDC: (thickness / screenSize) дает размер полупикселей в NDC (диапазон NDC = 2.0)
    float2 offsetNDC = ndcNormal * (pc.thickness / pc.screenSize) * side;

    // Применяем смещение к Clip-позиции до деления на W
    baseClip.xy += offsetNDC * baseClip.w;

    output.position = baseClip;

    // 5. Распаковка цвета RGBA
    uint c = lineData.colorPacked;
    output.color = float4(
        float(c & 0xFFu) / 255.0f,
        float((c >> 8u) & 0xFFu) / 255.0f,
        float((c >> 16u) & 0xFFu) / 255.0f,
        float((c >> 24u) & 0xFFu) / 255.0f
    );

    return output;
}

[shader("fragment")]
float4 fsMain(VSOutput input) : SV_Target
{
    return input.color;
}