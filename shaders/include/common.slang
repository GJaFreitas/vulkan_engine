module common;

public static const float PI = 3.14159265359;

public struct UniformBufferObject
{
    public float4x4 view;
    public float4x4 proj;
    public float4x4 inv_view;
    public float4x4 inv_proj;

    public float4 lightPositions[4];
    public float4 lightColors[4];
    public float4 camPos;
    public float exposure;
    public float gamma;
    public float prefilteredCubeMipLevels;
    public float scaleIBLAmbient;
};

[[vk::binding(0, 0)]]
public ConstantBuffer<UniformBufferObject> ubo;
