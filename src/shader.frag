#version 450
#extension GL_EXT_scalar_block_layout : require

#define GR_TEXTURECOMBINE_ZERO 0x0           /* texout = 0 */
#define GR_TEXTURECOMBINE_DECAL 0x1          /* texout = texthis */
#define GR_TEXTURECOMBINE_OTHER 0x2          /* this TMU in passthru mode */
#define GR_TEXTURECOMBINE_ADD 0x3            /* tout = tthis + t(this+1) */
#define GR_TEXTURECOMBINE_MULTIPLY 0x4       /* texout = tthis * t(this+1) */
#define GR_TEXTURECOMBINE_SUBTRACT 0x5       /* Sutract from upstream TMU */
#define GR_TEXTURECOMBINE_DETAIL 0x6         /* detail--detail on tthis */
#define GR_TEXTURECOMBINE_DETAIL_OTHER 0x7   /* detail--detail on tthis+1 */
#define GR_TEXTURECOMBINE_TRILINEAR_ODD 0x8  /* trilinear--odd levels tthis*/
#define GR_TEXTURECOMBINE_TRILINEAR_EVEN 0x9 /*trilinear--even levels tthis*/
#define GR_TEXTURECOMBINE_ONE 0xa            /* texout = 0xFFFFFFFF */

#define GR_COMBINE_FUNCTION_ZERO 0x0
#define GR_COMBINE_FUNCTION_LOCAL 0x1
#define GR_COMBINE_FUNCTION_LOCAL_ALPHA 0x2
#define GR_COMBINE_FUNCTION_SCALE_OTHER 0x3
#define GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL 0x4
#define GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL_ALPHA 0x5
#define GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL 0x6
#define GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL 0x7
#define GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL_ALPHA 0x8
#define GR_COMBINE_FUNCTION_SCALE_MINUS_LOCAL_ADD_LOCAL 0x9
#define GR_COMBINE_FUNCTION_SCALE_MINUS_LOCAL_ADD_LOCAL_ALPHA 0x10

#define GR_COMBINE_FACTOR_ZERO 0x0
#define GR_COMBINE_FACTOR_LOCAL 0x1
#define GR_COMBINE_FACTOR_OTHER_ALPHA 0x2
#define GR_COMBINE_FACTOR_LOCAL_ALPHA 0x3
#define GR_COMBINE_FACTOR_TEXTURE_ALPHA 0x4
#define GR_COMBINE_FACTOR_TEXTURE_RGB 0x5 /* Not documented */
#define GR_COMBINE_FACTOR_ONE 0x8
#define GR_COMBINE_FACTOR_ONE_MINUS_LOCAL 0x9
#define GR_COMBINE_FACTOR_ONE_MINUS_OTHER_ALPHA 0xa
#define GR_COMBINE_FACTOR_ONE_MINUS_LOCAL_ALPHA 0xb
#define GR_COMBINE_FACTOR_ONE_MINUS_TEXTURE_ALPHA 0xc
#define GR_COMBINE_FACTOR_ONE_MINUS_LOD_FRACTION 0xd

#define GR_COMBINE_LOCAL_ITERATED 0x0
#define GR_COMBINE_LOCAL_CONSTANT 0x1
#define GR_COMBINE_LOCAL_DEPTH 0x2

#define GR_COMBINE_OTHER_ITERATED 0x0
#define GR_COMBINE_OTHER_TEXTURE 0x1
#define GR_COMBINE_OTHER_CONSTANT 0x2

#define GR_CMP_NEVER 0x0
#define GR_CMP_LESS 0x1
#define GR_CMP_EQUAL 0x2
#define GR_CMP_LEQUAL 0x3
#define GR_CMP_GREATER 0x4
#define GR_CMP_NOTEQUAL 0x5
#define GR_CMP_GEQUAL 0x6
#define GR_CMP_ALWAYS 0x7

#define GR_FOG_DISABLE 0x0
#define GR_FOG_WITH_ITERATED_ALPHA 0x1
#define GR_FOG_WITH_TABLE 0x2
#define GR_FOG_WITH_ITERATED_Z 0x3 /* Bug 735 */
#define GR_FOG_MULT2 0x100
#define GR_FOG_ADD2 0x200

