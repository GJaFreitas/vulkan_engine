import common;

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

[[shader("fragment")]]
float4	fragMain(VSOutput input) : SV_TARGET
{
	// --- TEXTURE OBJECT ---
	// 1. Sample the texture:
	// look up the pixel color and multiply it by the material's tint.
	float4 baseColor = baseColorMap.Sample(input.UV) * material.baseColorFactor;

	// Get roughness and metallicness data
	float4 physicalDesc = metallicRoughnessMap.Sample(input.UV);
	float  roughness = physicalDesc.g * material.roughnessFactor;
	// Clamp roughness to prevent divide by 0
	roughness = max(roughness, 0.01);
	float  metallic = physicalDesc.b * material.metallicFactor;

	// --- 2. VECTOR SETUP ---
	float3 N = normalize(input.Normal);
	float3 V = normalize(ubo.cam_pos.xyz - input.WorldPos);
	float3 L = normalize(-ubo.sun_direction.xyz);
	float3 H = normalize(V + L); // Halfway vector

	// --- 3. BASE REFLECTIVITY (F0) ---
	float3 F0 = float3(0.04, 0.04, 0.04);
	F0 = lerp(F0, baseColor.rgb, metallic);

	// --- 4. COOK-TORRANCE BRDF ---
	float NdotL = max(dot(N, L), 0.0);
	float NdotV = max(dot(N, V), 0.0);

	// Calculate the three BRDF components
	float  NDF = distributionGGX(N, H, roughness);   
	float  G   = geometrySmith(N, V, L, roughness);    
	float3 F   = fresnelSchlick(max(dot(H, V), 0.0), F0);

	// Combine for specular output
	float3 numerator = NDF * G * F;
	float denominator = 4.0 * NdotV * NdotL + 0.0001; 
	float3 specular = numerator / denominator;

	// --- 5. ENERGY CONSERVATION ---
	float3 kS = F; // Specular contribution
	float3 kD = float3(1.0, 1.0, 1.0) - kS; // Diffuse contribution
	kD *= 1.0 - metallic; // Metals have no diffuse color

	// --- 6. FINAL LIGHTING ---
	// (Diffuse + Specular) * LightColor * Angle
	float3 Lo = (kD * baseColor.rgb / PI + specular) * ubo.sun_color.rgb * NdotL;

	// --- 7. AMBIENT & POST-PROCESSING ---
	float ao = 1.0;
	float3 ambient = float3(0.03, 0.03, 0.03) * baseColor.rgb * ao;
	float3 color = ambient + Lo;

	// Tonemapping (Exposure)
	color = float3(1.0, 1.0, 1.0) - exp(-color * ubo.exposure);
	// Gamma correction
	// Not needed since im using sRGB
	// color = pow(color, float3(1.0 / ubo.gamma, 1.0 / ubo.gamma, 1.0 / ubo.gamma));

	return float4(color, baseColor.a);
}

// Prev lignting
//
	// // 2. Basic Diffuse Lighting (Lambertian):
	// // We need to know the angle between the light and the surface.
	// float3 N = normalize(input.Normal);
	// // Invert the sun direction so it points from the surface TO the sun
	// float3 L = normalize(-ubo.sun_direction.xyz); 
	// // The dot product returns 1.0 if they face exactly the same way, 
	// // 0.0 if perpendicular, and negative if facing away.
	// // We use max() to prevent negative light.
	// float light_intensity = max(dot(N, L), 0.0);
	// // Add a tiny bit of ambient light (0.1) so shadows aren't pitch black
	// float ambient = 0.1;
	// float final_light = light_intensity + ambient;
