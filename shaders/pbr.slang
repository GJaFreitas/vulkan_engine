import common;


[[vk::binding(1, 0)]] Sampler2DArray shadowMap;

// Push constants - Fast, small data updated frequently per material/object
struct MaterialProperties {
    float4 baseColorFactor;             // Base color tint/multiplier
    float metallicFactor;               // Metallic property multiplier
    float roughnessFactor;              // Surface roughness multiplier
    int baseColorTextureSet;            // Texture binding index for base color (-1 = none)
    int physicalDescriptorTextureSet;   // Texture binding for metallic/roughness
    int normalTextureSet;               // Texture binding for normal maps
    int occlusionTextureSet;            // Texture binding for ambient occlusion
    int emissiveTextureSet;             // Texture binding for emissive maps
    float alphaMask;                    // Alpha masking enable flag
    float alphaMaskCutoff;              // Alpha cutoff threshold
};

struct InstanceData {
     column_major float4x4	model_mat;
};

[[vk::binding(0, 2)]] Sampler2D baseColorMap;
[[vk::binding(1, 2)]] Sampler2D metallicRoughnessMap;
[[vk::binding(2, 2)]] Sampler2D normalMap;
[[vk::binding(3, 2)]] Sampler2D occlusionMap;
[[vk::binding(4, 2)]] Sampler2D emissiveMap;

[[vk::binding(0, 1)]] StructuredBuffer<InstanceData> instances;

[[vk::push_constant]] MaterialProperties material;

// vertex stage

// Input from vertex buffer - Data sent per vertex from CPU
struct VSInput {
    float3 Position : POSITION;     // 3D position in model space
    float3 Normal : NORMAL;         // Surface normal for lighting calculations
    float2 UV : TEXCOORD0;          // Texture coordinates for material sampling
    float4 Tangent : TANGENT;       // Tangent vector for normal mapping (w component = handedness)
};

// Output from vertex shader / Input to fragment shader - Interpolated data
struct VSOutput {
    float4 Position : SV_POSITION; // Required clip space position for rasterization
    float3 WorldPos : POSITION;    // World space position for lighting calculations
    float3 Normal : NORMAL;        // World space normal (interpolated)
    float2 UV : TEXCOORD0;         // Texture coordinates (interpolated)
    float4 Tangent : TANGENT;      // World space tangent (interpolated)
    float ViewDepth : TEXCOORD1;   // Needed to determine which shadow cascade to sample
};

[[shader("vertex")]]
VSOutput	vertMain(VSInput input, uint instanceID: SV_InstanceID)
{
	VSOutput	out;
	float4x4	model_mat = instances[instanceID].model_mat;


	// --- POSITION CALCULATIONS ---
	// local space -> world space
	float4	world_pos = mul(model_mat, float4(input.Position, 1.0));
	// world space -> camera space
	float4	camera_pos = mul(ubo.view, world_pos);
	out.Position = mul(ubo.proj, camera_pos);


	// --- NORMAL CALCULATIONS ---
	// Get the normals from each model
	out.Normal = normalize(mul((float3x3)model_mat, input.Normal));

	// --- PASSTROUGH ---
	out.ViewDepth = abs(camera_pos.z);
	out.UV = input.UV;
	out.WorldPos = world_pos.xyz;

	return out;
}

// 1. Normal Distribution Function (GGX)
// Approximates the amount of microfacets that are aligned to the halfway vector.
// Higher roughness = more scattered microfacets = wider, blurrier highlights.
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

// 2. Geometry Function (Schlick-GGX)
// Approximates self-shadowing of the microfacets.
// Evaluates a single direction (either view direction or light direction).
float geometrySchlickGGX(float NdotV, float roughness)
{
    // For direct lighting, k is calculated this way. 
    // (Note: Image Based Lighting uses a different k calculation).
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;

    float num = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return num / denom;
}

// 2b. Geometry Smith
// Combines the geometry approximation for both the View and Light vectors.
float geometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    
    float ggx1 = geometrySchlickGGX(NdotL, roughness);
    float ggx2 = geometrySchlickGGX(NdotV, roughness);

    return ggx1 * ggx2;
}

