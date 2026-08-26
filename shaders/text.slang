struct PushConstants
{
	float2	screen_size;
	float	px_range;
};
[[vk::push_constant]] PushConstants pc;

[[vk::binding(0, 0)]] Sampler2D atlas;

struct VSInput
{
	float2	pos		: POSITION;
	float2	size		: SIZE;
	float2	uv_offset	: UVOFFSET;
	float2	uv_size		: UVSIZE;
	float4	color		: COLOR;
};

struct VSOutput
{
	float4	sv_position	: SV_POSITION;
	float2	uv		: TEXCOORD0;
	float4	color		: COLOR0;
};

[shader("vertex")]
VSOutput vertMain(VSInput input, uint vertex_id : SV_VertexID)
{
	VSOutput output;

	// Unit quad corners from vertex index:
	// 0 = TL, 1 = BL, 2 = TR, 3 = BR
	float2 corner = float2(
		float(vertex_id & 1u),
		float((vertex_id >> 1u) & 1u)
	);

	float2 pixel_pos = input.pos + corner * input.size;

	float2 ndc = (pixel_pos / pc.screen_size) * 2.0 - 1.0;

	output.sv_position = float4(ndc, 0.0, 1.0);
	output.uv = input.uv_offset + corner * input.uv_size;
	output.color = input.color;

	return output;
}

float2	sqr(float2 x)
{
	return x * x;
}


float screenPxRange(float2 texCoord)
{
    uint width, height;
    atlas.GetDimensions(width, height);
    float2 unitRange = float2(pc.px_range) / float2(width, height);

    float2 screenTexSize = rsqrt(sqr(ddx(texCoord)) + sqr(ddy(texCoord)));
    // Can also be approximated as screenTexSize = float2(1.0) / fwidth(texCoord);

    return max(0.5 * dot(unitRange, screenTexSize), 1.0);
}

float median(float r, float g, float b)
{
    return max(min(r, g), min(max(r, g), b));
}

[shader("fragment")]
float4 fragMain(VSOutput input) : SV_Target
{
    float3 msd = atlas.Sample(input.uv).rgb;
    float sd = median(msd.r, msd.g, msd.b);
    float screenPxDistance = screenPxRange(input.uv) * (sd - 0.5);
    float opacity = clamp(screenPxDistance + 0.5, 0.0, 1.0);

    return float4(input.color.rgb, input.color.a * opacity);
}
