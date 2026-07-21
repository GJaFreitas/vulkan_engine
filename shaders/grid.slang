import common;

struct GridProperties {
    float gridSize;      // spacing between minor lines, e.g. 1.0
    float lineWidth;      // in world units, e.g. 0.02
    float majorLineEvery; // e.g. every 10th line is "major" (thicker/brighter)
    float fadeDistance;    // distance at which grid fully fades out
};

[[vk::push_constant]] GridProperties grid;

struct VSOutput {
    float4 Position : SV_POSITION;
    float3 NearPoint : POSITION0; // world-space point on near plane
    float3 FarPoint : POSITION1;  // world-space point on far plane
};

float3 UnprojectPoint(float x, float y, float z, float4x4 viewInv, float4x4 projInv) {
    float4 unprojectedPoint = mul(viewInv, mul(projInv, float4(x, y, z, 1.0)));
    return unprojectedPoint.xyz / unprojectedPoint.w;
}

[[shader("vertex")]]
VSOutput vertMain(uint vertexID : SV_VertexID)
{
    float2 positions[3] = {
	float2(-1.0, -1.0),
	float2( 3.0, -1.0),
	float2(-1.0,  3.0)
    };
    float2 pos = positions[vertexID];

    VSOutput output;
    output.Position = float4(pos, 0.0, 1.0);
    // Unproject at near (z=0) and far (z=1) clip depth to get a world-space ray
    output.NearPoint = UnprojectPoint(pos.x, pos.y, 0.0, ubo.inv_view, ubo.inv_proj);
    output.FarPoint  = UnprojectPoint(pos.x, pos.y, 1.0, ubo.inv_view, ubo.inv_proj);
    return output;
}

float4 gridColor(float2 coord, float scale, float lineWidthPixels) {
    float2 derivative = fwidth(coord * scale);
    float2 g = abs(frac(coord * scale - 0.5) - 0.5) / derivative;
    float line = min(g.x, g.y);
    float alpha = 1.0 - min(line, 1.0);
    return float4(1.0, 1.0, 1.0, alpha);
}

[[shader("fragment")]]
float4 fragMain(VSOutput input) : SV_TARGET
{
    // Ray-plane intersection with the Y=0 plane

    if (abs(input.FarPoint.y - input.NearPoint.y) < 0.0001)
        discard;

    float t = -input.NearPoint.y / (input.FarPoint.y - input.NearPoint.y);
    float3 worldPos = input.NearPoint + t * (input.FarPoint - input.NearPoint);

    // Discard fragments where the ray doesn't cross the plane in front of the camera
    if (t < 0.0)
	discard;

    // Minor grid lines
    float4 minorGrid = gridColor(worldPos.xz, 1.0 / grid.gridSize, grid.lineWidth);
    // Major grid lines (thicker spacing, brighter)
    float4 majorGrid = gridColor(worldPos.xz, 1.0 / (grid.gridSize * grid.majorLineEvery), grid.lineWidth);

    float4 color = minorGrid;
    color.rgb = lerp(color.rgb, float3(1.0, 1.0, 1.0), majorGrid.a);
    color.a = max(minorGrid.a, majorGrid.a * 0.8);

    // Distance-based fade so the grid doesn't tile harshly to the horizon
    float dist = length(worldPos.xz - ubo.camPos.xz);
    float fade = 1.0 - smoothstep(0.0, grid.fadeDistance, dist);
    color.a *= fade;

    // Correct depth so grid properly occludes/is occluded by scene geometry
    // (requires writing gl_FragDepth / SV_Depth if you need real depth testing against other objects)

    return color;
}
