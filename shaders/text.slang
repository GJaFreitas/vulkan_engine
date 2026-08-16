struct PushConstants
{
	float2	screen_size;
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
}

struct VSOutput
{
	float4	sv_position	: SV_POSITION;
	float2	uv		: TEXCOORD0;
	float4	color		: COLOR0;
}

[shader("vertex")]
VSOutput vertMain(VSInput input, uint vertex_id: SV_VertexID)
{
	VSOutput	output;

	// unit quad corners from vertex index, triangle strip: 0=TL,1=BL,2=TR,3=BR
	float2	corner = float2(float(vertex_id & 1u), float((vertex_id >> 1u) & 1u));

	float2	pixel_pos = input.pos + corner * input.size;

	float2	ndc = (pixel_pos / pc.screen_size) * 2.0 - 1.0;

	output.sv_position = float4(ndc, 0.0, 1.0);
	output.uv = input.uv_offset + corner * input.uv_size;
	output.color = input.color;

	return output;
}

[shader("fragment")]
float4 fragMain(VSOutput input) : SV_Target
{
	float distance = atlas.Sample(input.uv).r;
	float smoothing = fwidth(distance) * 0.75;
	float alpha = smoothstep(0.5 - smoothing, 0.5 + smoothing, distance);
	return float4(input.color.rgb, input.color.a * alpha);
}
