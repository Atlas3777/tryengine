// Contract of the opaque material pipeline: the per-fragment block the pass fills, the
// push-constant root, and the generic PBR vertex output.
//
// A material shader that uses the same push constants (the grid material, for example) imports
// this module as well, so the object table and the fragment data have one declaration on the
// Slang side and one in OpaquePass.cpp.

import engine.common;
import engine.geometry;

public struct PointLight
{
    public float4 position_radius;
    public float4 color_intensity;
};

// Mirrors FragmentDataGPU in OpaquePass.cpp; the pass fills the same push-constant block for
// every material shader.
public struct FragmentData
{
    public Camera*     camera;
    public PointLight* lights;
    public uint        lightCount;
    public uint        _pad[3];
    public float4      ambientColor;
    public float4      viewPos;
};

public struct DrawPushConstants<TFragmentData>
{
    public ObjectData*    objects;
    public TFragmentData* fragment;
};

[vk::push_constant] public DrawPushConstants<FragmentData> pc;

// --- Generic vertex stage ---

public interface IVertexOutput
{
    static This assemble(VertexContext ctx);
};

public TOutput vsMainGeneric<TOutput : IVertexOutput>(uint vertexID, uint drawID)
{
    ObjectData* obj = &pc.objects[drawID];
    Camera* camera = pc.fragment->camera;

    VertexContext ctx = VertexContext.create(vertexID, obj, camera);
    return TOutput.assemble(ctx);
}

public struct PBRVSOutput : IVertexOutput
{
    public float3 outFragPos  : TEXCOORD1;
    public float3 outNormal   : NORMAL;
    public float2 outTexCoord : TEXCOORD0;

    public float4 position    : SV_Position;

    public static PBRVSOutput assemble(VertexContext ctx)
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
