struct Camera
{
    float4x4 view;
    float4x4 proj;
};

struct PickingObjectData
{
    float4x4 model;
    uint64_t entity_id;
    float3*  positions; // BDA адрес массива позиций
};

struct PushConstants
{
    PickingObjectData* objects;
    Camera*            camera;
};

[vk::push_constant] PushConstants pc;

struct VSOutput
{
    nointerpolation uint64_t entity_id : ENTITY_ID;
    float4          position  : SV_Position;
};

[shader("vertex")]
VSOutput mainVS(uint vertexID : SV_VertexID, uint drawID : SV_DrawIndex)
{
    PickingObjectData obj = pc.objects[drawID];
    Camera camera = *pc.camera;

    float3 localPos = obj.positions[vertexID];
    float4 worldPos = mul(float4(localPos, 1.0), obj.model);

    VSOutput output;
    output.position = mul(mul(worldPos, camera.view), camera.proj);
    output.entity_id = obj.entity_id;
    return output;
}

[shader("fragment")]
uint2 mainFS(VSOutput input) : SV_Target
{
    // Раскладываем uint64_t на два uint32_t для формата R32G32_UINT
    uint low  = uint(input.entity_id & 0xFFFFFFFF);
    uint high = uint(input.entity_id >> 32);

    return uint2(low, high);
}