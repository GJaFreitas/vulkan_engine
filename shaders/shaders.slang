// shared structs

struct UniformBufferObject
{
    float4x4 view;
    float4x4 proj;
    float4 lightPositions[4];
    float4 lightColors[4];
    float4 camPos;
    float exposure;
    float gamma;
    float prefilteredCubeMipLevels;
    float scaleIBLAmbient;
};

// Add this after your UniformBufferObject struct
struct PushConstants
{
    float4x4 model;
};

[[vk::push_constant]]
ConstantBuffer<PushConstants> pc;

[[vk::binding(0, 0)]]
ConstantBuffer<UniformBufferObject> ubo;

// Material bindings
[[vk::binding(0, 1)]]
Sampler2D baseColorMap;

[[vk::binding(1, 1)]]
Sampler2D metalRoughMap;

[[vk::binding(2, 1)]]
Sampler2D normalMap;

[[vk::binding(3, 1)]]
Sampler2D occlusionMap;

[[vk::binding(4, 1)]]
Sampler2D emissiveMap;

// vertex stage

struct VertexInput
{
    float3 pos       : POSITION;
    float3 normal    : NORMAL;
    float2 uv        : TEXCOORD0;
    float2 tex_coord : TEXCOORD1;
};

struct VSOutput
{
    float4 clipPos  : SV_Position;
    float3 worldPos : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0;
};

[shader("vertex")]
VSOutput vertMain(VertexInput input)
{
    VSOutput output;
    float4 worldPos = mul(pc.model, float4(input.pos, 1.0));
    output.worldPos = worldPos.xyz;
    output.clipPos = mul(ubo.proj, mul(ubo.view, worldPos));
    output.normal = mul((float3x3)pc.model, input.normal);
    output.uv = input.uv;
    return output;
}

// fragment stage

[shader("fragment")]
float4 fragMain(VSOutput input) : SV_Target
{
    float4 baseColor = baseColorMap.Sample(input.uv);

    float3 N = normalize(input.normal);
    float3 L = normalize(ubo.lightPositions[0].xyz - input.worldPos);
    float NdotL = max(dot(N, L), 0.0);

    float3 ambient = 0.1 * baseColor.rgb;
    float3 diffuse = NdotL * baseColor.rgb * ubo.lightColors[0].rgb;

    float3 color = ambient + diffuse;
    color = color / (color + float3(1.0, 1.0, 1.0)); // simple tonemap
    color = pow(color, float3(1.0 / ubo.gamma, 1.0 / ubo.gamma, 1.0 / ubo.gamma));

    return baseColor;
}
