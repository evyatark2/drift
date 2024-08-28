#include <assert.h>
#include <stdio.h>

#include <windows.h>

#include "glide.h"

void WINAPI grGlideInit_thunk()
{
    grGlideInit();
}

void WINAPI grGlideShutdown_thunk()
{
    grGlideShutdown();
}

FxBool WINAPI grSstQueryBoards_thunk(GrHwConfiguration *hwconfig)
{
    return grSstQueryBoards(hwconfig);
}

FxBool WINAPI grSstQueryHardware_thunk(GrHwConfiguration *hwconfig)
{
    return grSstQueryHardware(hwconfig);
}

int WINAPI grSstWinOpen_thunk(FxU32 hWnd, GrScreenResolution_t screen_resolution, GrScreenRefresh_t refresh_rate, GrColorFormat_t color_format, GrOriginLocation_t origin_location, int nColBuffers, int nAuxBuffers)
{
    //printf("HWND: %u", hWnd);
    Display *display = XOpenDisplay(NULL);
    Window window = (Window)GetPropA(hWnd != 0 ? (HWND)hWnd : GetActiveWindow(), "__wine_x11_whole_window");

    int width, height;
    switch (screen_resolution) {
    case GR_RESOLUTION_320x200:
        width = 320;
        height = 200;
    break;

    case GR_RESOLUTION_320x240:
        width = 320;
        height = 240;
    break;

    case GR_RESOLUTION_400x256:
        width = 400;
        height = 256;
    break;

    case GR_RESOLUTION_512x384:
        width = 512;
        height = 384;
    break;

    case GR_RESOLUTION_640x200:
        width = 640;
        height = 200;
    break;

    case GR_RESOLUTION_640x350:
        width = 640;
        height = 350;
    break;

    case GR_RESOLUTION_640x400:
        width = 640;
        height = 400;
    break;

    case GR_RESOLUTION_640x480:
        width = 640;
        height = 480;
    break;

    case GR_RESOLUTION_800x600:
        width = 800;
        height = 600;
    break;

    case GR_RESOLUTION_960x720:
        width = 960;
        height = 720;
    break;

    case GR_RESOLUTION_856x480:
        width = 856;
        height = 480;
    break;

    case GR_RESOLUTION_512x256:
        width = 512;
        height = 256;
    break;

    case GR_RESOLUTION_1024x768:
        width = 1024;
        height = 768;
    break;

    case GR_RESOLUTION_1280x1024:
        width = 1280;
        height = 1024;
    break;

    case GR_RESOLUTION_1600x1200:
        width = 1600;
        height = 1200;
    break;

    case GR_RESOLUTION_400x300:
        width = 400;
        height = 300;
    break;

    case GR_RESOLUTION_1152x864:
        width = 1152;
        height = 864;
    break;

    case GR_RESOLUTION_1280x960:
        width = 1280;
        height = 960;
    break;

    case GR_RESOLUTION_1600x1024:
        width = 1600;
        height = 1024;
    break;

    case GR_RESOLUTION_1792x1344:
        width = 1792;
        height = 1344;
    break;

    case GR_RESOLUTION_1856x1392:
        width = 1856;
        height = 1392;
    break;

    case GR_RESOLUTION_1920x1440:
        width = 1920;
        height = 1440;
    break;

    case GR_RESOLUTION_2048x1536:
        width = 2048;
        height = 1536;
    break;

    case GR_RESOLUTION_2048x2048:
        width = 2048;
        height = 2048;
    break;

    case GR_RESOLUTION_NONE:
        assert(0);
    break;
    }

    SetWindowPos((HWND)hWnd, NULL, 0, 0, width, height, 0);

    return grSstWinOpen(display, window, screen_resolution, refresh_rate, color_format, origin_location, nColBuffers, nAuxBuffers);
}

void WINAPI grSstWinClose_thunk()
{
    grSstWinClose();
}

FxBool WINAPI grSstControl_thunk(FxU32 code)
{
    return grSstControl(code);
}

void WINAPI grSstOrigin_thunk(GrOriginLocation_t origin)
{
    grSstOrigin(origin);
}

void WINAPI grSstSelect_thunk(int which_sst)
{
    grSstSelect(which_sst);
}

void WINAPI grSstIdle_thunk()
{
    grSstIdle();
}

FxBool WINAPI grSstVRetraceOn_thunk()
{
    return grSstVRetraceOn();
}

void WINAPI grSstPerfStats_thunk(GrSstPerfStats_t *pStats)
{
    grSstPerfStats(pStats);
}

void WINAPI grSstResetPerfStats_thunk()
{
    grSstResetPerfStats();
}

void WINAPI grBufferClear_thunk(GrColor_t color, GrAlpha_t alpha, FxU16 depth)
{
    grBufferClear(color, alpha, depth);
}

void WINAPI grBufferSwap_thunk(int swap_interval)
{
    grBufferSwap(swap_interval);
}

int WINAPI grBufferNumPending_thunk()
{
    return grBufferNumPending();
}

FxBool WINAPI grLfbLock_thunk(GrLock_t type, GrBuffer_t buffer, GrLfbWriteMode_t writeMode, GrOriginLocation_t origin, FxBool pixelPipeline, GrLfbInfo_t *info)
{
    return grLfbLock(type, buffer, writeMode, origin, pixelPipeline, info);
}

