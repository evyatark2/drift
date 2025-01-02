#include "sst.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#ifndef __linux__
#include <winuser.h>
#endif

#include "glide.h"

#include "common.h"
#include "drift.h"
#include "frame.h"
#include "buffer.h"
#include "pipeline.h"
#include "draw.h"
#include "tex.h"

bool HAS_WINDOW;
bool USE_TRIPLE_BUFFERING;
VkSurfaceKHR SURFACE;
VkPhysicalDevice PHYSICAL_DEVICE;
VkDevice DEVICE;
VkQueue QUEUE;
VkCommandPool POOL;
VkExtent2D RESOLUTION;
VkExtent2D REAL_RESOLUTION;
VkExtent2D SWAPCHAIN_EXTENT;
uint32_t SWAPCHAIN_IMAGE_COUNT;
VkSwapchainKHR SWAPCHAIN;
VkImage *SWAPCHAIN_IMAGES;
VkImageView *SWAPCHAIN_IMAGE_VIEWS;
VkImage DEPTH_IMAGE;
VkDeviceMemory DEPTH_IMAGE_MEMORY;
VkImageView DEPTH_IMAGE_VIEW;
VkImage RENDER_TARGET;
VkDeviceMemory RENDER_TARGET_MEMORY;
VkImageView *RENDER_TARGET_VIEWS;
VkImage FINAL_TARGET;
VkDeviceMemory FINAL_TARGET_MEMORY;
VkImageView *FINAL_TARGET_VIEWS;
VkRenderPass RENDER_PASS;
VkRenderPass CLEARING_RENDER_PASS;
VkRenderPass PP_RENDER_PASS;
VkPipelineLayout PP_LAYOUT;
VkDescriptorSetLayout PP_SET_LAYOUT;
VkPipeline PP_PIPELINE;
VkDescriptorPool PP_POOL;
VkDescriptorSet *PP_SETS;
VkImage LAST_FRAME;
VkImage LAST_FRAME_MEMORY;
VkFramebuffer *FRAMEBUFFERS;
VkFramebuffer *PP_FRAMEBUFFERS;

#define DEFINE_RESOLUTION(w, h) [GR_RESOLUTION_##w##x##h] = { w, h }
VkExtent2D RES_TABLE[] = {
    DEFINE_RESOLUTION(320, 200),
    DEFINE_RESOLUTION(320, 240),
    DEFINE_RESOLUTION(400, 256),
    DEFINE_RESOLUTION(512, 384),
    DEFINE_RESOLUTION(640, 200),
    DEFINE_RESOLUTION(640, 350),
    DEFINE_RESOLUTION(640, 400),
    DEFINE_RESOLUTION(640, 480),
    DEFINE_RESOLUTION(800, 600),
    DEFINE_RESOLUTION(960, 720),
    DEFINE_RESOLUTION(856, 480),
    DEFINE_RESOLUTION(512, 256),
    DEFINE_RESOLUTION(1024, 768),
    DEFINE_RESOLUTION(1280, 1024),
    DEFINE_RESOLUTION(1600, 1200),
    DEFINE_RESOLUTION(400, 300),
    DEFINE_RESOLUTION(1152, 864),
    DEFINE_RESOLUTION(1280, 960),
    DEFINE_RESOLUTION(1600, 1024),
    DEFINE_RESOLUTION(1792, 1344),
    DEFINE_RESOLUTION(1856, 1392),
    DEFINE_RESOLUTION(1920, 1440),
    DEFINE_RESOLUTION(2048, 1536),
    DEFINE_RESOLUTION(2048, 2048),
};

#define VK_DEVICE_FUNCTION(func) \
    PFN_##func func;

#include "vulkan-functions.inl"

static void *load_file(const char *name, size_t *size);

FX_ENTRY FxBool FX_CALL grSstQueryBoards(GrHwConfiguration *hwconfig)
{
    LOG_CALL();
    //uint32_t count;
    //vkEnumeratePhysicalDevices(INSTANCE, &count, NULL);
    hwconfig->num_sst = 1;
    return FXTRUE;
}

FX_ENTRY FxBool FX_CALL grSstQueryHardware(GrHwConfiguration *hwconfig)
{
    LOG_CALL();
    hwconfig->num_sst = 1;
    hwconfig->SSTs[0].type = GR_SSTTYPE_VOODOO;
    hwconfig->SSTs[0].sstBoard.Voodoo2Config.fbRam = 4;
    hwconfig->SSTs[0].sstBoard.Voodoo2Config.fbiRev = 1;
    hwconfig->SSTs[0].sstBoard.Voodoo2Config.nTexelfx = 2;
    hwconfig->SSTs[0].sstBoard.Voodoo2Config.sliDetect = 0;
    for (uint32_t i = 0; i < 2; i++) {
        hwconfig->SSTs[0].sstBoard.Voodoo2Config.tmuConfig[i].tmuRam = 4;
        hwconfig->SSTs[0].sstBoard.Voodoo2Config.tmuConfig[i].tmuRev = 1;
    }
    return FXTRUE;
}

