#version 450
#extension GL_EXT_scalar_block_layout : require

layout(constant_id = 0) const uint GLIDE_NUM_TMU = 3;

layout(location = 0) in vec4 coord;
layout(location = 1) in vec4 rgba;
layout(location = 2) in vec2 ST0;
layout(location = 3) in vec2 ST1;
layout(location = 4) in vec2 ST2;

layout(location = 0) out vec4 color;
layout(location = 1) out vec2 outST[GLIDE_NUM_TMU];

layout(std430, set = 0, binding = 0) uniform MatrixBlock {
    mat4 proj;
} ubo;

void main()
{
    gl_Position = ubo.proj * vec4(coord.xy, coord.w, 1.0);
    color = rgba / 255.0;
    vec2 ST[] = { ST0, ST1, ST2 };
    for (uint i = 0; i < GLIDE_NUM_TMU; i++)
        outST[i] = ST[i];
}
