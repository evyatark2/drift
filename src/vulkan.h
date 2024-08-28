#ifndef DRIFT_VULKAN_H
#define DRIFT_VULKAN_H

#define VK_NO_PROTOTYPES
#ifdef __linux__
#define VK_USE_PLATFORM_XLIB_KHR
#else
#define VK_USE_PLATFORM_WIN32_KHR
#endif

#include <vulkan/vulkan.h>

VkResult vk_create_buffer_and_memory(VkPhysicalDevice physical_device, VkDevice device, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags types, VkBuffer *buffer, VkDeviceMemory *memory);
VkResult vk_name(VkDevice device, VkObjectType type, uint64_t handle, const char *name);

#endif

