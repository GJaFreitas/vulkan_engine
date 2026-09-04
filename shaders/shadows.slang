import common;

struct InstanceData {
    column_major float4x4 model_mat;
};

[[vk::binding(0, 1)]] StructuredBuffer<InstanceData> instances;

// Push constant to know which cascade we are drawing
struct PushData {
    uint cascadeIndex;
};
[[vk::push_constant]]	PushData pushConstants;

struct VSInput {
    float3 Position : POSITION;
};

struct VSOutput {
    float4 Position : SV_POSITION;
};

[[shader("vertex")]]
VSOutput vertMain(VSInput input, uint instanceID: SV_InstanceID)
{
    VSOutput out;
    float4x4 model_mat = instances[instanceID].model_mat;
    
    // Transform vertex from local space to world space
    float4 world_pos = mul(model_mat, float4(input.Position, 1.0));
    
    // Project directly from world space to the specific cascade's light space
    out.Position = mul(ubo.lightSpaceMatrices[pushConstants.cascadeIndex], world_pos);
    
    return out;
}

[[shader("fragment")]]
void fragMain() 
{
}
