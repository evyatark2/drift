#include "vulkan.h"

#include "drift.h"
#include "sst.h"

VkResult vk_create_buffer_and_memory(VkPhysicalDevice physical_device, VkDevice device, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags types, VkBuffer *buffer, VkDeviceMemory *memory)
{
    VkResult ret;
    const VkBufferCreateInfo create_info = {
        .sType                 = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext                 = NULL,
        .flags                 = 0,
        .size                  = size,
        .usage                 = usage,
        .sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
        //.queueFamilyIndexCount = ,
        //.pQueueFamilyIndices   = ,
    };
    if ((ret = vkCreateBuffer(device, &create_info, NULL, buffer)) != VK_SUCCESS)
        return ret;

    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(device, *buffer, &requirements);

    VkPhysicalDeviceMemoryProperties props;
    vkGetPhysicalDeviceMemoryProperties(physical_device, &props);

    uint32_t index = 0;
    for (uint32_t i = 0; i < props.memoryTypeCount; i++) {
        if ((requirements.memoryTypeBits & (1 << i)) && (props.memoryTypes[i].propertyFlags & types)) {
            index = i;
            break;
        }
    }

    const VkMemoryAllocateInfo allocate_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = NULL,
        .allocationSize = requirements.size,
        .memoryTypeIndex = index,
    };
    if ((ret = vkAllocateMemory(device, &allocate_info, NULL, memory)) != VK_SUCCESS)
        return ret;

    return vkBindBufferMemory(device, *buffer, *memory, 0);
}

VkResult vk_name(VkDevice device, VkObjectType type, uint64_t handle, const char *name)
{
    VkDebugUtilsObjectNameInfoEXT name_info = {
        .sType        = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
        .pNext        = NULL,
        .objectType   = type,
        .objectHandle = handle,
        .pObjectName  = name,
    };
    return vkSetDebugUtilsObjectNameEXT(device, &name_info);
}

