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
    output.Position = float4(pos, 1.0, 1.0);
    
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
    UniformBufferObject ubo = root.ubo_addr[0];
    GridProperties grid = root.properties;

    float3 viewRay = normalize(input.FarPoint - input.NearPoint);
    float skyBlend = clamp(viewRay.y * 0.5 + 0.5, 0.0, 1.0);
    float3 topColor = float3(0.1, 0.3, 0.8);
    float3 horizonColor = float3(0.7, 0.8, 0.9);
    
    float4 finalColor = float4(lerp(horizonColor, topColor, skyBlend), 1.0);

    if (abs(input.FarPoint.y - input.NearPoint.y) > 0.0001 && root.show_grid)
    {
        float t = -input.NearPoint.y / (input.FarPoint.y - input.NearPoint.y);
        
        if (t > 0.0) 
        {
            float3 worldPos = input.NearPoint + t * (input.FarPoint - input.NearPoint);
            
            float4 minorGrid = gridColor(worldPos.xz, 1.0 / grid.major_line_every, 0.02);
            float4 majorGrid = gridColor(worldPos.xz, 1.0 / (grid.major_line_every * 10.0), 0.02);

            float4 gridCol = minorGrid;
            gridCol.rgb = lerp(gridCol.rgb, float3(1.0, 1.0, 1.0), majorGrid.a);
            gridCol.a = max(minorGrid.a, majorGrid.a * 0.8);

            // Calculate distance to axes in screen-space pixels
            float pixelDistToZAxis = abs(worldPos.x) / max(fwidth(worldPos.x), 0.0001); // X = 0 line
            float pixelDistToXAxis = abs(worldPos.z) / max(fwidth(worldPos.z), 0.0001); // Z = 0 line
            
            // Thickness of the axis lines in pixels
            float axisThickness = 1.5; 

            // Create anti-aliased masks for the two lines
            float zAxisMask = 1.0 - saturate(pixelDistToZAxis - axisThickness); // Green Z-axis
            float xAxisMask = 1.0 - saturate(pixelDistToXAxis - axisThickness); // Red X-axis

            // Apply Red to the X-axis
            if (xAxisMask > 0.0) {
                gridCol.rgb = lerp(gridCol.rgb, float3(1.0, 0.2, 0.2), xAxisMask);
                gridCol.a = max(gridCol.a, xAxisMask); 
            }
            // Apply Green to the Z-axis
            if (zAxisMask > 0.0) {
                gridCol.rgb = lerp(gridCol.rgb, float3(0.2, 1.0, 0.2), zAxisMask);
                gridCol.a = max(gridCol.a, zAxisMask);
            }
            // -------------------------------

            float dist = length(worldPos.xz - ubo.cam_pos.xz);
            float fade = 1.0 - smoothstep(0.0, 500.0, dist);
            gridCol.a *= fade;

            finalColor.rgb = lerp(finalColor.rgb, gridCol.rgb, gridCol.a);
        }
    }

    return finalColor;
}
