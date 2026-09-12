import common;

#include "shared_structs.h"

// --- BINDLESS: SHADOW MAP ---
// Render targets (like shadow maps) are often kept in a dedicated descriptor set (Set 1)
// rather than the global bindless heap, because they change every frame and require 
// a Texture2DArray type instead of a standard Texture2D.
[[vk::binding(0, 1)]] Sampler2DArray shadowMap;

[[vk::push_constant]] PBRRootConstants root;

struct VSOutput {
    float4 Position : SV_POSITION; 
    float3 WorldPos : POSITION;    
    float3 Normal : NORMAL;        
    float2 UV : TEXCOORD0;         
    float4 Tangent : TANGENT;      
    float ViewDepth : TEXCOORD1;   
};

[[shader("vertex")]]
VSOutput vertMain(uint vertexID : SV_VertexID, uint instanceID: SV_InstanceID)
{
    VSOutput out;

    Vertex v = root.vertex_addr[vertexID];
    EntityInstanceData inst = root.instance_addr[instanceID];

    // --- POSITION CALCULATIONS ---
    float4 world_pos = mul(inst.model_mat, float4(v.pos, 1.0));
    float4 camera_pos = mul(root.ubo_addr[0].view, world_pos);
    out.Position = mul(root.ubo_addr[0].proj, camera_pos);

    // --- NORMAL CALCULATIONS ---
    out.Normal = normalize(mul((float3x3)inst.model_mat, v.normal));

    // --- PASSTHROUGH ---
    out.ViewDepth = abs(camera_pos.z);
    out.UV = v.uv;
    out.WorldPos = world_pos.xyz;

    return out;
}

float distributionGGX(float3 N, float3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return num / denom;
}

float geometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;

    float num = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return num / denom;
}

float geometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    
    float ggx1 = geometrySchlickGGX(NdotL, roughness);
    float ggx2 = geometrySchlickGGX(NdotV, roughness);

    return ggx1 * ggx2;
}

float3 fresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float calculateShadow(float3 worldPos, float viewDepth, uint2 shadowMapDimensions, UniformBufferObject ubo)
{
    // Vectorized, branchless cascade selection
    float4 depths = float4(ubo.cascade_split_depths[0], ubo.cascade_split_depths[1], ubo.cascade_split_depths[2], 999999.0);
    float4 isGreater = step(depths, float4(viewDepth, viewDepth, viewDepth, viewDepth));
    uint cascadeIndex = (uint)dot(isGreater, float4(1.0, 1.0, 1.0, 0.0));

    float4 lightSpacePos = mul(ubo.light_space_matrices[cascadeIndex], float4(worldPos, 1.0));
    float3 projCoords = lightSpacePos.xyz / lightSpacePos.w;

    projCoords.xy = projCoords.xy * 0.5 + 0.5;

    if (projCoords.z > 1.0 || projCoords.z < 0.0 || 
        projCoords.x > 1.0 || projCoords.x < 0.0 || 
        projCoords.y > 1.0 || projCoords.y < 0.0) {
        return 0.0;
    }

    float2 texelSize = 1.0 / float2(shadowMapDimensions.x, shadowMapDimensions.y);

    float currentDepth = projCoords.z;
    float shadow = 0.0;

    for(int x = -1; x <= 1; ++x)
    {
        for(int y = -1; y <= 1; ++y)
        {
            float pcfDepth = shadowMap.Sample(float3(projCoords.xy + float2(x, y) * texelSize, cascadeIndex)).r; 
            shadow += currentDepth - 0.0001 > pcfDepth ? 1.0 : 0.0;
        }
    }
    shadow /= 9.0;

    return shadow;
}

float3 calculateMicrofacetLighting(float3 N, float3 V, float3 L, float3 F0, float3 albedo, float roughness, float metallic, float3 radiance)
{
    float3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);

    float NDF = distributionGGX(N, H, roughness);   
    float G   = geometrySmith(N, V, L, roughness);      
    float3 F  = fresnelSchlick(max(dot(H, V), 0.0), F0);

    float3 numerator = NDF * G * F;
    float denominator = 4.0 * NdotV * NdotL + 0.0001; 
    float3 specular = numerator / denominator;

    float3 kS = F; 
    float3 kD = float3(1.0, 1.0, 1.0) - kS; 
    kD *= 1.0 - metallic;

    return (kD * albedo / PI + specular) * radiance * NdotL;
}

[[shader("fragment")]]
float4 fragMain(VSOutput input) : SV_TARGET
{
    UniformBufferObject ubo = root.ubo_addr[0];

    // --- BINDLESS: GLOBAL TEXTURE HEAP LOOKUPS ---
    float4 baseColor    = sampleGlobalTexture(root.base_color_tex, GLOBAL_SAMPLER_PBR, input.UV) * root.base_color_factor;
    float4 physicalDesc = sampleGlobalTexture(root.metallic_roughness_tex, GLOBAL_SAMPLER_PBR, input.UV);
    
    float roughness = max(physicalDesc.g * root.roughness_factor, 0.01);
    float metallic = physicalDesc.b * root.metallic_factor;

    // 2. Base Vectors
    float3 N = normalize(input.Normal);
    float3 V = normalize(ubo.cam_pos.xyz - input.WorldPos); // Updated to camera_pos from UBO struct

    // 3. F0 (Base reflectivity)
    float3 F0 = float3(0.04, 0.04, 0.04);
    F0 = lerp(F0, baseColor.rgb, metallic);

    // 4. ACCUMULATE LIGHTING
    float3 Lo = float3(0.0, 0.0, 0.0);

    // --- A. DIRECTIONAL SUN ---
    float3 sunL = normalize(-ubo.sun_direction.xyz);
    float3 sunRadiance = ubo.sun_color.rgb * ubo.sun_color.a;
    
    uint width, height, elements;
    shadowMap.GetDimensions(width, height, elements);
    uint2 shadowMapDim = uint2(width, height);
    float shadowFactor = calculateShadow(input.WorldPos, input.ViewDepth, shadowMapDim, ubo);
    
    Lo += calculateMicrofacetLighting(N, V, sunL, F0, baseColor.rgb, roughness, metallic, sunRadiance) * (1.0 - shadowFactor);

    // --- B. POINT LIGHTS ---
    for (int i = 0; i < ubo.point_light_count; ++i) 
    {
        float3 lightPos = ubo.point_lights[i].position.xyz;
        float radius = ubo.point_lights[i].position.w; 
        float3 lightColor = ubo.point_lights[i].color.rgb;
        float intensity = ubo.point_lights[i].color.a; 

        float3 L = lightPos - input.WorldPos;
        float distance = length(L);
        L = normalize(L);
        
        float attenuation = pow(clamp(1.0 - pow(distance / radius, 4.0), 0.0, 1.0), 2.0) / (distance * distance + 1.0);
        float3 radiance = lightColor * intensity * attenuation;
        
        Lo += calculateMicrofacetLighting(N, V, L, F0, baseColor.rgb, roughness, metallic, radiance);
    }

    // 5. Ambient & Post
    float ao = 1.0;
    float3 ambient = float3(0.03, 0.03, 0.03) * baseColor.rgb * ao;
    float3 color = ambient + Lo;

    // Tonemapping
    color = float3(1.0, 1.0, 1.0) - exp(-color * ubo.exposure);

    return float4(color, baseColor.a);
}
