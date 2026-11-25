// Grid material for level primitives (the ProBuilder tool in the editor).
//
// Unlike the editor's infinite Grid.slang, this one is an ordinary opaque material over a
// mesh: it draws a one-metre grid on every face of a box. The grid lives in object space
// multiplied by the object's scale, so a box scaled to 2 m shows two metre cells no matter
// how the entity is transformed, and the lines stay one metre apart in world units.
//
// Vertex fetch, the object table and the opaque push constants come from engine.geometry and
// engine.opaque; only the grid itself is material code.

import engine.geometry;
import engine.opaque;

// The grid is built from the local position and the object scale; no UV or normal is read.
public static const uint MESH_ATTRIBUTES = 0;

// Material parameters, set from the material file (`data/materials/grid.mat`). The material
// editor reflects this block and exposes the same fields.
[[vk::binding(0, 1)]]
cbuffer MaterialUBO
{
    float4 albedo_color;   // base face colour
    float4 line_color;     // grid line colour
    float  cell_size;      // grid cell in metres
    float  line_width;     // line thickness in metres
    float2 _padding;
};

struct GridVSOutput
{
    float3 localPosition : TEXCOORD0;
    float3 worldPosition : TEXCOORD1;
    float3 objectScale   : TEXCOORD2;
    float3 localNormal   : TEXCOORD3;
    float3 normal        : NORMAL;
    float4 position      : SV_Position;
};

[shader("vertex")]
GridVSOutput mainVS(uint vertexID : SV_VertexID, uint drawID : SV_DrawIndex)
{
    ObjectData* object = &pc.objects[drawID];
    VertexContext ctx = VertexContext.create(vertexID, object, pc.fragment->camera);

    float3 local = ctx.getLocalPosition();
    float4 world = ctx.getWorldPosition();

    GridVSOutput output;
    output.localPosition = local;
    output.worldPosition = world.xyz;
    output.objectScale = ctx.getObjectScale();

    // Normal: the same packed unpack and normal-matrix transform the generic vertex stage
    // uses. The fallback face is the +Z side, like the box primitive.
    output.localNormal = ctx.getLocalNormal(float3(0.0, 0.0, 1.0));
    output.normal = ctx.getNormal(float3(0.0, 0.0, 1.0));

    output.position = ctx.getClipPosition(world);
    return output;
}

// Metres of world space covered by one pixel on each of the two grid axes.
float2 GridFootprint(float2 coord)
{
    return float2(abs(ddx(coord.x)) + abs(ddy(coord.x)), abs(ddx(coord.y)) + abs(ddy(coord.y)));
}

// Distance in metres from a coordinate to the nearest grid line, antialiased by the pixel
// footprint. `line_width` is the half-width the material asks for; the derivative floor
// keeps the line at least one pixel wide when the face is far away.
float GridLine(float2 coord, float cell, float halfWidth, float2 footprint)
{
    float2 cellCoord = coord / cell;
    float2 toLine = (0.5 - abs(frac(cellCoord) - 0.5)) * cell;

    float aa = max(max(footprint.x, footprint.y), halfWidth);
    return 1.0 - saturate(min(toLine.x, toLine.y) / max(aa, 1e-5));
}

[shader("fragment")]
float4 fsMain(GridVSOutput input) : SV_Target
{
    float3 normal = normalize(input.normal);
    float3 axis = abs(normalize(input.localNormal));
    float3 scale = input.objectScale;

    // Pick the face plane from the dominant LOCAL normal. The tangent coordinates are the
    // object-space axes of that plane in metres, shifted so 0 sits on the box border: a box
    // scaled to 1 m therefore shows its edge lines, a 3 m box shows three cells across.
    float2 coord;
    float2 faceScale;
    if (axis.x >= axis.y && axis.x >= axis.z)
    {
        coord = float2(input.localPosition.y * scale.y, input.localPosition.z * scale.z);
        faceScale = float2(scale.y, scale.z);
    }
    else if (axis.y >= axis.z)
    {
        coord = float2(input.localPosition.x * scale.x, input.localPosition.z * scale.z);
        faceScale = float2(scale.x, scale.z);
    }
    else
    {
        coord = float2(input.localPosition.x * scale.x, input.localPosition.y * scale.y);
        faceScale = float2(scale.x, scale.y);
    }
    coord += faceScale * 0.5;

    float cell = max(cell_size, 0.0001);
    float2 footprint = GridFootprint(coord);
    float halfWidth = max(line_width, 0.001) * 0.5;
    float line = GridLine(coord, cell, halfWidth, footprint);

    // The face border is a grid line too. Without it a one-metre-high wall has no interior
    // horizontal cell and reads as if the grid only ran one way; the inset border makes the
    // top and bottom of every face visible at any size.
    float2 toBorder = min(coord, faceScale - coord);
    float borderAa = max(max(footprint.x, footprint.y), halfWidth);
    float border = 1.0 - saturate(min(toBorder.x, toBorder.y) / max(borderAa, 1e-5));
    line = max(line, border * 0.8);

    float3 albedo = lerp(albedo_color.rgb, line_color.rgb, line);

    // Simple directional key plus the scene ambient: level blocks read as solid surfaces
    // without pretending to be a PBR material.
    float3 lightDir = normalize(float3(0.35, 0.5, 0.8));
    float diffuse = saturate(dot(normal, lightDir));
    float3 color = albedo * (pc.fragment->ambientColor.rgb + 0.55 * diffuse + 0.18);

    return float4(color, albedo_color.a);
}
