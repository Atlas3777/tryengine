// Mesh fetch shared by every shader that reads BDA vertex data.
//
// Positions are a float3 array; per-vertex attributes are a stream of 32-bit words whose
// layout is described by MeshAttributeFlags. The unpack helpers and the dword offset
// calculation live here so a material shader does not have to repeat the vertex path.
//
// See engine_source/graphics/src/OpaquePass.cpp for the C++ side of the layout and for
// MeshAttributeFlags.

import engine.common;

public static const uint ATTR_BIT_NORMAL    = 0; // SNorm8x4 (4 bytes)
public static const uint ATTR_BIT_TANGENT   = 1; // SNorm8x4 (4 bytes)
public static const uint ATTR_BIT_TEXCOORD0 = 2; // Half2    (4 bytes)
public static const uint ATTR_BIT_TEXCOORD1 = 3; // Half2    (4 bytes)
public static const uint ATTR_BIT_COLOR0    = 4; // UNorm8x4 (4 bytes)
public static const uint ATTR_BIT_JOINTS0   = 5; // UInt8x4  (4 bytes)
public static const uint ATTR_BIT_WEIGHTS0  = 6; // UNorm8x4 (4 bytes)

public struct GeometryPointers
{
    public float3*   positions;        // BDA: positions, 12 bytes per vertex
    public uint32_t* attributes;       // BDA: packed attribute words
    public uint      attribute_flags;  // MeshAttributeFlags bit mask
    public uint      attribute_stride; // size of the attribute block per vertex, in bytes
};

public struct ObjectData
{
    public float4x4 model;
    public float4x4 normalMatrix;
    public GeometryPointers geometry;
    public uint2 _pad;
};

// --- Unpack helpers (Slang/HLSL compatible) ---

public float4 unpackSnorm4x8(uint val)
{
    int4 i = int4(
        (int)(val << 24) >> 24,
        (int)(val << 16) >> 24,
        (int)(val << 8)  >> 24,
        (int)val         >> 24
    );
    return max(float4(-1.0), float4(i) * (1.0 / 127.0));
}

public float4 unpackUnorm4x8(uint val)
{
    uint4 u = uint4(
        val & 0xFFu,
        (val >> 8u) & 0xFFu,
        (val >> 16u) & 0xFFu,
        (val >> 24u) & 0xFFu
    );
    return float4(u) * (1.0 / 255.0);
}

public float2 unpackHalf2x16(uint val)
{
    return float2(
        f16tof32(val),
        f16tof32(val >> 16u)
    );
}

public uint getAttributeDwordOffset(uint attrFlags, uint bitIndex)
{
    uint mask = attrFlags & ((1u << bitIndex) - 1u);
    return countbits(mask);
}

// State of one vertex fetch. Created once per vertex by the vertex entry point; the helpers
// are the only place that knows how attributes are packed.
public struct VertexContext
{
    public uint        vertexID;
    public ObjectData* object;
    public Camera*     camera;

    public uint baseDwordOffset;
    public uint flags;

    public static VertexContext create(uint vertexID, ObjectData* object, Camera* camera)
    {
        VertexContext ctx;
        ctx.vertexID = vertexID;
        ctx.object = object;
        ctx.camera = camera;
        ctx.baseDwordOffset = vertexID * (object->geometry.attribute_stride >> 2);
        ctx.flags = object->geometry.attribute_flags;
        return ctx;
    }

    public float3 getLocalPosition() { return object->geometry.positions[vertexID]; }

    public float4 getWorldPosition() { return mul(float4(getLocalPosition(), 1.0), object->model); }

    public float4 getClipPosition(float4 worldPos) { return mul(mul(worldPos, camera->view), camera->proj); }

    public float3 getObjectScale()
    {
        // Rows of the row-vector model matrix are the transformed basis axes, so their
        // lengths are the per-axis scales.
        return float3(length(object->model[0].xyz), length(object->model[1].xyz),
                      length(object->model[2].xyz));
    }

    public float3 getLocalNormal(float3 defaultVal = float3(0.0, 1.0, 0.0))
    {
        if ((flags & (1u << ATTR_BIT_NORMAL)) == 0) return defaultVal;
        uint dwordOffset = getAttributeDwordOffset(flags, ATTR_BIT_NORMAL);
        uint rawVal = object->geometry.attributes[baseDwordOffset + dwordOffset];
        return normalize(unpackSnorm4x8(rawVal).xyz);
    }

    public float3 getNormal(float3 defaultVal = float3(0.0, 1.0, 0.0))
    {
        float3 localNormal = getLocalNormal(defaultVal);
        return normalize(mul(localNormal, (float3x3)object->normalMatrix));
    }

    public float4 getColor(float4 defaultVal = float4(1.0))
    {
        if ((flags & (1u << ATTR_BIT_COLOR0)) == 0) return defaultVal;
        uint dwordOffset = getAttributeDwordOffset(flags, ATTR_BIT_COLOR0);
        uint rawVal = object->geometry.attributes[baseDwordOffset + dwordOffset];
        return unpackUnorm4x8(rawVal);
    }

    public float2 getTexCoord0(float2 defaultVal = float2(0.0))
    {
        if ((flags & (1u << ATTR_BIT_TEXCOORD0)) == 0) return defaultVal;
        uint dwordOffset = getAttributeDwordOffset(flags, ATTR_BIT_TEXCOORD0);
        uint rawVal = object->geometry.attributes[baseDwordOffset + dwordOffset];
        return unpackHalf2x16(rawVal);
    }
};
