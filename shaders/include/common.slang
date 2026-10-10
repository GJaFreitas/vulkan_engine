static const float PI = 3.14159265359;

// ============================================================================
// GLOBAL BINDLESS HEAP
// ============================================================================
[[vk::binding(0, 0)]] Texture2D		g_textures[];
[[vk::binding(1, 0)]] SamplerState	g_samplers[];

// Helper to safely sample a texture or return a default white fallback
float4 sampleGlobalTexture(uint tex_idx, uint sampler_idx, float2 uv)
{
    // 0xFFFFFFFF is our fallback for "no texture"
    if (tex_idx == 0xFFFFFFFF) {
        return float4(1.0, 1.0, 1.0, 1.0);
    }

    Texture2D tex = g_textures[NonUniformResourceIndex(tex_idx)];
    
    SamplerState samp = g_samplers[NonUniformResourceIndex(sampler_idx)];
    
    return tex.Sample(samp, uv);
}

float4x4 calculateModelMatrix(EntityInstanceData instance)
{
	float	x = instance.rot.x;
	float	y = instance.rot.y;
	float	z = instance.rot.z;
	float	w = instance.rot.w;
	float	s = instance.scale;

	// Precompute repeated terms for the quaternion conversion
	float	x2 = x + x;
	float	y2 = y + y;
	float	z2 = z + z;
	
	float	xx = x * x2;
	float	xy = x * y2;
	float	xz = x * z2;
	float	yy = y * y2;
	float	yz = y * z2;
	float	zz = z * z2;
	float	wx = w * x2;
	float	wy = w * y2;
	float	wz = w * z2;

	// Build the local Entity TRS matrix in a single step.
	// Slang constructs float4x4 in row-major order.
	float4x4	entity_mat = float4x4(
		(1.0 - (yy + zz)) * s,	(xy - wz) * s,			(xz + wy) * s,			instance.pos.x,
		(xy + wz) * s,			(1.0 - (xx + zz)) * s,	(yz - wx) * s,			instance.pos.y,
		(xz - wy) * s,			(yz + wx) * s,			(1.0 - (xx + yy)) * s,	instance.pos.z,
		0.0,					0.0,					0.0,					1.0
	);

	// Multiply the Entity's world matrix by the local glTF node transform.
	// In Slang/HLSL, mul(A, B) does A * B.
	return mul(entity_mat, instance.node_transform);
}

