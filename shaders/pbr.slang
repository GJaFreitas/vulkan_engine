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
     float4x4	model_mat;
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

	return out;
}

[[shader("fragment")]]
float4	fragMain(VSOutput input) : SV_TARGET
{
	// --- TEXTURE OBJECT ---
	// 1. Sample the texture:
	// look up the pixel color and multiply it by the material's tint.
	float4 baseColor = baseColorMap.Sample(input.UV) * material.baseColorFactor;

	// 2. Basic Diffuse Lighting (Lambertian):
	// We need to know the angle between the light and the surface.
	float3 N = normalize(input.Normal);
	// Invert the sun direction so it points from the surface TO the sun
	float3 L = normalize(-ubo.sun_direction.xyz); 
	// The dot product returns 1.0 if they face exactly the same way, 
	// 0.0 if perpendicular, and negative if facing away.
	// We use max() to prevent negative light.
	float light_intensity = max(dot(N, L), 0.0);
	// Add a tiny bit of ambient light (0.1) so shadows aren't pitch black
	float ambient = 0.1;
	float final_light = light_intensity + ambient;

	// 3. Apply the light to the color:
	float3 final_color = baseColor.rgb * final_light;

	return float4(final_color, baseColor.a);
}
