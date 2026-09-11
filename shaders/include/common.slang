module common;

public static const float PI = 3.14159265359;

// ============================================================================
// GLOBAL BINDLESS HEAP
// ============================================================================
[[vk::binding(0, 0)]] public Texture2D		g_textures[];
[[vk::binding(1, 0)]] public SamplerState	g_samplers[];

// Helper to safely sample a texture or return a default white fallback
public float4 sampleGlobalTexture(uint tex_idx, float2 uv)
{
    // 0xFFFFFFFF is our fallback for "no texture"
    if (tex_idx == 0xFFFFFFFF) {
        return float4(1.0, 1.0, 1.0, 1.0);
    }

    // Wrap in NonUniformResourceIndex since the index changes per-triangle/pixel
    Texture2D tex = g_textures[NonUniformResourceIndex(tex_idx)];
    SamplerState samp = g_samplers[NonUniformResourceIndex(tex_idx)];
    
    return tex.Sample(samp, uv);
}
