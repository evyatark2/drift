#version 450

layout(location = 0) in vec2 xy;
layout(location = 1) in float oow;

layout(set = 0, binding = 0) uniform MatrixBlock {
    mat4 proj;
} ubo;

void main()
{
    gl_Position = ubo.proj * vec4(xy, oow, 1.0);
}