#ifdef __linux__
FX_ENTRY FxBool FX_CALL grSstWinOpen(Display *dpy, Window win, GrScreenResolution_t screen_resolution, GrScreenRefresh_t refresh_rate, GrColorFormat_t color_format, GrOriginLocation_t origin, int color_buffer_count, int aux_buffer_count)
#else
FX_ENTRY FxBool FX_CALL grSstWinOpen(FxU32 hWnd, GrScreenResolution_t screen_resolution, GrScreenRefresh_t refresh_rate, GrColorFormat_t color_format, GrOriginLocation_t origin, int color_buffer_count, int aux_buffer_count)
#endif
{
    LOG(LEVEL_DEBUG, "resolution: %d (%dx%d), refresh_rate: %d, format: %d, origin: %d, color_buffer_count: %d, aux_buffer_count: %d\n", screen_resolution, RES_TABLE[screen_resolution].width, RES_TABLE[screen_resolution].height, refresh_rate, color_format, origin, color_buffer_count, aux_buffer_count);

    assert((color_buffer_count == 2 && aux_buffer_count == 1) || (color_buffer_count == 3 && aux_buffer_count == 0));

    if (color_buffer_count == 3) {
        USE_TRIPLE_BUFFERING = true;
    }

    RESOLUTION = RES_TABLE[screen_resolution];
    REAL_RESOLUTION = RESOLUTION;
    
    if (DRIFT_CONFIG.force_resolution) {
        int w, h;
        sscanf(DRIFT_CONFIG.force_resolution, "%dx%d", &w, &h);
        REAL_RESOLUTION.width = w;
        REAL_RESOLUTION.height = h;
    }

    {
#ifdef __linux__
        const VkXlibSurfaceCreateInfoKHR create_info = {
            .sType  = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR,
            .pNext  = NULL,
            .flags  = 0,
            .dpy    = dpy,
            .window = win,
        };

        CHECK_VULKAN_RESULT(vkCreateXlibSurfaceKHR(INSTANCE, &create_info, NULL, &SURFACE));
#else
        if (hWnd == 0)
            hWnd = (FxU32)GetActiveWindow();

        // Borderless
        LONG style = GetWindowLong((HWND)hWnd, GWL_STYLE);
        style &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU);
        SetWindowLong((HWND)hWnd, GWL_STYLE, style);
        SetWindowPos((HWND)hWnd, NULL, 0, 0, REAL_RESOLUTION.width, REAL_RESOLUTION.height, 0);

        const VkWin32SurfaceCreateInfoKHR create_info = {
            .sType     = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
            .pNext     = NULL,
            .flags     = 0,
            .hinstance = GetModuleHandle(NULL),
            .hwnd      = (HWND)hWnd,
        };

        CHECK_VULKAN_RESULT(vkCreateWin32SurfaceKHR(INSTANCE, &create_info, NULL, &SURFACE));
#endif
    }

    VkSurfaceFormatKHR format;
    {
        uint32_t count = 2;
        VkSurfaceFormatKHR formats[2];
        vkGetPhysicalDeviceSurfaceFormatsKHR(PHYSICAL_DEVICE, SURFACE, &count, formats);
        // FIXME: The second format is most likely a UNORM format, but it isn't guaranteed
        format = formats[1];
        
        LOG(LEVEL_INFO, "Using format: %d\n", format.format);

        VkSurfaceCapabilitiesKHR capabilities;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(PHYSICAL_DEVICE, SURFACE, &capabilities);

        SWAPCHAIN_EXTENT = REAL_RESOLUTION;
        if (SWAPCHAIN_EXTENT.width < capabilities.minImageExtent.width) {
            LOG(LEVEL_WARNING, "Width less than min extent width; width: %u, min: %u\n", SWAPCHAIN_EXTENT.width, capabilities.minImageExtent.width);
            SWAPCHAIN_EXTENT.width = capabilities.minImageExtent.width;
        } else if (SWAPCHAIN_EXTENT.width > capabilities.maxImageExtent.width) {
            LOG(LEVEL_WARNING, "Width greater than max extent width; width: %u, max: %u\n", SWAPCHAIN_EXTENT.width, capabilities.maxImageExtent.width);
            SWAPCHAIN_EXTENT.width = capabilities.maxImageExtent.width;
            //return FXFALSE;
        }

        if (SWAPCHAIN_EXTENT.height < capabilities.minImageExtent.height) {
            LOG(LEVEL_WARNING, "Height less than min extent height; height: %u, min: %u\n", SWAPCHAIN_EXTENT.height, capabilities.minImageExtent.height);
            SWAPCHAIN_EXTENT.height = capabilities.minImageExtent.height;
            //return FXFALSE;
        } else if (SWAPCHAIN_EXTENT.height > capabilities.maxImageExtent.height) {
            LOG(LEVEL_WARNING, "Height greater than max extent height; height: %u, max: %u\n", SWAPCHAIN_EXTENT.height, capabilities.maxImageExtent.height);
            SWAPCHAIN_EXTENT.height = capabilities.maxImageExtent.height;
            //return FXFALSE;
        }

        SWAPCHAIN_EXTENT = capabilities.currentExtent;

        LOG(LEVEL_INFO, "Current Extent: (%d, %d)\n", capabilities.currentExtent.width, capabilities.currentExtent.height);

        const VkSwapchainCreateInfoKHR create_info = {
            .sType                 = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
            .pNext                 = NULL,
            .flags                 = 0,
            .surface               = SURFACE,
            .minImageCount         = capabilities.minImageCount,
            .imageFormat           = format.format,
            .imageColorSpace       = format.colorSpace,
            .imageExtent           = SWAPCHAIN_EXTENT,
            .imageArrayLayers      = 1,
            .imageUsage            = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            .imageSharingMode      = VK_SHARING_MODE_EXCLUSIVE,
            //.queueFamilyIndexCount = ,
            //.pQueueFamilyIndices   = ,
            .preTransform          = capabilities.currentTransform,
            .compositeAlpha        = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            .presentMode           = VK_PRESENT_MODE_FIFO_KHR,
            .clipped               = VK_TRUE,
            .oldSwapchain          = VK_NULL_HANDLE,
        };

        CHECK_VULKAN_RESULT(vkCreateSwapchainKHR(DEVICE, &create_info, NULL, &SWAPCHAIN));

        CHECK_VULKAN_RESULT(vkGetSwapchainImagesKHR(DEVICE, SWAPCHAIN, &SWAPCHAIN_IMAGE_COUNT, NULL));
        SWAPCHAIN_IMAGES = malloc(SWAPCHAIN_IMAGE_COUNT * sizeof(VkImage));
        assert(SWAPCHAIN_IMAGES != NULL);
        CHECK_VULKAN_RESULT(vkGetSwapchainImagesKHR(DEVICE, SWAPCHAIN, &SWAPCHAIN_IMAGE_COUNT, SWAPCHAIN_IMAGES));
    }

    // Swapchain image views
    {
        VkImageViewCreateInfo create_info = {
            .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext            = NULL,
            .flags            = 0,
            //.image            = ,
            .viewType         = VK_IMAGE_VIEW_TYPE_2D,
            .format           = format.format,
            .components       = {
                .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                .a = VK_COMPONENT_SWIZZLE_IDENTITY,
            },
            .subresourceRange = {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = 0,
                .layerCount     = 1,
            },
        };

        SWAPCHAIN_IMAGE_VIEWS = malloc(SWAPCHAIN_IMAGE_COUNT * sizeof(VkImageView));
        assert(SWAPCHAIN_IMAGE_VIEWS != NULL);

        for (uint32_t i = 0; i < SWAPCHAIN_IMAGE_COUNT; i++) {
            create_info.image = SWAPCHAIN_IMAGES[i];
            CHECK_VULKAN_RESULT(vkCreateImageView(DEVICE, &create_info, NULL, SWAPCHAIN_IMAGE_VIEWS + i));
        }
    }

    // Viewport and scissor
    {
        VIEWPORT.x        = 0;
        VIEWPORT.y        = 0;
        VIEWPORT.width    = (float)SWAPCHAIN_EXTENT.width;
        VIEWPORT.height   = (float)SWAPCHAIN_EXTENT.height;
        VIEWPORT.minDepth = 0.0;
        VIEWPORT.maxDepth = 1.0;

        SCISSOR.offset = (VkOffset2D) { 0, 0 };
        SCISSOR.extent = SWAPCHAIN_EXTENT;

        VIEWPORT_STATE.pViewports = &VIEWPORT;
        VIEWPORT_STATE.pScissors = &SCISSOR;
    }

    // Resolve images
    if (DRIFT_CONFIG.force_aa || DRIFT_CONFIG.enable_pp) {
        RENDER_TARGET_VIEWS = malloc(SWAPCHAIN_IMAGE_COUNT * sizeof(VkImageView));
        assert(RENDER_TARGET_VIEWS != NULL);
        
        VkImageCreateInfo create_info = {
            .sType                 = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .pNext                 = NULL,
            .flags                 = 0,
            .imageType             = VK_IMAGE_TYPE_2D,
            .format                = format.format,
            .extent                = { SWAPCHAIN_EXTENT.width, SWAPCHAIN_EXTENT.height, 1 },
            .mipLevels             = 1,
            .arrayLayers           = SWAPCHAIN_IMAGE_COUNT,
            .samples               = DRIFT_CONFIG.force_aa ? VK_SAMPLE_COUNT_4_BIT : VK_SAMPLE_COUNT_1_BIT,
            .tiling                = VK_IMAGE_TILING_OPTIMAL,
            .usage                 = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | (DRIFT_CONFIG.enable_pp ? VK_IMAGE_USAGE_SAMPLED_BIT : 0),
            .sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
            //.queueFamilyIndexCount = ,
            //.pQueueFamilyIndices   = ,
            .initialLayout         = VK_IMAGE_LAYOUT_UNDEFINED,
        };
        CHECK_VULKAN_RESULT(vkCreateImage(DEVICE, &create_info, NULL, &RENDER_TARGET));

        VkMemoryRequirements reqs;
        vkGetImageMemoryRequirements(DEVICE, RENDER_TARGET, &reqs);

        VkPhysicalDeviceMemoryProperties props;
        vkGetPhysicalDeviceMemoryProperties(PHYSICAL_DEVICE, &props);

        uint32_t index = 0;
        for (uint32_t i = 0; i < props.memoryTypeCount; i++) {
            if ((reqs.memoryTypeBits & (1 << i)) && (props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
                index = i;
                break;
            }
        }

        VkMemoryAllocateInfo allocate_info = {
            .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .pNext           = NULL,
            .allocationSize  = reqs.size,
            .memoryTypeIndex = index,
        };
        CHECK_VULKAN_RESULT(vkAllocateMemory(DEVICE, &allocate_info, NULL, &RENDER_TARGET_MEMORY));
        CHECK_VULKAN_RESULT(vkBindImageMemory(DEVICE, RENDER_TARGET, RENDER_TARGET_MEMORY, 0));

        VkImageViewCreateInfo view_create_info = {
            .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext            = NULL,
            .flags            = 0,
            .image            = RENDER_TARGET,
            .viewType         = VK_IMAGE_VIEW_TYPE_2D,
            .format           = format.format,
            .components       = {
                .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                .a = VK_COMPONENT_SWIZZLE_IDENTITY,
            },
            .subresourceRange = {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = 0,
                .layerCount     = 1,
            },
        };
        for (size_t i = 0; i < SWAPCHAIN_IMAGE_COUNT; i++) {
            view_create_info.subresourceRange.baseArrayLayer = i;
            CHECK_VULKAN_RESULT(vkCreateImageView(DEVICE, &view_create_info, NULL, &RENDER_TARGET_VIEWS[i]));
        }
    }

    // Post-process targets
    if (DRIFT_CONFIG.enable_pp) {
        FINAL_TARGET_VIEWS = malloc(SWAPCHAIN_IMAGE_COUNT * sizeof(VkImageView));
        assert(FINAL_TARGET_VIEWS != NULL);
        
        VkImageCreateInfo create_info = {
            .sType                 = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .pNext                 = NULL,
            .flags                 = 0,
            .imageType             = VK_IMAGE_TYPE_2D,
            .format                = format.format,
            .extent                = { SWAPCHAIN_EXTENT.width, SWAPCHAIN_EXTENT.height, 1 },
            .mipLevels             = 1,
            .arrayLayers           = SWAPCHAIN_IMAGE_COUNT,
            .samples               = VK_SAMPLE_COUNT_4_BIT,
            .tiling                = VK_IMAGE_TILING_OPTIMAL,
            .usage                 = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            .sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
            //.queueFamilyIndexCount = ,
            //.pQueueFamilyIndices   = ,
            .initialLayout         = VK_IMAGE_LAYOUT_UNDEFINED,
        };
        CHECK_VULKAN_RESULT(vkCreateImage(DEVICE, &create_info, NULL, &FINAL_TARGET));

        VkMemoryRequirements reqs;
        vkGetImageMemoryRequirements(DEVICE, FINAL_TARGET, &reqs);

        VkPhysicalDeviceMemoryProperties props;
        vkGetPhysicalDeviceMemoryProperties(PHYSICAL_DEVICE, &props);

        uint32_t index = 0;
        for (uint32_t i = 0; i < props.memoryTypeCount; i++) {
            if ((reqs.memoryTypeBits & (1 << i)) && (props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
                index = i;
                break;
            }
        }

        VkMemoryAllocateInfo allocate_info = {
            .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .pNext           = NULL,
            .allocationSize  = reqs.size,
            .memoryTypeIndex = index,
        };
        CHECK_VULKAN_RESULT(vkAllocateMemory(DEVICE, &allocate_info, NULL, &FINAL_TARGET_MEMORY));
        CHECK_VULKAN_RESULT(vkBindImageMemory(DEVICE, FINAL_TARGET, FINAL_TARGET_MEMORY, 0));

        VkImageViewCreateInfo view_create_info = {
            .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext            = NULL,
            .flags            = 0,
            .image            = FINAL_TARGET,
            .viewType         = VK_IMAGE_VIEW_TYPE_2D,
            .format           = format.format,
            .components       = {
                .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                .a = VK_COMPONENT_SWIZZLE_IDENTITY,
            },
            .subresourceRange = {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = 0,
                .layerCount     = 1,
            },
        };
        for (size_t i = 0; i < SWAPCHAIN_IMAGE_COUNT; i++) {
            view_create_info.subresourceRange.baseArrayLayer = i;
            CHECK_VULKAN_RESULT(vkCreateImageView(DEVICE, &view_create_info, NULL, &FINAL_TARGET_VIEWS[i]));
        }
    }

    if (DRIFT_CONFIG.enable_pp) {
        VkAttachmentDescription attachments[] = {
            // Output image (Resolved)
            {
                .flags          = 0,
                .format         = format.format,
                .samples        = VK_SAMPLE_COUNT_1_BIT,
                .loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
                .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
                .finalLayout    = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            },
            // Render target
            {
                .flags          = 0,
                .format         = format.format,
                .samples        = VK_SAMPLE_COUNT_4_BIT,
                .loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
                .finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            },
        };

        const VkRenderPassCreateInfo create_info = {
            .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
            .pNext           = NULL,
            .flags           = 0,
            .attachmentCount = DRIFT_CONFIG.force_aa ? 2 : 1,
            .pAttachments    = attachments,
            .subpassCount    = 1,
            .pSubpasses      = (VkSubpassDescription[]) {
                {
                    .flags                   = 0,
                    .pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS,
                    .inputAttachmentCount    = 0,
                    .pInputAttachments       = NULL,
                    .colorAttachmentCount    = 1,
                    .pColorAttachments       = (VkAttachmentReference[]) { { DRIFT_CONFIG.force_aa ? 1 : 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL } },
                    .pResolveAttachments     = DRIFT_CONFIG.force_aa ? (VkAttachmentReference[]) { { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL } } : NULL,
                    .pDepthStencilAttachment = NULL,
                    .preserveAttachmentCount = 0,
                    .pPreserveAttachments    = NULL,
                },
            },
            .dependencyCount = 2,
            .pDependencies   = (VkSubpassDependency[]) {
                {
                    .srcSubpass      = VK_SUBPASS_EXTERNAL,
                    .dstSubpass      = 0,
                    .srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                    .dstStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                    .srcAccessMask   = DRIFT_CONFIG.force_aa ? VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT : 0,
                    .dstAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                    .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
                },
                {
                    .srcSubpass      = 0,
                    .dstSubpass      = VK_SUBPASS_EXTERNAL,
                    .srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                    .dstStageMask    = VK_PIPELINE_STAGE_TRANSFER_BIT,
                    .srcAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                    .dstAccessMask   = VK_ACCESS_TRANSFER_READ_BIT,
                    .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
                },
            },
        };
        CHECK_VULKAN_RESULT(vkCreateRenderPass(DEVICE, &create_info, NULL, &PP_RENDER_PASS));
        LOG(LEVEL_INFO, "Created render pass\n");
    }

    if (DRIFT_CONFIG.enable_pp) {
        VkDescriptorSetLayoutCreateInfo create_info = {
            .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .pNext        = NULL,
            .flags        = 0,
            .bindingCount = 1,
            .pBindings    = (VkDescriptorSetLayoutBinding[]) {
                {
                    .binding            = 0,
                    .descriptorType     = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    .descriptorCount    = 1,
                    .stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT,
                    .pImmutableSamplers = NULL,
                }
            }
        };
        vkCreateDescriptorSetLayout(DEVICE, &create_info, NULL, &PP_SET_LAYOUT);
        LOG(LEVEL_INFO, "Created descriptor set layout\n");
    }

    if (DRIFT_CONFIG.enable_pp) {
        const VkPipelineLayoutCreateInfo create_info = {
            .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .pNext                  = NULL,
            .flags                  = 0,
            .setLayoutCount         = 1,
            .pSetLayouts            = &PP_SET_LAYOUT,
            .pushConstantRangeCount = 0,
            //.pPushConstantRanges    = ,
        };
        CHECK_VULKAN_RESULT(vkCreatePipelineLayout(DEVICE, &create_info, NULL, &PP_LAYOUT));
        LOG(LEVEL_INFO, "Created pipeline layout\n");
    }

    if (DRIFT_CONFIG.enable_pp) {
        VkPipelineShaderStageCreateInfo stages[] = {
            {
                .sType               = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .pNext               = NULL,
                .flags               = 0,
                .stage               = VK_SHADER_STAGE_VERTEX_BIT,
                //.module              = , // Should be initialized in grSstWinOpen
                .pName               = "main",
                .pSpecializationInfo = NULL,
            },
            {
                .sType               = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .pNext               = NULL,
                .flags               = 0,
                .stage               = VK_SHADER_STAGE_FRAGMENT_BIT,
                //.module              = , // Should be initialized in grSstWinOpen
                .pName               = "main",
                .pSpecializationInfo = NULL,
            }
        };

        {
            size_t size;
            void *code = load_file("pp_vert.spv", &size);

            VkShaderModuleCreateInfo create_info = {
                .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                .pNext    = NULL,
                .flags    = 0,
                .codeSize = size,
                .pCode    = code,
            };

            CHECK_VULKAN_RESULT(vkCreateShaderModule(DEVICE, &create_info, NULL, &stages[0].module));

            free(code);
            code = load_file("pp_frag.spv", &size);
            create_info.codeSize = size;
            create_info.pCode = code;

            CHECK_VULKAN_RESULT(vkCreateShaderModule(DEVICE, &create_info, NULL, &stages[1].module));
        }

        VkPipelineVertexInputStateCreateInfo input_state = {
            .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .pNext                           = NULL,
            .flags                           = 0,
            .vertexBindingDescriptionCount   = 0,
            //.pVertexBindingDescriptions      = ,
            .vertexAttributeDescriptionCount = 0,
            //.pVertexAttributeDescriptions    = ,
        };

        VkPipelineInputAssemblyStateCreateInfo assembly_state = {
            .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .pNext                  = NULL,
            .flags                  = 0,
            .topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            .primitiveRestartEnable = VK_FALSE,
        };

        VkPipelineViewportStateCreateInfo viewport_state = {
            .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .pNext         = NULL,
            .flags         = 0,
            .viewportCount = 1,
            .pViewports    = &VIEWPORT,
            .scissorCount  = 1,
            .pScissors     = &SCISSOR,
        };

        VkPipelineRasterizationStateCreateInfo rasterization_state = {
            .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .pNext                   = NULL,
            .flags                   = 0,
            .depthClampEnable        = VK_FALSE,
            .rasterizerDiscardEnable = VK_FALSE,
            .polygonMode             = VK_POLYGON_MODE_FILL,
            .cullMode                = VK_CULL_MODE_BACK_BIT,
            .frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE,
            .depthBiasEnable         = VK_FALSE,
            //.depthBiasConstantFactor = ,
            //.depthBiasClamp          = ,
            //.depthBiasSlopeFactor    = ,
            .lineWidth               = 1.0,
        };

        VkPipelineMultisampleStateCreateInfo multisample_state = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .pNext = NULL,
            .flags = 0,
            .rasterizationSamples = DRIFT_CONFIG.force_aa ? VK_SAMPLE_COUNT_4_BIT : VK_SAMPLE_COUNT_1_BIT,
            .sampleShadingEnable = VK_FALSE,
            .minSampleShading = 0.0f,
            .pSampleMask = NULL,
            .alphaToCoverageEnable = VK_FALSE,
            .alphaToOneEnable = VK_FALSE,
        };

        const VkPipelineColorBlendStateCreateInfo color_blend_state = {
            .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .pNext           = NULL,
            .flags           = 0,
            .logicOpEnable   = VK_FALSE,
            //.logicOp         = ,
            .attachmentCount = 1,
            .pAttachments    = (VkPipelineColorBlendAttachmentState[]) {
                {
                    .blendEnable         = VK_FALSE,
                    // The spec says that these should have vaild values even when .blendEnable == FALSE
                    .srcColorBlendFactor = VK_BLEND_FACTOR_ZERO,
                    .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
                    .colorBlendOp        = VK_BLEND_OP_ADD,
                    .srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
                    .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
                    .alphaBlendOp        = VK_BLEND_OP_ADD,
                    .colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
                }
            },
            //.blendConstants  = ,
        };

        VkGraphicsPipelineCreateInfo create_info = {
            .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .pNext               = NULL,
            .flags               = 0,
            .stageCount          = 2,
            .pStages             = stages,
            .pVertexInputState   = &input_state,
            .pInputAssemblyState = &assembly_state,
            .pTessellationState  = NULL,
            .pViewportState      = &viewport_state,
            .pRasterizationState = &rasterization_state,
            .pMultisampleState   = &multisample_state,
            .pDepthStencilState  = NULL,
            .pColorBlendState    = &color_blend_state,
            .pDynamicState       = NULL,
            .layout              = PP_LAYOUT,
            .renderPass          = PP_RENDER_PASS,
            .subpass             = 0,
            .basePipelineHandle  = VK_NULL_HANDLE,
            .basePipelineIndex   = -1,
        };
        vkCreateGraphicsPipelines(DEVICE, VK_NULL_HANDLE, 1, &create_info, NULL, &PP_PIPELINE);
        LOG(LEVEL_INFO, "Created pipeline\n");
    }

    // Post-process sources
    if (DRIFT_CONFIG.enable_pp) {
        VkDescriptorPoolCreateInfo create_info = {
            .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .pNext         = NULL,
            .flags         = 0,
            .maxSets       = SWAPCHAIN_IMAGE_COUNT,
            .poolSizeCount = 1,
            .pPoolSizes    = (VkDescriptorPoolSize[]) {
                { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, SWAPCHAIN_IMAGE_COUNT }
            },
        };
        CHECK_VULKAN_RESULT(vkCreateDescriptorPool(DEVICE, &create_info, NULL, &PP_POOL));

        PP_SETS = malloc(SWAPCHAIN_IMAGE_COUNT * sizeof(VkDescriptorSet));
        assert(PP_SETS != NULL);

        VkDescriptorSetLayout layouts[SWAPCHAIN_IMAGE_COUNT];
        for (uint32_t i = 0; i < SWAPCHAIN_IMAGE_COUNT; i++) {
            layouts[i] = PP_SET_LAYOUT;
        }
        VkDescriptorSetAllocateInfo allocate_info = {
            .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .pNext              = NULL,
            .descriptorPool     = PP_POOL,
            .descriptorSetCount = SWAPCHAIN_IMAGE_COUNT,
            .pSetLayouts        = layouts,
        };
        CHECK_VULKAN_RESULT(vkAllocateDescriptorSets(DEVICE, &allocate_info, PP_SETS));

        VkWriteDescriptorSet write = {
            .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext            = NULL,
            //.dstSet           = ,
            .dstBinding       = 0,
            .dstArrayElement  = 0,
            .descriptorCount  = 1,
            .descriptorType   = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .pImageInfo       = &(VkDescriptorImageInfo) {
                //.sampler     = ,
                //.imageView   = ,
                //.imageLayout = ,
            },
            //.pBufferInfo      = ,
            //.pTexelBufferView = ,
        };

        LOG(LEVEL_INFO, "Updating pp sources\n");
        for (uint32_t i = 0; i < SWAPCHAIN_IMAGE_COUNT; i++) {
            write.dstSet = PP_SETS[i];
            write.pImageInfo = &(VkDescriptorImageInfo) { .imageView = RENDER_TARGET_VIEWS[i], .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            vkUpdateDescriptorSets(DEVICE, 1, &write, 0, NULL);
        }

        LOG(LEVEL_INFO, "Created pp sources\n");
    }

    // Depth image
    {
        const VkImageCreateInfo create_info = {
            .sType                 = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .pNext                 = NULL,
            .flags                 = 0,
            .imageType             = VK_IMAGE_TYPE_2D,
            .format                = VK_FORMAT_D32_SFLOAT,
            .extent                = { SWAPCHAIN_EXTENT.width, SWAPCHAIN_EXTENT.height, 1 },
            .mipLevels             = 1,
            .arrayLayers           = 1,
            .samples               = DRIFT_CONFIG.force_aa ? VK_SAMPLE_COUNT_4_BIT : VK_SAMPLE_COUNT_1_BIT,
            .tiling                = VK_IMAGE_TILING_OPTIMAL,
            .usage                 = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            .sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
            //.queueFamilyIndexCount = ,
            //.pQueueFamilyIndices   = ,
            .initialLayout         = VK_IMAGE_LAYOUT_UNDEFINED,
        };
        CHECK_VULKAN_RESULT(vkCreateImage(DEVICE, &create_info, NULL, &DEPTH_IMAGE));

        VkMemoryRequirements requirements;
        vkGetImageMemoryRequirements(DEVICE, DEPTH_IMAGE, &requirements);

        VkPhysicalDeviceMemoryProperties props;
        vkGetPhysicalDeviceMemoryProperties(PHYSICAL_DEVICE, &props);

        uint32_t index = 0;
        for (uint32_t i = 0; i < props.memoryTypeCount; i++) {
            if ((requirements.memoryTypeBits & (1 << i)) && (props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
                index = i;
                break;
            }
        }

        VkMemoryAllocateInfo allocate_info = {
            .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .pNext           = NULL,
            .allocationSize  = requirements.size,
            .memoryTypeIndex = index,
        };
        CHECK_VULKAN_RESULT(vkAllocateMemory(DEVICE, &allocate_info, NULL, &DEPTH_IMAGE_MEMORY));
        CHECK_VULKAN_RESULT(vkBindImageMemory(DEVICE, DEPTH_IMAGE, DEPTH_IMAGE_MEMORY, 0));

        const VkImageViewCreateInfo view_create_info = {
            .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext            = NULL,
            .flags            = 0,
            .image            = DEPTH_IMAGE,
            .viewType         = VK_IMAGE_VIEW_TYPE_2D,
            .format           = VK_FORMAT_D32_SFLOAT,
            .components       = {
                .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                .a = VK_COMPONENT_SWIZZLE_IDENTITY,
            },
            .subresourceRange = {
                .aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = 0,
                .layerCount     = 1,
            },
        };
        CHECK_VULKAN_RESULT(vkCreateImageView(DEVICE, &view_create_info, NULL, &DEPTH_IMAGE_VIEW));
    }

    // Render pass
    {
        /* This table describes the various values in the structures below
         * when one is given the boolean triplet (enable_pp, force_aa, USE_TRIPLE_BUFFERING)*/
        /*  +----------+-----------+-----------+-----------+-----------+
            | pp/aa,TB | 0,0       | 0,1       | 1,0       | 1,1       |
            +----------+-----------+-----------+-----------+-----------+
            | 0        | Depth     | Swapchain | Depth     | Swapchain |
            |          | Swapchain |           | Swapchain | Target4   |
            |          |           |           | Tatget4   |           |
            +----------+-----------+-----------+-----------+-----------+
            | 1        | Depth     | Target    | Depth     | Target4   |
            |          | Target    |           | Target4   |           |
            +----------+-----------+-----------+-----------+-----------+

            Where:
                Depth - depth attachment when Triple buffering is off
                Swapchain - A swapchain image should be attached in the framebuffer;
                    used when no post-process is necessary so we can immediatly resolve/render to the swapchain
                Target - A non-swapchain single-sampled image should be attached in the framebuffer;
                    needed when multisampling isn't enabled and we need the output for the post-process render pass
                Target4 - A non-swapchain multi-sampled image should be attached;
                    used either as a render target when multisampling, or as an input for the post-process render pass */
        VkAttachmentDescription attachments[] = {
            // Depth buffer
            {
                .flags          = 0,
                .format         = VK_FORMAT_D32_SFLOAT,
                .samples        = DRIFT_CONFIG.force_aa ? VK_SAMPLE_COUNT_4_BIT : VK_SAMPLE_COUNT_1_BIT,
                .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
                .finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            },
            // Output image (Resolved)
            {
                .flags          = 0,
                .format         = format.format,
                .samples        = DRIFT_CONFIG.force_aa && DRIFT_CONFIG.enable_pp ? VK_SAMPLE_COUNT_4_BIT : VK_SAMPLE_COUNT_1_BIT,
                .loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
                .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
                .finalLayout    = DRIFT_CONFIG.enable_pp ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            },
            // Render target
            {
                .flags          = 0,
                .format         = format.format,
                .samples        = VK_SAMPLE_COUNT_4_BIT,
                .loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
                .finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            },
        };
        const VkRenderPassCreateInfo create_info = {
            .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
            .pNext           = NULL,
            .flags           = 0,
            .attachmentCount = DRIFT_CONFIG.force_aa ? (USE_TRIPLE_BUFFERING ? (DRIFT_CONFIG.enable_pp ? 1 : 2) : (DRIFT_CONFIG.enable_pp ? 2 : 3)) : (USE_TRIPLE_BUFFERING ? 1 : 2),
            .pAttachments    = USE_TRIPLE_BUFFERING ? attachments + 1 : attachments,
            .subpassCount    = 1,
            .pSubpasses      = (VkSubpassDescription[]) {
                {
                    .flags                   = 0,
                    .pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS,
                    .inputAttachmentCount    = 0,
                    .pInputAttachments       = NULL,
                    .colorAttachmentCount    = 1,
                    .pColorAttachments       = (VkAttachmentReference[]) { { DRIFT_CONFIG.force_aa ? (USE_TRIPLE_BUFFERING ? (DRIFT_CONFIG.enable_pp ? 0 : 1) : (DRIFT_CONFIG.enable_pp ? 1 : 2)) : (USE_TRIPLE_BUFFERING ? 0 : 1), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL } },
                    .pResolveAttachments     = DRIFT_CONFIG.force_aa && !DRIFT_CONFIG.enable_pp ? (VkAttachmentReference[]) { { (USE_TRIPLE_BUFFERING ? 0 : 1), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL } } : NULL,
                    .pDepthStencilAttachment = USE_TRIPLE_BUFFERING ? NULL : (VkAttachmentReference[]) { { 0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL } },
                    .preserveAttachmentCount = 0,
                    .pPreserveAttachments    = NULL,
                },
            },
            .dependencyCount = 3,
            .pDependencies   = (VkSubpassDependency[]) {
                {
                    .srcSubpass      = VK_SUBPASS_EXTERNAL,
                    .dstSubpass      = 0,
                    .srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                    .dstStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                    .srcAccessMask   = DRIFT_CONFIG.force_aa && !DRIFT_CONFIG.enable_pp ? VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT : 0,
                    .dstAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                    .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
                },
                {
                    .srcSubpass      = 0,
                    .dstSubpass      = VK_SUBPASS_EXTERNAL,
                    .srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                    .dstStageMask    = DRIFT_CONFIG.enable_pp ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_TRANSFER_BIT,
                    .srcAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                    .dstAccessMask   = DRIFT_CONFIG.enable_pp ? VK_ACCESS_SHADER_READ_BIT : VK_ACCESS_TRANSFER_READ_BIT,
                    .dependencyFlags = DRIFT_CONFIG.enable_pp ? 0 : VK_DEPENDENCY_BY_REGION_BIT,
                },
                {
                    .srcSubpass      = VK_SUBPASS_EXTERNAL,
                    .dstSubpass      = 0,
                    .srcStageMask    = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                    .dstStageMask    = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                    .srcAccessMask   = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                    .dstAccessMask   = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                    .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
                }
            },
        };
        CHECK_VULKAN_RESULT(vkCreateRenderPass(DEVICE, &create_info, NULL, &RENDER_PASS));

        if (DRIFT_CONFIG.enable_pp) {
            if (USE_TRIPLE_BUFFERING) {
                attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            } else {
                attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            }
        } else {
            if (DRIFT_CONFIG.force_aa) {
                if (USE_TRIPLE_BUFFERING) {
                    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                } else {
                    attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                }
            } else {
                if (USE_TRIPLE_BUFFERING) {
                    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                } else {
                    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                }
            }
        }
        CHECK_VULKAN_RESULT(vkCreateRenderPass(DEVICE, &create_info, NULL, &CLEARING_RENDER_PASS));
    }

    // Framebuffers
    {
        VkFramebufferCreateInfo create_info = {
            .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .pNext           = NULL,
            .flags           = 0,
            .renderPass      = RENDER_PASS,
            .attachmentCount = DRIFT_CONFIG.force_aa ? (USE_TRIPLE_BUFFERING ? ( DRIFT_CONFIG.enable_pp ? 1 : 2) : (DRIFT_CONFIG.enable_pp ? 2 : 3)) : (USE_TRIPLE_BUFFERING ? 1 : 2),
            //.pAttachments    = ,
            .width           = SWAPCHAIN_EXTENT.width,
            .height          = SWAPCHAIN_EXTENT.height,
            .layers          = 1,
        };

        FRAMEBUFFERS = malloc(SWAPCHAIN_IMAGE_COUNT * sizeof(VkFramebuffer));
        assert(FRAMEBUFFERS != NULL);

        for (uint32_t i = 0; i < SWAPCHAIN_IMAGE_COUNT; i++) {
            VkImageView views[3];
            if (DRIFT_CONFIG.force_aa) {
                views[0] = DEPTH_IMAGE_VIEW;
                views[1] = DRIFT_CONFIG.enable_pp ? RENDER_TARGET_VIEWS[i] : SWAPCHAIN_IMAGE_VIEWS[i];
                views[2] = RENDER_TARGET_VIEWS[i];
            } else {
                views[0] = DEPTH_IMAGE_VIEW;
                views[1] = DRIFT_CONFIG.enable_pp ? RENDER_TARGET_VIEWS[i] : SWAPCHAIN_IMAGE_VIEWS[i];
            }
            create_info.pAttachments = USE_TRIPLE_BUFFERING ? views + 1 : views;
            CHECK_VULKAN_RESULT(vkCreateFramebuffer(DEVICE, &create_info, NULL, &FRAMEBUFFERS[i]));
        }

        LOG(LEVEL_INFO, "Created framebuffers\n");

        if (DRIFT_CONFIG.enable_pp) {
            PP_FRAMEBUFFERS = malloc(SWAPCHAIN_IMAGE_COUNT * sizeof(VkFramebuffer));
            assert(PP_FRAMEBUFFERS != NULL);

            create_info.renderPass = PP_RENDER_PASS;
            create_info.attachmentCount = DRIFT_CONFIG.force_aa ? 2 : 1;

            for (uint32_t i = 0; i < SWAPCHAIN_IMAGE_COUNT; i++) {
                VkImageView views[2] = { SWAPCHAIN_IMAGE_VIEWS[i], FINAL_TARGET_VIEWS[i]};
                create_info.pAttachments = views;
                CHECK_VULKAN_RESULT(vkCreateFramebuffer(DEVICE, &create_info, NULL, &PP_FRAMEBUFFERS[i]));
            }
        }
    }

    // Shaders
    {
        size_t size;
        void *code = load_file("vert.spv", &size);

        VkShaderModuleCreateInfo create_info = {
            .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .pNext    = NULL,
            .flags    = 0,
            .codeSize = size,
            .pCode    = code,
        };

        CHECK_VULKAN_RESULT(vkCreateShaderModule(DEVICE, &create_info, NULL, &SHADER_INFO[0].module));

        free(code);
        code = load_file("frag.spv", &size);
        create_info.codeSize = size;
        create_info.pCode = code;

        CHECK_VULKAN_RESULT(vkCreateShaderModule(DEVICE, &create_info, NULL, &SHADER_INFO[1].module));
    }

    {
        VkImageCreateInfo create_info = {
            .sType                 = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .pNext                 = NULL,
            .flags                 = 0,
            .imageType             = VK_IMAGE_TYPE_2D,
            .format                = format.format,
            .extent                = { SWAPCHAIN_EXTENT.width, SWAPCHAIN_EXTENT.height, 1 },
            .mipLevels             = 1,
            .arrayLayers           = 1,
            .samples               = VK_SAMPLE_COUNT_1_BIT,
            .tiling                = VK_IMAGE_TILING_OPTIMAL,
            .usage                 = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            .sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
            //.queueFamilyIndexCount = ,
            //.pQueueFamilyIndices   = ,
            .initialLayout         = VK_IMAGE_LAYOUT_UNDEFINED,
        };
        CHECK_VULKAN_RESULT(vkCreateImage(DEVICE, &create_info, NULL, &LAST_FRAME));

        VkMemoryRequirements requirements;
        vkGetImageMemoryRequirements(DEVICE, DEPTH_IMAGE, &requirements);

        VkPhysicalDeviceMemoryProperties props;
        vkGetPhysicalDeviceMemoryProperties(PHYSICAL_DEVICE, &props);

        uint32_t index = 0;
        for (uint32_t i = 0; i < props.memoryTypeCount; i++) {
            if ((requirements.memoryTypeBits & (1 << i)) && (props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
                index = i;
                break;
            }
        }

        VkMemoryAllocateInfo allocate_info = {
            .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .pNext           = NULL,
            .allocationSize  = requirements.size,
            .memoryTypeIndex = index,
        };
        CHECK_VULKAN_RESULT(vkAllocateMemory(DEVICE, &allocate_info, NULL, &LAST_FRAME_MEMORY));
        CHECK_VULKAN_RESULT(vkBindImageMemory(DEVICE, LAST_FRAME, LAST_FRAME_MEMORY, 0));
    }

    frames_init(PHYSICAL_DEVICE, DEVICE, RESOLUTION.width, RESOLUTION.height);
    pipeline_array_init(PIPELINES);
    textures_init();

    // HACK
    frame_render(frames_get_current(), PIPELINES);
    frame_present(frames_get_current());
    frames_advance();

    HAS_WINDOW = true;
    return FXTRUE;
}

FX_ENTRY void FX_CALL grSstWinClose(void)
{
    LOG_CALL();
    vkDeviceWaitIdle(DEVICE);
    textures_terminate();
    vkDestroyPipeline(DEVICE, WIREFRAME_PIPELINE, NULL);
    for (uint32_t i = 0; i < SWAPCHAIN_IMAGE_COUNT; i++) {
        vkDestroyFramebuffer(DEVICE, FRAMEBUFFERS[i], NULL);
    }
    for (uint32_t i = 0; i < SWAPCHAIN_IMAGE_COUNT; i++) {
        vkDestroyImageView(DEVICE, SWAPCHAIN_IMAGE_VIEWS[i], NULL);
    }
    free(SWAPCHAIN_IMAGE_VIEWS);
    frames_terminate();
    for (size_t i = 0; i < GLIDE_NUM_TMU; i++) {
        texture_avl_free(&TEXTURES[i]);
    }
    vkDestroyImageView(DEVICE, DEPTH_IMAGE_VIEW, NULL);
    vkDestroyImage(DEVICE, DEPTH_IMAGE, NULL);
    vkFreeMemory(DEVICE, DEPTH_IMAGE_MEMORY, NULL);
    vkDestroyImage(DEVICE, LAST_FRAME, NULL);
    vkFreeMemory(DEVICE, LAST_FRAME_MEMORY, NULL);
    vkDestroyRenderPass(DEVICE, CLEARING_RENDER_PASS, NULL);
    vkDestroyRenderPass(DEVICE, RENDER_PASS, NULL);
    vkDestroyShaderModule(DEVICE, SHADER_INFO[0].module, NULL);
    vkDestroyShaderModule(DEVICE, SHADER_INFO[1].module, NULL);
    free(SWAPCHAIN_IMAGES);
    vkDestroySwapchainKHR(DEVICE, SWAPCHAIN, NULL);
    vkDestroySurfaceKHR(INSTANCE, SURFACE, NULL);
}

FX_ENTRY FxBool FX_CALL grSstControl(FxU32 code)
{
    return 1;
}

FX_ENTRY void FX_CALL grSstOrigin(GrOriginLocation_t origin)
{
}

FX_ENTRY void FX_CALL grSstSelect(int which_sst)
{
}

FX_ENTRY void FX_CALL grSstIdle()
{
    LOG_CALL();
    //vkDeviceWaitIdle(DEVICE);
}

FX_ENTRY FxBool FX_CALL grSstVRetraceOn()
{
    LOG_CALL();
    return 1;
}

FX_ENTRY void FX_CALL grSstPerfStats(GrSstPerfStats_t* pStats)
{
    LOG_CALL();
}

FX_ENTRY void FX_CALL grSstResetPerfStats(void)
{
    LOG_CALL();
}

static void *load_file(const char *name, size_t *size)
{
    FILE *file;
#ifdef __linux__
    file = fopen(name, "rb");
    assert(file != NULL);
#else
    assert(fopen_s(&file, name, "rb") == 0);
#endif
    assert(fseek(file, 0, SEEK_END) == 0);
    *size = ftell(file);
    assert(*size != -1);
    rewind(file);
    char *data = malloc(*size);
    LOG(LEVEL_TRACE, "Size: %zu\n", *size);
    assert(data != NULL);
    size_t bytes_read = 0;
    while (bytes_read < *size) {
        int status = fread(data + bytes_read, 1, *size - bytes_read, file);
        if (status < *size - bytes_read) {
            if (ferror(file) != 0) {
                LOG(LEVEL_ERROR, "Failed reading %s: %d\n", name, ferror(file));
                assert(0);
            } else if (feof(file)) {
                LOG(LEVEL_ERROR, "Unexpected end of line\n");
                assert(0);
            }
        }
        bytes_read += status;
        LOG(LEVEL_TRACE, "Size: %zu\n", bytes_read);
    }
    //assert(fread(data, 1, *size, file) == *size);
    fclose(file);
    return data;
}

