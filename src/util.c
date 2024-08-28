#include "util.h"

#include <math.h>

#include "glide.h"
#include "common.h"

FX_ENTRY float FX_CALL guFogTableIndexToW(int i)
{
    return (float)pow(2.0, 3.0 + (double)(i >> 2)) / (8 - (i & 3));
}

FX_ENTRY void FX_CALL guAlphaSource(GrAlphaSource_t mode)
{
    LOG(LEVEL_TRACE, "Called\n");
}

FX_ENTRY void FX_CALL guColorCombineFunction(GrColorCombineFnc_t fnc)
{
    LOG(LEVEL_TRACE, "Called with %d\n", fnc);
    switch (fnc) {
        case GR_COLORCOMBINE_ZERO:
            grColorCombine(GR_COMBINE_FUNCTION_ZERO, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_NONE, FXFALSE);
            break;
        case GR_COLORCOMBINE_CCRGB:
            grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_NONE, FXFALSE);
            break;
        case GR_COLORCOMBINE_ITRGB:
            grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, FXFALSE);
            break;
        case GR_COLORCOMBINE_ITRGB_DELTA0:
            break;
        case GR_COLORCOMBINE_DECAL_TEXTURE:
            grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE, GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, FXFALSE);
            break;
        case GR_COLORCOMBINE_TEXTURE_TIMES_CCRGB:
            grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_LOCAL, GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_TEXTURE, FXFALSE);
            break;
        case GR_COLORCOMBINE_TEXTURE_TIMES_ITRGB:
            grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_LOCAL, GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE, FXFALSE);
            break;
        case GR_COLORCOMBINE_TEXTURE_TIMES_ITRGB_DELTA0:
            break;
        case GR_COLORCOMBINE_TEXTURE_TIMES_ITRGB_ADD_ALPHA:
            grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL_ALPHA, GR_COMBINE_FACTOR_LOCAL, GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE, FXFALSE);
            break;
        case GR_COLORCOMBINE_TEXTURE_TIMES_ALPHA:
            grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_LOCAL_ALPHA, GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, FXFALSE);
            break;
        case GR_COLORCOMBINE_TEXTURE_TIMES_ALPHA_ADD_ITRGB:
            grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL, GR_COMBINE_FACTOR_LOCAL_ALPHA, GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE, FXFALSE);
            break;
        case GR_COLORCOMBINE_TEXTURE_ADD_ITRGB:
            grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL, GR_COMBINE_FACTOR_ONE, GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE, FXFALSE);
            break;
        case GR_COLORCOMBINE_TEXTURE_SUB_ITRGB:
            grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL, GR_COMBINE_FACTOR_ONE, GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE, FXFALSE);
            break;
        case GR_COLORCOMBINE_CCRGB_BLEND_ITRGB_ON_TEXALPHA:
            break;
        case GR_COLORCOMBINE_DIFF_SPEC_A:
            break;
        case GR_COLORCOMBINE_DIFF_SPEC_B:
            break;
        case GR_COLORCOMBINE_ONE:
            grColorCombine(GR_COMBINE_FUNCTION_ZERO, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_NONE, FXTRUE);
            break;
    }
}
