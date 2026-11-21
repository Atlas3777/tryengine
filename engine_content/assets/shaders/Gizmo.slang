// Редакторский гизмо трансформа. Поток треугольников ручек (стрелок, плоскостей, колец,
// кубов), собранный на CPU, кладётся во временный BDA-буфер и растеризуется поверх цвета
// сцены без depth-теста: гизмо всегда видно, а его picking не зависит от буфера глубины.
//
// Цвета вершин премультиплицированы: пасс блендит (ONE, ONE_MINUS_SRC_ALPHA), поэтому
// альфа сцены остаётся 1.0 (текстура вьюпорта рисуется через ImGui).

struct GizmoCamera
{
    float4x4 view;
    float4x4 proj;
    float4   position;
};

struct GizmoVertex
{
    float4 position; // xyz в мире
    float4 normal;   // xyz мировая нормаль
    float4 color;    // rgba без премультипликации
};

struct GizmoPushConstants
{
    GizmoVertex* vertices;
    GizmoCamera* camera;
};

[vk::push_constant] GizmoPushConstants pc;

struct VSOutput
{
    float4 position : SV_Position;
    float3 worldPosition;
    float3 normal;
    float4 color;
};

[shader("vertex")]
VSOutput vsMain(uint vertexID : SV_VertexID)
{
    GizmoVertex vertex = pc.vertices[vertexID];

    VSOutput output;
    output.position = mul(mul(float4(vertex.position.xyz, 1.0), pc.camera->view), pc.camera->proj);
    output.worldPosition = vertex.position.xyz;
    output.normal = vertex.normal.xyz;
    output.color = vertex.color;
    return output;
}

[shader("fragment")]
float4 fsMain(VSOutput input) : SV_Target
{
    float3 normal = normalize(input.normal);
    float3 light = normalize(float3(0.35, -0.55, 0.75));
    float3 view = normalize(pc.camera->position.xyz - input.worldPosition);

    // Двустороннее освещение: плоскости и внутренняя сторона колец видны с обеих сторон.
    float diffuse = abs(dot(normal, light));
    // Мягкий rim отделяет гизмо от сцены любой яркости без отдельного outline-пасса.
    float rim = pow(1.0 - saturate(abs(dot(normal, view))), 2.0);

    float3 color = input.color.rgb * (0.72 + 0.28 * diffuse);
    color += input.color.rgb * rim * 0.18 + rim * 0.04;
    return float4(color * input.color.a, input.color.a);
}
