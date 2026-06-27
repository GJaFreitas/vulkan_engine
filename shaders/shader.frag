#version 450

layout(location = 0) in vec2 fragUV;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    vec4 baseColorFactor;
} pc;

layout(set = 0, binding = 0) uniform sampler2D baseColorTexture;

void main()
{
    vec4 texColor = texture(baseColorTexture, fragUV);

    outColor = texColor * pc.baseColorFactor;
}
