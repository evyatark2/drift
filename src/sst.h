#ifndef DRIFT_SST_H
#define DRIFT_SST_H

#include <stdbool.h>

#include "vulkan.h"

#define VK_DEVICE_FUNCTION(func) \
    extern PFN_##func func;

#include "vulkan-functions.inl"

extern bool HAS_WINDOW;
extern bool USE_TRIPLE_BUFFERING;
extern VkPhysicalDevice PHYSICAL_DEVICE;
extern VkDevice DEVICE;
extern VkQueue QUEUE;
extern VkCommandPool POOL;
extern VkExtent2D RESOLUTION;
extern VkExtent2D SWAPCHAIN_EXTENT;
extern VkSwapchainKHR SWAPCHAIN;
extern VkImage *SWAPCHAIN_IMAGES;
extern VkImageView *SWAPCHAIN_IMAGE_VIEWS;
extern VkImage DEPTH_IMAGE;
extern VkImageView DEPTH_IMAGE_VIEW;
extern VkRenderPass RENDER_PASS;
extern VkRenderPass CLEARING_RENDER_PASS;
extern VkImage LAST_FRAME;
extern VkImage LAST_FRAME_MEMORY;
extern VkFramebuffer *FRAMEBUFFERS;

#endif

