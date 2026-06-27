#version 450

layout(location = 0) in vec3 pos;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 uv;

layout(location = 0) out vec2 fragUV;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    vec4 baseColorFactor;
} pc;

void main()
{
    fragUV = uv;
    gl_Position = pc.mvp * vec4(pos, 1.0);
}