// 3. Fresnel Equation (Schlick's Approximation)
// Calculates the ratio of light that gets reflected vs. refracted.
// At grazing angles (edges), surfaces become highly reflective.
float3 fresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float calculateShadow(float3 worldPos, float viewDepth)
{
	// 1. Determine which cascade to use based on fragment's distance from camera
	uint cascadeIndex = 0;
	for(uint i = 0; i < 3; ++i) {
		if(viewDepth > ubo.cascadeSplitDepths[i]) {
			cascadeIndex = i + 1;
		}
	}

	// 2. Transform world position to light space for this specific cascade
	float4 lightSpacePos = mul(ubo.lightSpaceMatrices[cascadeIndex], float4(worldPos, 1.0));
	float3 projCoords = lightSpacePos.xyz / lightSpacePos.w;

	// Map XY from Vulkan clip space [-1, 1] to texture UV space [0, 1]
	projCoords.xy = projCoords.xy * 0.5 + 0.5;

	// If outside the light's frustum, do not shadow
	if (projCoords.z > 1.0 || projCoords.z < 0.0 || 
		projCoords.x > 1.0 || projCoords.x < 0.0 || 
		projCoords.y > 1.0 || projCoords.y < 0.0) {
		return 0.0;
	}

	// 3. Percentage-Closer Filtering (PCF) for soft shadow edges
	uint width, height, elements;
	shadowMap.GetDimensions(width, height, elements);
	float2 texelSize = 1.0 / float2(width, height);

	float currentDepth = projCoords.z;
	float shadow = 0.0;

	// 3x3 PCF Kernel
	for(int x = -1; x <= 1; ++x)
	{
		for(int y = -1; y <= 1; ++y)
		{
			// Sample the specific layer of the Texture2DArray
			float pcfDepth = shadowMap.Sample(float3(projCoords.xy + float2(x, y) * texelSize, cascadeIndex)).r; 
			
			// Note: Because you enabled depthBias and Front-Face culling in the C code, 
			// shadow acne is already physically mitigated. A tiny epsilon prevents floating-point errors.
			shadow += currentDepth - 0.0001 > pcfDepth ? 1.0 : 0.0;
		}
	}
	shadow /= 9.0;

	return shadow;
}

// Calculates the lighting contribution for a single light source
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
    // 1. Material Properties
    float4 baseColor = baseColorMap.Sample(input.UV) * material.baseColorFactor;
    float4 physicalDesc = metallicRoughnessMap.Sample(input.UV);
    
    float roughness = max(physicalDesc.g * material.roughnessFactor, 0.01);
    float metallic = physicalDesc.b * material.metallicFactor;

    // 2. Base Vectors
    float3 N = normalize(input.Normal);
    float3 V = normalize(ubo.cam_pos.xyz - input.WorldPos);

    // 3. F0 (Base reflectivity)
    float3 F0 = float3(0.04, 0.04, 0.04);
    F0 = lerp(F0, baseColor.rgb, metallic);

    // 4. ACCUMULATE LIGHTING
    // Start with 0 light, and add each source
    float3 Lo = float3(0.0, 0.0, 0.0);

    // --- A. DIRECTIONAL SUN ---
    float3 sunL = normalize(-ubo.sun_direction.xyz);
    float3 sunRadiance = ubo.sun_color.rgb * ubo.sun_color.a;
    float shadowFactor = calculateShadow(input.WorldPos, input.ViewDepth);
    
    // Add sun contribution (multiplied by shadow)
    Lo += calculateMicrofacetLighting(N, V, sunL, F0, baseColor.rgb, roughness, metallic, sunRadiance) * (1.0 - shadowFactor);

    // --- B. POINT LIGHTS ---
    for (int i = 0; i < ubo.pointLightCount; ++i) 
    {
        float3 lightPos = ubo.pointLights[i].position.xyz;
        float radius = ubo.pointLights[i].position.w; // w stores the light's radius
        float3 lightColor = ubo.pointLights[i].color.rgb;
        float intensity = ubo.pointLights[i].color.a; // a stores the light's intensity

        float3 L = lightPos - input.WorldPos;
        float distance = length(L);
        L = normalize(L);
        
        // Attenuation: Physical inverse square law with a smooth cutoff at the light's maximum radius
        float attenuation = pow(clamp(1.0 - pow(distance / radius, 4.0), 0.0, 1.0), 2.0) / (distance * distance + 1.0);
        float3 radiance = lightColor * intensity * attenuation;
        
        // Add point light contribution
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