#define GR_FOG_TABLE_SIZE 64

layout(constant_id = 0) const uint GLIDE_NUM_TMU = 3;

layout(location = 0) in vec4 inColor;
layout(location = 1) in vec3 ST[GLIDE_NUM_TMU];

layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform sampler2D images[GLIDE_NUM_TMU];
//layout(set = 1, binding = 0) uniform sampler2D tmu0[];
//layout(set = 1, binding = 1) uniform sampler2D tmu1[];
//layout(set = 1, binding = 2) uniform sampler2D tmu2[];
//layout(std430, set = 3, binding = 0) uniform ConfigBlock {
//    vec4 const_color;
//    vec4 chromakey;
//    vec4 fog_color;
//    uint tmu[GLIDE_NUM_TMU];
//    uint ccombine_func;
//    uint ccombine_f;
//    uint ccombine_local;
//    uint ccombine_other;
//    uint acombine_func;
//    uint acombine_f;
//    uint acombine_local;
//    uint acombine_other;
//    uint atest_func;
//    float atest_value;
//    uint chromakey_enable;
//    uint fog_mode;
//} config[];


layout(std430, set = 2, binding = 0) uniform FogTable {
    uint table[GR_FOG_TABLE_SIZE];
} fog;

layout(push_constant) uniform PCBlock {
    vec4 const_color;
    vec4 chromakey;
    vec4 fog_color;
    uint tmu[GLIDE_NUM_TMU];
    uint ccombine_func;
    uint ccombine_f;
    uint ccombine_local;
    uint ccombine_other;
    uint acombine_func;
    uint acombine_f;
    uint acombine_local;
    uint acombine_other;
    uint atest_func;
    float atest_value;
    uint chromakey_enable;
    uint fog_mode;
} PC;