FxBool WINAPI grLfbUnlock_thunk(GrLock_t type, GrBuffer_t buffer)
{
    return grLfbUnlock(type, buffer);
}

void WINAPI grDrawTriangle_thunk(const GrVertex *a, const GrVertex *b, const GrVertex *c)
{
    grDrawTriangle(a, b, c);
}

void WINAPI grDrawLine_thunk(const GrVertex *a, const GrVertex *b)
{
    grDrawLine(a, b);
}

void WINAPI grColorMask_thunk(FxBool rgb, FxBool a)
{
    grColorMask(rgb, a);
}

void WINAPI grDepthBufferMode_thunk(GrDepthBufferMode_t mode)
{
    grDepthBufferMode(mode);
}

void WINAPI grDepthMask_thunk(FxBool mask)
{
    grDepthMask(mask);
}

void WINAPI grDepthBufferFunction_thunk(GrCmpFnc_t function)
{
    grDepthBufferFunction(function);
}

void WINAPI grColorCombine_thunk(GrCombineFunction_t function, GrCombineFactor_t factor, GrCombineLocal_t local, GrCombineOther_t other, FxBool invert)
{
    grColorCombine(function, factor, local, other, invert);
}

void WINAPI grAlphaCombine_thunk(GrCombineFunction_t function, GrCombineFactor_t factor, GrCombineLocal_t local, GrCombineOther_t other, FxBool invert)
{
    grAlphaCombine(function, factor, local, other, invert);
}

void WINAPI grAlphaBlendFunction_thunk(GrAlphaBlendFnc_t rgb_sf, GrAlphaBlendFnc_t rgb_df, GrAlphaBlendFnc_t alpha_sf, GrAlphaBlendFnc_t alpha_df)
{
    grAlphaBlendFunction(rgb_sf, rgb_df, alpha_sf, alpha_df);
}

void WINAPI grTexFilterMode_thunk(GrChipID_t tmu, GrTextureFilterMode_t minfilter_mode, GrTextureFilterMode_t magfilter_mode)
{
    grTexFilterMode(tmu, minfilter_mode, magfilter_mode);
}

void WINAPI grTexLodBiasValue_thunk(GrChipID_t tmu, float bias)
{
    grTexLodBiasValue(tmu, bias);
}

void WINAPI grTexDownloadTable_thunk(GrChipID_t tmu, GrTexTable_t type, void *data)
{
    grTexDownloadTable(tmu, type, data);
}

void WINAPI grTexSource_thunk(GrChipID_t tmu, FxU32 startAddress, FxU32 evenOdd, GrTexInfo *info)
{
    grTexSource(tmu, startAddress, evenOdd, info);
}

void WINAPI grTexCombineFunction_thunk(GrChipID_t tmu, GrTextureCombineFnc_t fnc)
{
    grTexCombineFunction(tmu, fnc);
}

void WINAPI grTexClampMode_thunk(GrChipID_t tmu, GrTextureClampMode_t s_clampmode, GrTextureClampMode_t t_clampmode)
{
    return grTexClampMode(tmu, s_clampmode, t_clampmode);
}

FxU32 WINAPI grTexMinAddress_thunk(GrChipID_t tmu)
{
   return grTexMinAddress(tmu);
}

FxU32 WINAPI grTexMaxAddress_thunk(GrChipID_t tmu)
{
   return grTexMaxAddress(tmu);
}

void WINAPI grTexDownloadMipMap_thunk(GrChipID_t tmu, FxU32 startAddress, FxU32 evenOdd, GrTexInfo *info)
{
    grTexDownloadMipMap(tmu, startAddress, evenOdd, info);
}

FxU32 WINAPI grTexTextureMemRequired_thunk(FxU32 evenOdd, GrTexInfo *info)
{
    return grTexTextureMemRequired(evenOdd, info);
}

void WINAPI grTexMipMapMode_thunk(GrChipID_t tmu, GrMipMapMode_t mode, FxBool lodBlend)
{
    grTexMipMapMode(tmu, mode, lodBlend);
}

void WINAPI grFogMode_thunk(GrFogMode_t mode)
{
	grFogMode(mode);
}

void WINAPI grFogTable_thunk(const GrFog_t *ft)
{
	grFogTable(ft);
}

void WINAPI grFogColorValue_thunk(GrColor_t fogcolor)
{
	grFogColorValue(fogcolor);
}

void WINAPI grChromakeyValue_thunk(GrColor_t value)
{
	grChromakeyValue(value);
}

void WINAPI grChromakeyMode_thunk(GrChromakeyMode_t mode)
{
	grChromakeyMode(mode);
}

void WINAPI grAlphaTestReferenceValue_thunk(GrAlpha_t value)
{
	grAlphaTestReferenceValue(value);
}

void WINAPI grAlphaTestFunction_thunk(GrCmpFnc_t function)
{
	grAlphaTestFunction(function);
}

float WINAPI guFogTableIndexToW_thunk(int i)
{
    return guFogTableIndexToW(i);
}

void WINAPI guAlphaSource_thunk(GrAlphaSource_t mode)
{
    guAlphaSource(mode);
}

void WINAPI guColorCombineFunction_thunk(GrColorCombineFnc_t fnc)
{
    guColorCombineFunction(fnc);
}

