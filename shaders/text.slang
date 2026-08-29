struct PushConstants
{
    float2 screen_size;
    float2 atlas_size;
    float  px_range;
};
[[vk::push_constant]] PushConstants pc;

[[vk::binding(0, 0)]] Sampler2D atlas;

struct VSInput
{
    float2 pos            : POSITION;
    float2 size           : SIZE;
    float2 uv_offset       : UVOFFSET;
    float2 uv_size         : UVSIZE;
    float4 color          : COLOR;
    float4 inner_color    : COLOR1;
    uint   primitive_type : PRIMITIVE_TYPE;
    float  corner_radius  : CORNER_RADIUS;
    float  stroke_width   : STROKE_WIDTH;
};

struct VSOutput
{
    float4 sv_position    : SV_POSITION;
    float2 uv             : TEXCOORD0;
    float2 local_pos      : TEXCOORD1;
    float2 half_size      : EXTRA0;
    float4 color          : COLOR0;
    float4 inner_color    : COLOR1;
    nointerpolation uint  primitive_type : EXTRA1;
    nointerpolation float corner_radius  : EXTRA2;
    nointerpolation float stroke_width   : EXTRA3;
};

[shader("vertex")]
VSOutput vertMain(VSInput input, uint vertex_id : SV_VertexID)
{
    VSOutput output;

    float2 corner = float2(
        float(vertex_id & 1u),
        float((vertex_id >> 1u) & 1u)
    );

    float2 pixel_pos = input.pos + corner * input.size;
    float2 ndc = (pixel_pos / pc.screen_size) * 2.0 - 1.0;

    output.sv_position    = float4(ndc, 0.0, 1.0);
    output.uv             = input.uv_offset + corner * input.uv_size;
    output.color          = input.color;
    output.inner_color    = input.inner_color;
    output.primitive_type = input.primitive_type;
    output.corner_radius  = input.corner_radius;
    output.stroke_width   = input.stroke_width;

    output.half_size = input.size * 0.5;
    output.local_pos = (corner - 0.5) * input.size;

    return output;
}

float median(float r, float g, float b)
{
    return max(min(r, g), min(max(r, g), b));
}

float sdRoundedBox(float2 p, float2 b, float r)
{
    float2 q = abs(p) - b + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

float sdEllipse(float2 p, float2 r)
{
    float k0 = length(p / r);
    float k1 = length(p / (r * r));
    return k0 * (k0 - 1.0) / k1;
}

[shader("fragment")]
float4 fragMain(VSOutput input) : SV_Target
{
    // --- PRIMITIVE 0: MSDF / MTSDF TEXT ---
    if (input.primitive_type == 0u)
    {
        float width, height;
        atlas.GetDimensions(width, height);

        float2 unitRange = float2(pc.px_range, pc.px_range) / float2(width, height);
        float2 screenTexSize = 1.0 / fwidth(input.uv);
        float screenPxRange = max(0.5 * dot(unitRange, screenTexSize), 1.0);

        float4 mtsd = atlas.Sample(input.uv);
        float sd = max(median(mtsd.r, mtsd.g, mtsd.b), mtsd.a);

        float screenPxDistance = (sd - 0.5) * screenPxRange;
        float opacity = clamp(screenPxDistance + 0.5, 0.0, 1.0);

        return float4(input.color.rgb, input.color.a * opacity);
    }

    // --- PRIMITIVES 1 & 2: RECTANGLES & ELLIPSES ---
    
    // Clamp corner radius to half of the smallest side so math doesn't invert
    float max_radius = min(input.half_size.x, input.half_size.y);
    float radius = clamp(input.corner_radius, 0.0, max_radius);

    float dist = 0.0;
    if (input.primitive_type == 1u)
        dist = sdRoundedBox(input.local_pos, input.half_size, radius);
    else if (input.primitive_type == 2u)
        dist = sdEllipse(input.local_pos, input.half_size);

    float4 final_color;

    // --- SOLID FILL (stroke_width == 0) ---
    if (input.stroke_width <= 0.0)
    {
        // Smooth anti-aliased edge transition across 1 pixel
        float alpha = clamp(-dist + 0.5, 0.0, 1.0);
        final_color = input.inner_color;
        final_color.a *= alpha;
    }
    // --- STROKED / BORDERED SHAPE ---
    else
    {
        float half_stroke = input.stroke_width * 0.5;

        // Outer stroke distance boundary (expand outwards by half_stroke)
        float dist_outer = dist - half_stroke;
        // Inner fill distance boundary (contract inwards by half_stroke)
        float dist_inner = dist + half_stroke;

        // Anti-aliased edge masks (1.0 screen-pixel AA range)
        float outer_alpha = clamp(-dist_outer + 0.5, 0.0, 1.0);
        float inner_alpha = clamp(-dist_inner + 0.5, 0.0, 1.0);

        // Blend inner_color into outer border color smoothly
        final_color = lerp(input.color, input.inner_color, inner_alpha);
        final_color.a *= outer_alpha;
    }

    return final_color;
}
