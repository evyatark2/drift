/**
 * An open source Glide implementation
 * Copyright © 2022 Evyatar Stalinsky

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "drift.h"

#include "glide.h"
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>

#ifdef __linux__
#include <dlfcn.h>
#endif

#include "sst.h"

#include "common.h"

struct DriftConfig DRIFT_CONFIG;

static void *LIB;
VkInstance INSTANCE;
VkDebugUtilsMessengerEXT MESSENGER;

static bool INITIALIZED;
uint32_t FAMILY_INDEX;

#define VK_EXPORTED_FUNCTION(func) \
    static PFN_##func func;

#define VK_GLOBAL_FUNCTION(func) \
    static PFN_##func func;

#define VK_INSTANCE_FUNCTION(func) \
    PFN_##func func;

#include "vulkan-functions.inl"

static VkBool32 FX_CALL on_log(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT types, const VkDebugUtilsMessengerCallbackDataEXT* data, void *user_data);

FX_ENTRY void FX_CALL grGlideInit()
{
    if (INITIALIZED)
        return;

    LOG(LEVEL_TRACE, "Called\n");

#ifdef __linux__
    LIB = dlopen("libvulkan.so.1", RTLD_NOW);
    assert(LIB != NULL);

#define VK_EXPORTED_FUNCTION(func) \
    func = (PFN_##func)dlsym(LIB, #func); \
    assert(func != NULL);

#include "vulkan-functions.inl"
#else
    LIB = LoadLibraryA("vulkan-1.dll");
    assert(LIB != NULL);

#define VK_EXPORTED_FUNCTION(func) \
    func = (PFN_##func)GetProcAddress(LIB, #func); \
    assert(func != NULL);

#include "vulkan-functions.inl"
#endif

#define VK_GLOBAL_FUNCTION(func) \
    func = (PFN_##func)vkGetInstanceProcAddr(VK_NULL_HANDLE, #func); \
    assert(func != NULL);

#include "vulkan-functions.inl"

    const VkApplicationInfo app_info = {
        .sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext              = NULL,
        .pApplicationName   = NULL,
        .applicationVersion = VK_MAKE_VERSION(0, 0, 1),
        .pEngineName        = "Drift",
        .engineVersion      = VK_MAKE_VERSION(0, 0, 1),
        .apiVersion         = VK_API_VERSION_1_3,
    };

#ifdef __linux__
    const char *const exts[] = { VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_XLIB_SURFACE_EXTENSION_NAME, VK_EXT_DEBUG_UTILS_EXTENSION_NAME, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME };
#else
    const char *const exts[] = { VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME, VK_EXT_DEBUG_UTILS_EXTENSION_NAME, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME };
    //const char *const exts[] = { VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME };
#endif

    const VkDebugUtilsMessengerCreateInfoEXT messenger_info = {
        .sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .pNext           = NULL,
        .flags           = 0,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = on_log,
        .pUserData       = NULL,
    };

    const VkLayerSettingEXT layer_settings[] = {
        { "VK_LAYER_KHRONOS_validation", "validate_core", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &(VkBool32) { VK_TRUE } },
        { "VK_LAYER_KHRONOS_validation", "validate_sync", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &(VkBool32) { VK_TRUE } },
    };

    const VkLayerSettingsCreateInfoEXT layer_settings_create_info = {
        .sType        = VK_STRUCTURE_TYPE_LAYER_SETTINGS_CREATE_INFO_EXT,
        .pNext        = &messenger_info,
        .settingCount = sizeof(layer_settings) / sizeof(layer_settings[0]),
        .pSettings    = layer_settings,
    };

    const VkInstanceCreateInfo create_info = {
        .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext                   = &layer_settings_create_info,
        //.pNext                   = NULL,
        .flags                   = 0,
        .pApplicationInfo        = &app_info,
        .enabledLayerCount       = 1,
        //.enabledLayerCount       = 0,
        .ppEnabledLayerNames     = (const char *const[]) { "VK_LAYER_KHRONOS_validation" },
        .enabledExtensionCount   = sizeof(exts) / sizeof(exts[0]),
        .ppEnabledExtensionNames = exts,
    };

    LOG(LEVEL_TRACE, "Creating instance\n");
    CHECK_VULKAN_RESULT(vkCreateInstance(&create_info, NULL, &INSTANCE));

#define VK_INSTANCE_FUNCTION(func) \
    func = (PFN_##func)vkGetInstanceProcAddr(INSTANCE, #func); \
    //assert(func != NULL);

#include "vulkan-functions.inl"

    LOG(LEVEL_TRACE, "Creating debug messenger\n");
    CHECK_VULKAN_RESULT(vkCreateDebugUtilsMessengerEXT(INSTANCE, &messenger_info, NULL, &MESSENGER));

    INITIALIZED = true;

    {
        uint32_t count = 1;
        vkEnumeratePhysicalDevices(INSTANCE, &count, &PHYSICAL_DEVICE);

        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(PHYSICAL_DEVICE, &props);
        LOG(LEVEL_INFO, "Picked device %s\n", props.deviceName);
    }

    {
        uint32_t count;
        vkGetPhysicalDeviceQueueFamilyProperties(PHYSICAL_DEVICE, &count, NULL);
        VkQueueFamilyProperties props[count];
        assert(props != NULL);
        vkGetPhysicalDeviceQueueFamilyProperties(PHYSICAL_DEVICE, &count, props);
        for (uint32_t i = 0; i < count; i++) {
            if (props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                FAMILY_INDEX = i;
                break;
            }
        }
    }

    {
        const VkDeviceQueueCreateInfo queue_info = {
            .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .pNext            = NULL,
            .flags            = 0,
            .queueFamilyIndex = FAMILY_INDEX,
            .queueCount       = 1,
            .pQueuePriorities = (float[]) { 1.0f },
        };

        const char *const exts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_UNIFORM_BUFFER_STANDARD_LAYOUT_EXTENSION_NAME, VK_EXT_ROBUSTNESS_2_EXTENSION_NAME };

        VkPhysicalDeviceRobustness2FeaturesEXT robustness = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT,
            .pNext = NULL,
            .robustBufferAccess2 = VK_FALSE,
            .robustImageAccess2 = VK_FALSE,
            .nullDescriptor = VK_TRUE,
        };

        VkPhysicalDeviceUniformBufferStandardLayoutFeaturesKHR uniform_standard_layout = {
            .sType                       = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_UNIFORM_BUFFER_STANDARD_LAYOUT_FEATURES_KHR,
            .pNext                       = &robustness,
            .uniformBufferStandardLayout = VK_TRUE,
        };

        const VkDeviceCreateInfo create_info = {
            .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .pNext                   = &uniform_standard_layout,
            .flags                   = 0,
            .queueCreateInfoCount    = 1,
            .pQueueCreateInfos       = &queue_info,
            .enabledLayerCount       = 0,
            //.ppEnabledLayerNames     = ,
            .enabledExtensionCount   = sizeof(exts) / sizeof(exts[0]),
            .ppEnabledExtensionNames = exts,
            .pEnabledFeatures        = NULL,
        };

        CHECK_VULKAN_RESULT(vkCreateDevice(PHYSICAL_DEVICE, &create_info, NULL, &DEVICE));
    }

#define VK_DEVICE_FUNCTION(func) \
    func = (PFN_##func)vkGetDeviceProcAddr(DEVICE, #func); \
    assert(func != NULL);

#include "vulkan-functions.inl"

    vkGetDeviceQueue(DEVICE, FAMILY_INDEX, 0, &QUEUE);

    {
        const VkCommandPoolCreateInfo create_info = {
            .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .pNext            = NULL,
            .flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = FAMILY_INDEX,
        };

        CHECK_VULKAN_RESULT(vkCreateCommandPool(DEVICE, &create_info, NULL, &POOL));
    }

    const char *force_aa;
    if ((force_aa = getenv("DRIFT_FORCE_AA"))) {
        LOG(LEVEL_INFO, "Forcing AA\n");
        DRIFT_CONFIG.force_aa = atoi(force_aa);
    }

    const char *forca_disable_fog;
    if ((forca_disable_fog = getenv("DRIFT_FORCE_DISABLE_FOG"))) {
        LOG(LEVEL_INFO, "Forcing Fog Disable\n");
        DRIFT_CONFIG.force_disable_fog = atoi(forca_disable_fog);
    }
}

void grGlideShutdown()
{
    LOG(LEVEL_TRACE, "Called\n");
    grSstWinClose();
    if (DEVICE != VK_NULL_HANDLE) {
        vkDestroyCommandPool(DEVICE, POOL, NULL);
    }
    vkDestroyDevice(DEVICE, NULL);
    vkDestroyDebugUtilsMessengerEXT(INSTANCE, MESSENGER, NULL);
    vkDestroyInstance(INSTANCE, NULL);
#ifdef __linux__
    dlclose(LIB);
#else
    FreeLibrary(LIB);
#endif
}

static VkBool32 FX_CALL on_log(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT types, const VkDebugUtilsMessengerCallbackDataEXT* data, void *user_data)
{
    enum LogLevel level = LEVEL_ERROR;
    switch (severity) {
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
        level = LEVEL_TRACE;
        break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
        level = LEVEL_INFO;
        break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
        level = LEVEL_WARNING;
        break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
        level = LEVEL_ERROR;
        break;
    default:
        assert(0);
    }

    LOG(level, "%s\n", data->pMessage);
    return VK_FALSE;
}

