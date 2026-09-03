module common;

public static const float PI = 3.14159265359;

public struct UniformBufferObject
{
    public float4x4 view;
    public float4x4 proj;
    public float4x4 inv_view;
    public float4x4 inv_proj;

    public float4 sun_direction;
    public float4 sun_color;

    public float4 cam_pos;
    public float exposure;
    public float gamma;
};

[[vk::binding(0, 0)]]
public ConstantBuffer<UniformBufferObject> ubo;
