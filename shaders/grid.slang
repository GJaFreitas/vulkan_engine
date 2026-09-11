import common;
#include "shared_structs.h"

// --- BINDLESS: Push Constants with BDA ---
[[vk::push_constant]] GridRootConstants root;

struct VSOutput {
    float4 Position : SV_POSITION;
    float3 NearPoint : POSITION0; 
    float3 FarPoint : POSITION1;  
};

float3 UnprojectPoint(float x, float y, float z, float4x4 viewInv, float4x4 projInv) {
    float4 unprojectedPoint = mul(viewInv, mul(projInv, float4(x, y, z, 1.0)));
    return unprojectedPoint.xyz / unprojectedPoint.w;
}

[[shader("vertex")]]
VSOutput vertMain(uint vertexID : SV_VertexID)
{
    // Fetch UBO from BDA Pointer
    UniformBufferObject ubo = root.ubo_addr[0];

    float2 positions[3] = {
        float2(-1.0, -1.0),
        float2( 3.0, -1.0),
        float2(-1.0,  3.0)
    };
    float2 pos = positions[vertexID];

    VSOutput output;
    output.Position = float4(pos, 0.0, 1.0);
    
    // Note: Assuming your UBO has inv_view and inv_proj fields!
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
    // Fetch UBO from BDA Pointer
    UniformBufferObject ubo = root.ubo_addr[0];
    GridProperties grid = root.properties;

    if (abs(input.FarPoint.y - input.NearPoint.y) < 0.0001)
        discard;

    float t = -input.NearPoint.y / (input.FarPoint.y - input.NearPoint.y);
    float3 worldPos = input.NearPoint + t * (input.FarPoint - input.NearPoint);

    if (t < 0.0)
        discard;

    // Use spacing and line width from the properties
    float4 minorGrid = gridColor(worldPos.xz, 1.0 / grid.major_line_every, 0.02);
    float4 majorGrid = gridColor(worldPos.xz, 1.0 / (grid.major_line_every * 10.0), 0.02);

    float4 color = minorGrid;
    color.rgb = lerp(color.rgb, float3(1.0, 1.0, 1.0), majorGrid.a);
    color.a = max(minorGrid.a, majorGrid.a * 0.8);

    // Fade logic (assuming you add a fadeDistance to properties, or hardcode it)
    float dist = length(worldPos.xz - ubo.cam_pos.xz);
    float fade = 1.0 - smoothstep(0.0, 500.0, dist); // Assuming 500.0 fade distance
    color.a *= fade;

    return color;
}
