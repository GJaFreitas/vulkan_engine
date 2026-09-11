import common;
#include "shared_structs.h"

// --- BINDLESS: Push Constants with BDA ---
[[vk::push_constant]] ShadowRootConstants root;

struct VSOutput {
    float4 Position : SV_POSITION;
};

[[shader("vertex")]]
VSOutput vertMain(uint vertexID : SV_VertexID, uint instanceID: SV_InstanceID)
{
    // --- BINDLESS: Dereference Pointers ---
    Vertex v = root.vertex_addr[vertexID];
    EntityInstanceData inst = root.instance_addr[instanceID];
    UniformBufferObject ubo = root.ubo_addr[0];

    VSOutput out;
    
    // Transform vertex from local space to world space
    float4 world_pos = mul(inst.model_mat, float4(v.pos, 1.0));
    
    // Project directly from world space to the specific cascade's light space
    out.Position = mul(ubo.light_space_matrices[root.cascade_index], world_pos);
    
    return out;
}

[[shader("fragment")]]
void fragMain() 
{
}
