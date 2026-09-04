module common;

public static const float PI = 3.14159265359;
public static const uint  MAX_POINT_LIGHTS = 16;

public struct PointLight
{
    public float4 position; // xyz = position, w = radius (for attenuation cutoff)
    public float4 color;    // rgb = color, a = intensity
};

public struct UniformBufferObject
{
    public float4x4 view;
    public float4x4 proj;
    public float4x4 inv_view;
    public float4x4 inv_proj;

    public float4x4 lightSpaceMatrices[4];
    public float4 cascadeSplitDepths; // z-depths where the cascades split

    public float4 sun_direction;
    public float4 sun_color;
    public PointLight pointLights[MAX_POINT_LIGHTS];
    public uint pointLightCount;

    public float4 cam_pos;
    public float exposure;
    public float gamma;
};

[[vk::binding(0, 0)]]
public ConstantBuffer<UniformBufferObject> ubo;
