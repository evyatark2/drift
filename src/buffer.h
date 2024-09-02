#ifndef DRIFT_BUFFER_H
#define DRIFT_BUFFER_H

#include <stdbool.h>

#include "vulkan.h"
#include "pipeline.h"

extern bool IS_WRITING[3];

extern uint32_t CURRENT_BACKBUFFER;
extern uint32_t CURRENT_AUXBUFFER;

extern VkCommandBuffer *TRANSITION_TO_PRESENT;

extern VkViewport VIEWPORT;
extern VkRect2D SCISSOR;

extern VkPipelineCache PIPELINE_CACHE;

extern bool SHOULD_CLEAR;
extern VkClearColorValue CLEAR_COLOR_VALUE;
extern VkClearDepthStencilValue CLEAR_DEPTH_VALUE;

extern bool SKIP_RENDER;

#endif

