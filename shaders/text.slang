struct PushConstants
{
    float2 screen_size;
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
    uint   primitive_type : PRIMITIVE_TYPE; // 0=Text, 1=Rect, 2=Ellipse
    float  corner_radius  : CORNER_RADIUS;  // Radius in pixels
};

struct VSOutput
{
    float4 sv_position    : SV_POSITION;
    float2 uv             : TEXCOORD0;
    float2 local_pos      : TEXCOORD1; // Fragment position relative to quad center (pixels)
    float2 half_size      : EXTRA0;    // Half-dimensions of rectangle (pixels)
    float4 color          : COLOR0;
    nointerpolation uint primitive_type : EXTRA1;
    nointerpolation float corner_radius : EXTRA2;
};

[shader("vertex")]
VSOutput vertMain(VSInput input, uint vertex_id : SV_VertexID)
{
    VSOutput output;

    // Unit quad corners: 0 = TL, 1 = BL, 2 = TR, 3 = BR
    float2 corner = float2(
        float(vertex_id & 1u),
        float((vertex_id >> 1u) & 1u)
    );

    float2 pixel_pos = input.pos + corner * input.size;
    float2 ndc = (pixel_pos / pc.screen_size) * 2.0 - 1.0;

    output.sv_position = float4(ndc, 0.0, 1.0);
    output.uv = input.uv_offset + corner * input.uv_size;
    output.color = input.color;
    output.primitive_type = input.primitive_type;
    output.corner_radius = input.corner_radius;

    // Center-origin local space for procedural SDF math
    output.half_size = input.size * 0.5;
    output.local_pos = (corner - 0.5) * input.size;

    return output;
}

float median(float r, float g, float b)
{
    return max(min(r, g), min(max(r, g), b));
}

// SDF for a Rounded Box (in pixel units)
float sdRoundedBox(float2 p, float2 b, float r)
{
    float2 q = abs(p) - b + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

// SDF for an Ellipse (in pixel units)
float sdEllipse(float2 p, float2 r)
{
    float k0 = length(p / r);
    float k1 = length(p / (r * r));
    return k0 * (k0 - 1.0) / k1;
}

[shader("fragment")]
float4 fragMain(VSOutput input) : SV_Target
{
    float opacity = 0.0;

    // --- PRIMITIVE 0: MSDF/MTSDF TEXT ---
    if (input.primitive_type == 0u)
    {
        uint width, height;
        atlas.GetDimensions(width, height);

        float2 unitRange = float2(pc.px_range, pc.px_range) / float2(width, height);
        float2 screenTexSize = 1.0 / fwidth(input.uv);
        float screenPxRange = max(0.5 * dot(unitRange, screenTexSize), 1.0);

        float4 mtsd = atlas.Sample(input.uv);
        float sd = max(median(mtsd.r, mtsd.g, mtsd.b), mtsd.a);

        float screenPxDistance = (sd - 0.5) * screenPxRange;
        opacity = clamp(screenPxDistance + 0.5, 0.0, 1.0);
    }
    // --- PRIMITIVE 1: RECTANGLE / ROUNDED RECTANGLE ---
    else if (input.primitive_type == 1u)
    {
        float dist = sdRoundedBox(input.local_pos, input.half_size, input.corner_radius);
        // Anti-alias edge over 1.0 screen pixel
        opacity = clamp(-dist + 0.5, 0.0, 1.0);
    }
    // --- PRIMITIVE 2: ELLIPSE / CIRCLE ---
    else if (input.primitive_type == 2u)
    {
        float dist = sdEllipse(input.local_pos, input.half_size);
        // Anti-alias edge over 1.0 screen pixel
        opacity = clamp(-dist + 0.5, 0.0, 1.0);
    }

    return float4(input.color.rgb, input.color.a * opacity);
}
