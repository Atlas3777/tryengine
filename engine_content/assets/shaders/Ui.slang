// Game UI: one shader for filled rounded rectangles, SDF text and textured quads.
//
// Vertices come from a BDA buffer (no vertex input), one draw call per material and clip
// rectangle. Positions are render-target pixels with the origin at the top-left, so the
// projection is the same one ImGui's Vulkan backend uses.
//
// The vertex index must be the absolute one: the pass draws each batch with
// vkCmdDraw(count, 1, first_vertex, 0) into one shared vertex array. HLSL-style SV_VertexID
// is zero-based per draw (Slang emits gl_VertexIndex - gl_BaseVertex for it), so the batch
// offset would silently be dropped and every batch would read vertices 0..count-1.
// SV_VulkanVertexID maps to the raw gl_VertexIndex and matches the draw's offset.
//
// The fragment shader has four modes (UiVertex.params.x), picked by the UI pass when it
// expands high-level draw commands into vertices:
//   0 - filled rounded rectangle, uv = local pixel coordinates inside the rect
//   1 - single-channel SDF text, uv = atlas UV
//   2 - textured quad of a custom material, uv = 0..1
//   3 - border ring of a rounded rectangle, uv = local pixel coordinates
//
// Colors are premultiplied by the UI module, and the pass blends with (ONE, ONE_MINUS_SRC_ALPHA),
// so scene alpha is preserved for the editor viewport texture.

struct UiVertex
{
    float2 position; // render-target pixels, origin top-left
    float2 uv;
    float4 color;    // premultiplied RGBA
    float4 shape;    // x radius, y border width, zw half size (modes 0/3)
    float4 params;   // x mode, y px_per_em (text), zw reserved
};

struct UiFrame
{
    float2 viewport;
    float2 _pad;
    // x = onedge_value / 255, y = pixel_dist_scale, z = field mode (0 = SDF, 1 = MSDF).
    float4 params;
};

struct UiPushConstants
{
    UiVertex* vertices;
    UiFrame*  frame;
};

[vk::push_constant] UiPushConstants pc;

// Set 1 (material): the UI material binds the font atlas here. Any custom UI material must
// keep this binding so text and shapes still find the field.
[[vk::binding(1, 1)]]
Sampler2D albedo_map;

struct VSOutput
{
    float4 position : SV_Position;
    float2 uv;
    float4 color;
    float4 shape;
    float4 params;
};

[shader("vertex")]
VSOutput vsMain(uint vertexID : SV_VulkanVertexID)
{
    UiVertex vertex = pc.vertices[vertexID];

    VSOutput output;
    float2 ndc = float2(vertex.position.x / pc.frame->viewport.x * 2.0 - 1.0,
                        vertex.position.y / pc.frame->viewport.y * 2.0 - 1.0);
    output.position = float4(ndc, 0.0, 1.0);
    output.uv = vertex.uv;
    output.color = vertex.color;
    output.shape = vertex.shape;
    output.params = vertex.params;
    return output;
}

float RoundedBoxSdf(float2 point, float2 halfSize, float radius)
{
    float2 q = abs(point) - halfSize + radius;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
}

// Distance to the glyph edge in atlas pixels: positive inside, negative outside. Single
// channel SDF today; the mode switch keeps a multi-channel field a shader change only.
float GlyphDistance(float2 uv)
{
    float onedge = pc.frame->params.x;
    float dist_scale = pc.frame->params.y;
    if (pc.frame->params.z > 0.5)
    {
        float3 field = albedo_map.Sample(uv).rgb;
        float sd = max(min(field.r, field.g), min(max(field.r, field.g), field.b));
        return (sd - onedge) * 255.0 / dist_scale;
    }
    return (albedo_map.Sample(uv).r - onedge) * 255.0 / dist_scale;
}

[shader("fragment")]
float4 fsMain(VSOutput input) : SV_Target
{
    float mode = input.params.x;
    float alpha = 1.0;

    if (mode > 0.5 && mode < 1.5)
    {
        // SDF text: the field is spread in atlas pixels, the quad carries the em scale.
        float distance_px = GlyphDistance(input.uv);
        alpha = saturate(distance_px * input.params.y + 0.5);
    }
    else if (mode > 1.5 && mode < 2.5)
    {
        float4 texel = albedo_map.Sample(input.uv);
        alpha = texel.a;
        input.color.rgb *= texel.rgb;
    }
    else
    {
        // Rounded box: uv holds local pixels, shape.zw the half size.
        float2 halfSize = input.shape.zw;
        float2 local = input.uv - halfSize;
        float distance_px = RoundedBoxSdf(local, halfSize, input.shape.x);
        if (mode > 2.5)
        {
            // Ring: inside the outer edge but outside the inner one.
            float inner = saturate(0.5 - (distance_px + input.shape.y));
            alpha = saturate(0.5 - distance_px) - inner;
        }
        else
        {
            alpha = saturate(0.5 - distance_px);
        }
    }

    return float4(input.color.rgb * alpha, input.color.a * alpha);
}
