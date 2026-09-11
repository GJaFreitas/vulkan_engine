import common;
#include "shared_structs.h"

[[vk::push_constant]] TextRootConstants root;

struct VSOutput
{
    float4 sv_position    : SV_POSITION;
    float2 uv             : TEXCOORD0;
    float2 local_pos      : TEXCOORD1;
    float2 half_size      : EXTRA0;
    float4 color          : COLOR0;
    float4 inner_color    : COLOR1;
    nointerpolation uint   primitive_type  : EXTRA1;
    nointerpolation float corner_radius   : EXTRA2;
    nointerpolation float stroke_width    : EXTRA3;
    nointerpolation float4 clip_rect      : EXTRA4;
};

[shader("vertex")]
VSOutput vertMain(uint vertex_id : SV_VertexID, uint instance_id : SV_InstanceID)
{
    // --- BINDLESS: Fetch UI Instance data from raw pointer ---
    // Make sure BDA_PTR(UiRenderInstance) instance_addr is in TextRootConstants!
    UiRenderInstance input = root.instance_addr[instance_id];

    VSOutput output;

    float2 corner = float2(
        float(vertex_id & 1u),
        float((vertex_id >> 1u) & 1u)
    );

    float2 pixel_pos = input.pos + corner * input.size;
    float2 ndc = (pixel_pos / root.window_size) * 2.0 - 1.0;

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

float median(float r, float g, float b) {
    return max(min(r, g), min(max(r, g), b));
}

float sdRoundedBox(float2 p, float2 b, float r) {
    float2 q = abs(p) - b + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

float sdEllipse(float2 p, float2 r) {
    float k0 = length(p / r);
    float k1 = length(p / (r * r));
    return k0 * (k0 - 1.0) / k1;
}

[shader("fragment")]
float4 fragMain(VSOutput input) : SV_Target
{
    if (input.sv_position.x < input.clip_rect.x || 
        input.sv_position.y < input.clip_rect.y || 
        input.sv_position.x > input.clip_rect.z || 
        input.sv_position.y > input.clip_rect.w) 
    {
        discard;
    }

    if (input.primitive_type == 0u)
    {
        float width, height;
        g_textures[root.atlas_tex_idx].GetDimensions(width, height);
        // --- BINDLESS: Sample font atlas from global heap ---
        float2 unitRange = float2(root.px_range, root.px_range) / float2(width, height);
        float2 screenTexSize = 1.0 / fwidth(input.uv);
        float screenPxRange = max(0.5 * dot(unitRange, screenTexSize), 1.0);

        float4 mtsd = sampleGlobalTexture(root.atlas_tex_idx, GLOBAL_SAMPLER_UI, input.uv);
        float sd = max(median(mtsd.r, mtsd.g, mtsd.b), mtsd.a);

        float screenPxDistance = (sd - 0.5) * screenPxRange;
        float opacity = clamp(screenPxDistance + 0.5, 0.0, 1.0);

        return float4(input.color.rgb, input.color.a * opacity);
    }

    float max_radius = min(input.half_size.x, input.half_size.y);
    float radius = clamp(input.corner_radius, 0.0, max_radius);

    float dist = 0.0;
    if (input.primitive_type == 1u)
        dist = sdRoundedBox(input.local_pos, input.half_size, radius);
    else if (input.primitive_type == 2u)
        dist = sdEllipse(input.local_pos, input.half_size);

    float4 final_color;

    if (input.stroke_width <= 0.0) {
        float alpha = clamp(-dist + 0.5, 0.0, 1.0);
        final_color = input.inner_color;
        final_color.a *= alpha;
    } else {
        float half_stroke = input.stroke_width * 0.5;
        float dist_outer = dist - half_stroke;
        float dist_inner = dist + half_stroke;

        float outer_alpha = clamp(-dist_outer + 0.5, 0.0, 1.0);
        float inner_alpha = clamp(-dist_inner + 0.5, 0.0, 1.0);

        final_color = lerp(input.color, input.inner_color, inner_alpha);
        final_color.a *= outer_alpha;
    }

    return final_color;
}
