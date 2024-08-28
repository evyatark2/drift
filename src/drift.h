#ifndef DRIFT_GLOBALS_H
#define DRIFT_GLOBALS_H

#include "vulkan.h"

#define VK_INSTANCE_FUNCTION(func) \
    extern PFN_##func func;

#include "vulkan-functions.inl"

extern VkInstance INSTANCE;

#endif