void main()
{
    outColor = vec4(0, 0, 0, 1);
    //gl_FragDepth = -1.0 * gl_FragCoord.z + 1.0;
    if (PC.ccombine_other == GR_COMBINE_OTHER_TEXTURE || PC.acombine_other == GR_COMBINE_OTHER_TEXTURE) {
        for (uint _i = 0; _i < GLIDE_NUM_TMU; _i++) {
            uint i = GLIDE_NUM_TMU - (_i + 1);

            vec2 newST;
            ivec2 _size = textureSize(images[i], 0);
            vec2 size = vec2(float(_size.x), float(_size.y));
            if (size.x < size.y) {
                float aspect = size.y / size.x;
                size.y = 256.0;
                size.x = 256.0 / aspect;
            } else {
                float aspect = size.x / size.y;
                size.x = 256.0;
                size.y = 256.0 / aspect;
            }

            newST = ST[i].yz / size / ST[i].x;

            switch (PC.tmu[i]) {
            case GR_TEXTURECOMBINE_ZERO:
                outColor = vec4(vec3(0.0), 1.0);
                break;
            case GR_TEXTURECOMBINE_DECAL:
                outColor = texture(images[i], newST);
                break;
            case GR_TEXTURECOMBINE_OTHER:
                /* empty */
                break;
            case GR_TEXTURECOMBINE_ADD:
                outColor = outColor + texture(images[i], newST);
                break;
            case GR_TEXTURECOMBINE_MULTIPLY:
                outColor = outColor * texture(images[i], newST);
                break;
            case GR_TEXTURECOMBINE_SUBTRACT:
                outColor = outColor - texture(images[i], newST);
                break;
            case GR_TEXTURECOMBINE_DETAIL:
                break;
            case GR_TEXTURECOMBINE_DETAIL_OTHER:
                break;
            case GR_TEXTURECOMBINE_TRILINEAR_ODD:
                break;
            case GR_TEXTURECOMBINE_TRILINEAR_EVEN:
                break;
            case GR_TEXTURECOMBINE_ONE:
                outColor = vec4(1.0);
                break;
            }
        }
    }

    vec3 localColor;
    switch (PC.ccombine_local) {
    case GR_COMBINE_LOCAL_ITERATED:
        localColor = inColor.rgb;
        break;
    case GR_COMBINE_LOCAL_CONSTANT:
        localColor = PC.const_color.rgb;
        break;
    case GR_COMBINE_LOCAL_DEPTH:
        break;
    }

    float localAlpha;
    switch (PC.acombine_local) {
    case GR_COMBINE_LOCAL_ITERATED:
        localAlpha = inColor.a;
        break;
    case GR_COMBINE_LOCAL_CONSTANT:
        localAlpha = PC.const_color.a;
        break;
    case GR_COMBINE_LOCAL_DEPTH:
        break;
    }

    vec3 otherColor;
    switch (PC.ccombine_other) {
    case GR_COMBINE_OTHER_ITERATED:
        otherColor = inColor.rgb;
        break;
    case GR_COMBINE_OTHER_TEXTURE:
        otherColor = outColor.rgb;
        break;
    case GR_COMBINE_OTHER_CONSTANT:
        otherColor = PC.const_color.rgb;
        break;
    }

    float otherAlpha;
    switch (PC.acombine_other) {
    case GR_COMBINE_OTHER_ITERATED:
        otherAlpha = inColor.a;
        break;
    case GR_COMBINE_OTHER_TEXTURE:
        otherAlpha = outColor.a;
        break;
    case GR_COMBINE_OTHER_CONSTANT:
        otherAlpha = PC.const_color.a;
        break;
    }

    vec3 factorColor;
    switch (PC.ccombine_f) {
    case GR_COMBINE_FACTOR_ZERO:
        factorColor = vec3(0);
        break;
    case GR_COMBINE_FACTOR_LOCAL:
        factorColor = localColor;
        break;
    case GR_COMBINE_FACTOR_OTHER_ALPHA:
        break;
    case GR_COMBINE_FACTOR_LOCAL_ALPHA:
        factorColor = vec3(localAlpha);
        break;
    case GR_COMBINE_FACTOR_TEXTURE_ALPHA:
        factorColor = vec3(outColor.a);
        break;
    //case GR_COMBINE_FACTOR_TEXTURE_RGB:
    //    break;
    case GR_COMBINE_FACTOR_ONE:
        factorColor = vec3(1);
        break;
    case GR_COMBINE_FACTOR_ONE_MINUS_LOCAL:
        factorColor = vec3(1) - localColor;
        break;
    case GR_COMBINE_FACTOR_ONE_MINUS_OTHER_ALPHA:
        break;
    case GR_COMBINE_FACTOR_ONE_MINUS_LOCAL_ALPHA:
        factorColor = vec3(1 - localAlpha);
        break;
    case GR_COMBINE_FACTOR_ONE_MINUS_TEXTURE_ALPHA:
        break;
    case GR_COMBINE_FACTOR_ONE_MINUS_LOD_FRACTION:
        break;
    }

    float factorAlpha;
    switch (PC.acombine_f) {
    case GR_COMBINE_FACTOR_ZERO:
        factorColor = vec3(0);
        break;
    case GR_COMBINE_FACTOR_LOCAL:
    case GR_COMBINE_FACTOR_LOCAL_ALPHA:
        factorAlpha = localAlpha;
        break;
    case GR_COMBINE_FACTOR_OTHER_ALPHA:
        factorAlpha = otherAlpha;
        break;
    case GR_COMBINE_FACTOR_TEXTURE_ALPHA:
        factorAlpha = outColor.a;
        break;
    //case GR_COMBINE_FACTOR_TEXTURE_RGB:
    //    break;
    case GR_COMBINE_FACTOR_ONE:
        factorAlpha = 1;
        break;
    case GR_COMBINE_FACTOR_ONE_MINUS_LOCAL:
    case GR_COMBINE_FACTOR_ONE_MINUS_LOCAL_ALPHA:
        factorAlpha = 1 - localAlpha;
        break;
    case GR_COMBINE_FACTOR_ONE_MINUS_OTHER_ALPHA:
        factorAlpha = 1 - otherAlpha;
        break;
    case GR_COMBINE_FACTOR_ONE_MINUS_TEXTURE_ALPHA:
        factorAlpha = 1 - outColor.a;
        break;
    case GR_COMBINE_FACTOR_ONE_MINUS_LOD_FRACTION:
        break;
    }

    if (PC.chromakey_enable == 1 && outColor == vec4(otherColor, otherAlpha)) {
        discard;
    } else {
        switch (PC.ccombine_func) {
        case GR_COMBINE_FUNCTION_ZERO:
            outColor.rgb = vec3(0);
            break;
        case GR_COMBINE_FUNCTION_LOCAL:
            outColor.rgb = localColor;
            break;
        case GR_COMBINE_FUNCTION_LOCAL_ALPHA:
            break;
        case GR_COMBINE_FUNCTION_SCALE_OTHER:
            outColor.rgb = factorColor * otherColor;
            break;
        case GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL:
            outColor.rgb = factorColor * otherColor + localColor;
            break;
        case GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL_ALPHA:
            break;
        case GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL:
            outColor.rgb = factorColor * (otherColor - localColor);
            break;
        case GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL:
            outColor.rgb = factorColor * (otherColor - localColor) + localColor;
            break;
        case GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL_ALPHA:
            break;
        case GR_COMBINE_FUNCTION_SCALE_MINUS_LOCAL_ADD_LOCAL:
            outColor.rgb = factorColor * (-localColor) + localColor;
            break;
        case GR_COMBINE_FUNCTION_SCALE_MINUS_LOCAL_ADD_LOCAL_ALPHA:
            break;
        }

        switch (PC.acombine_func) {
        case GR_COMBINE_FUNCTION_ZERO:
            outColor.a = 0;
            break;
        case GR_COMBINE_FUNCTION_LOCAL:
        case GR_COMBINE_FUNCTION_LOCAL_ALPHA:
            outColor.a = localAlpha;
            break;
        case GR_COMBINE_FUNCTION_SCALE_OTHER:
            outColor.a = factorAlpha * otherAlpha;
            break;
        case GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL:
        case GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL_ALPHA:
            outColor.a = factorAlpha * otherAlpha + localAlpha;
            break;
        case GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL:
            outColor.a = factorAlpha * (otherAlpha - localAlpha);
            break;
        case GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL:
        case GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL_ALPHA:
            outColor.a = factorAlpha * (otherAlpha - localAlpha) + localAlpha;
            break;
        case GR_COMBINE_FUNCTION_SCALE_MINUS_LOCAL_ADD_LOCAL:
        case GR_COMBINE_FUNCTION_SCALE_MINUS_LOCAL_ADD_LOCAL_ALPHA:
            outColor.a = factorAlpha * (-localAlpha) + localAlpha;
            break;
        }

        // The alpha test
        float ref_value = PC.atest_value / 255.0;
        switch (PC.atest_func) {
        case GR_CMP_NEVER:
            discard;
            break;
        case GR_CMP_LESS:
            if (outColor.a >= ref_value) {
                discard;
            }
            break;
        case GR_CMP_EQUAL:
            if (outColor.a != ref_value) {
                discard;
            }
            break;
        case GR_CMP_LEQUAL:
            if (outColor.a > ref_value) {
                discard;
            }
            break;
        case GR_CMP_GREATER:
            if (outColor.a <= ref_value) {
                discard;
            }
            break;
        case GR_CMP_NOTEQUAL:
            if (outColor.a == ref_value) {
                discard;
            }
            break;
        case GR_CMP_GEQUAL:
            if (outColor.a < ref_value) {
                discard;
            }
            break;
        case GR_CMP_ALWAYS:
            break;
        }

        // Fog
        switch (PC.fog_mode) {
            case GR_FOG_DISABLE:
            break;
            case GR_FOG_WITH_ITERATED_ALPHA:
                outColor = inColor.a * PC.fog_color + (1 - inColor.a) * outColor;
            break;
            case GR_FOG_WITH_TABLE:
                float w = log2(1 / (-gl_FragCoord.z + 1)) * 4;
                uint i = uint(w);
                float f = (i < 64 ? fog.table[i] / 255.0 : 1.0) * (1 - (w - i)) + (i < 63 ? (fog.table[i+1] / 255.0) : 1.0) * (w - i);
                outColor = f * PC.fog_color + (1 - f) * outColor;
            break;
            case GR_FOG_WITH_ITERATED_Z:
            case GR_FOG_MULT2:
            case GR_FOG_ADD2:
                break;
        }
    }
}
