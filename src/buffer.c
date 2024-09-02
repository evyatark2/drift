#include "buffer.h"

#include <stdbool.h>
#include <stdint.h>

#include "glide.h"

#include "common.h"
#include "sst.h"
#include "pipeline.h"
#include "frame.h"

uint32_t CURRENT_BACKBUFFER;
uint32_t CURRENT_AUXBUFFER;

VkFence FENCE;

bool IS_WRITING[3];

VkViewport VIEWPORT;
VkRect2D SCISSOR;

bool SHOULD_CLEAR;
VkClearColorValue CLEAR_COLOR_VALUE;
VkClearDepthStencilValue CLEAR_DEPTH_VALUE;

bool SKIP_RENDER;

uint32_t FRAME_COUNT;

FX_ENTRY void FX_CALL grBufferClear(GrColor_t color, GrAlpha_t alpha, FxU16 depth)
{
    LOG(LEVEL_TRACE, "Called");

    SHOULD_CLEAR = true;
    // FIXME: Assuming that the glide buffer format is ABGR
    CLEAR_COLOR_VALUE.float32[0] = (color >> 0  & 0xFF) / 255.0f;
    CLEAR_COLOR_VALUE.float32[1] = (color >> 8  & 0xFF) / 255.0f;
    CLEAR_COLOR_VALUE.float32[2] = (color >> 16 & 0xFF) / 255.0f;
    CLEAR_COLOR_VALUE.float32[3] = (color >> 24 & 0xFF) / 255.0f;

    CLEAR_DEPTH_VALUE.depth = depth / 65535.0f;

    // Reset vertex buffer and the mesh cache
    pipeline_array_reset(PIPELINES);
    staging_pipeline_force_change();
    staging_fog_table_force_change();
    frame_reset(frames_get_current());
}

FX_ENTRY void FX_CALL grBufferSwap(int swap_interval)
{
    FRAME_COUNT++;
    LOG(LEVEL_TRACE, "------------- Frame %d -------------\n", FRAME_COUNT);

    for (int i = 0; i < swap_interval; i++) {
        frame_skip(frames_get_current());
    }

    frame_render(frames_get_current(), PIPELINES);
    frame_present(frames_get_current());

    // Next frame
    frames_advance();
}

FX_ENTRY int FX_CALL grBufferNumPending()
{
    return 0;
}

FX_ENTRY FxBool FX_CALL grLfbLock(GrLock_t type, GrBuffer_t buffer, GrLfbWriteMode_t mode, GrOriginLocation_t origin, FxBool pixel_pipeline, GrLfbInfo_t *info)
{
    // TODO: Flush (render) all the grDraw()s that have happened until this point before returning
    LOG(LEVEL_TRACE, "type: %d, buffer: %d, mode: %d, origin: %d, pipeline: %d\n", type, buffer, mode, origin, pixel_pipeline);

    frame_lock(frames_get_current(), &info->lfbPtr);
    // TODO: Copy the frame's image data to the buffer
    IS_WRITING[buffer] = type & GR_LFB_WRITE_ONLY;

    // Reset the frame's resources
    pipeline_array_reset(PIPELINES);
    staging_pipeline_force_change();
    staging_fog_table_force_change();
    frame_reset(frames_get_current());

    info->origin = origin;
    info->strideInBytes = RESOLUTION.width * 2;
    info->writeMode = mode;

    return 1;
}


FX_ENTRY FxBool FX_CALL grLfbUnlock(GrLock_t type, GrBuffer_t buffer)
{
    LOG(LEVEL_TRACE, "type: %d, buffer: %d\n", type, buffer);

    frame_unlock(frames_get_current());

    if (IS_WRITING[buffer]) {
        SKIP_RENDER = true;
    }

    return 1;
}

