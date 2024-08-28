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
VkRenderPass RENDER_PASS;
VkRenderPass CLEARING_RENDER_PASS;
VkFramebuffer *FRAMEBUFFERS;

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
    LOG(LEVEL_TRACE, "Called\n");
    //uint32_t count;
    //vkEnumeratePhysicalDevices(INSTANCE, &count, NULL);
    hwconfig->num_sst = 1;
    return FXTRUE;
}

FX_ENTRY FxBool FX_CALL grSstQueryHardware(GrHwConfiguration *hwconfig)
{
    LOG(LEVEL_TRACE, "Called\n");
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
    LOG(LEVEL_DEBUG, "resolution: %d, refresh_rate: %d, format: %d, origin: %d, color_buffer_count: %d, aux_buffer_count: %d\n", screen_resolution, refresh_rate, color_format, origin, color_buffer_count, aux_buffer_count);

    assert((color_buffer_count == 2 && aux_buffer_count == 1) || (color_buffer_count == 3 && aux_buffer_count == 0));

    if (color_buffer_count == 3) {
        USE_TRIPLE_BUFFERING = true;
    }

    RESOLUTION = RES_TABLE[screen_resolution];

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

        LONG lStyle = GetWindowLong((HWND)hWnd, GWL_STYLE);
        lStyle &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU);
        SetWindowLong((HWND)hWnd, GWL_STYLE, lStyle);
        SetWindowPos((HWND)hWnd, NULL, 0, 0, RESOLUTION.width, RESOLUTION.height, 0);

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
        vkGetPhysicalDeviceSurfaceFormatsKHR(PHYSICAL_DEVICE, SURFACE, (uint32_t[]) { 1 }, &format);

        LOG(LEVEL_INFO, "Using format: %d\n", format.format);

        VkSurfaceCapabilitiesKHR capabilities;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(PHYSICAL_DEVICE, SURFACE, &capabilities);

        SWAPCHAIN_EXTENT = RESOLUTION;
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

    // Resolve images
    {
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
            .samples               = VK_SAMPLE_COUNT_4_BIT,
            .tiling                = VK_IMAGE_TILING_OPTIMAL,
            .usage                 = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
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

        uint32_t i;
        for (i = 0; i < props.memoryTypeCount; i++) {
            if ((reqs.memoryTypeBits & (1 << i)) && (props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
                break;
            }
        }

        VkMemoryAllocateInfo allocate_info = {
            .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .pNext           = NULL,
            .allocationSize  = reqs.size,
            .memoryTypeIndex = i,
        };
        CHECK_VULKAN_RESULT(vkAllocateMemory(DEVICE, &allocate_info, NULL, &RENDER_TARGET_MEMORY));
        CHECK_VULKAN_RESULT(vkBindImageMemory(DEVICE, RENDER_TARGET, RENDER_TARGET_MEMORY, 0));

        VkImageViewCreateInfo view_info = {
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
                //.baseArrayLayer = 0,
                .layerCount     = 1,
            },
        };

        for (uint32_t i = 0; i < SWAPCHAIN_IMAGE_COUNT; i++) {
            view_info.subresourceRange.baseArrayLayer = i;
            vkCreateImageView(DEVICE, &view_info, NULL, &RENDER_TARGET_VIEWS[i]);
        }
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
            .samples               = VK_SAMPLE_COUNT_4_BIT,
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
        VkAttachmentDescription attachments[] = {
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
            {
                .flags          = 0,
                .format         = format.format,
                .samples        = VK_SAMPLE_COUNT_1_BIT,
                .loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
                .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
                .finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            },
            {
                .flags          = 0,
                .format         = VK_FORMAT_D32_SFLOAT,
                .samples        = VK_SAMPLE_COUNT_4_BIT,
                .loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
                .finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            },
        };
        const VkRenderPassCreateInfo create_info = {
            .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
            .pNext           = NULL,
            .flags           = 0,
            .attachmentCount = USE_TRIPLE_BUFFERING ? 2 : 3,
            .pAttachments    = attachments,
            .subpassCount    = 1,
            .pSubpasses      = (VkSubpassDescription[]) {
                {
                    .flags                   = 0,
                    .pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS,
                    .inputAttachmentCount    = 0,
                    .pInputAttachments       = NULL,
                    .colorAttachmentCount    = 1,
                    .pColorAttachments       = (VkAttachmentReference[]) { { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL } },
                    .pResolveAttachments     = (VkAttachmentReference[]) { { 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL } },
                    .pDepthStencilAttachment = USE_TRIPLE_BUFFERING ? NULL : (VkAttachmentReference[]) { { 2, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL } },
                    .preserveAttachmentCount = 0,
                    .pPreserveAttachments    = NULL,
                }
            },
            .dependencyCount = 1,
            .pDependencies   = (VkSubpassDependency[]) {
                {
                    .srcSubpass      = VK_SUBPASS_EXTERNAL,
                    .dstSubpass      = 0,
                    .srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                    .dstStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                    .srcAccessMask   = 0,
                    .dstAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                    .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
                }
            },
        };
        CHECK_VULKAN_RESULT(vkCreateRenderPass(DEVICE, &create_info, NULL, &RENDER_PASS));

        attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        CHECK_VULKAN_RESULT(vkCreateRenderPass(DEVICE, &create_info, NULL, &CLEARING_RENDER_PASS));
    }

    // Framebuffers
    {
        VkFramebufferCreateInfo create_info = {
            .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .pNext           = NULL,
            .flags           = 0,
            .renderPass      = RENDER_PASS,
            .attachmentCount = USE_TRIPLE_BUFFERING ? 2 : 3,
            //.pAttachments    = ,
            .width           = SWAPCHAIN_EXTENT.width,
            .height          = SWAPCHAIN_EXTENT.height,
            .layers          = 1,
        };

        FRAMEBUFFERS = malloc(SWAPCHAIN_IMAGE_COUNT * sizeof(VkFramebuffer));
        assert(FRAMEBUFFERS != NULL);

        for (uint32_t i = 0; i < SWAPCHAIN_IMAGE_COUNT; i++) {
            create_info.pAttachments = (VkImageView[]) { RENDER_TARGET_VIEWS[i], SWAPCHAIN_IMAGE_VIEWS[i], DEPTH_IMAGE_VIEW };
            CHECK_VULKAN_RESULT(vkCreateFramebuffer(DEVICE, &create_info, NULL, &FRAMEBUFFERS[i]));
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

    frames_init(PHYSICAL_DEVICE, DEVICE, RESOLUTION.width, RESOLUTION.height);
    pipeline_array_init(PIPELINES);
    textures_init();

    HAS_WINDOW = true;
    return FXTRUE;
}

FX_ENTRY void FX_CALL grSstWinClose(void)
{
    LOG(LEVEL_DEBUG, "Called\n");
    texture_memory_termiate();
    vkDeviceWaitIdle(DEVICE);
    vkDestroyPipeline(DEVICE, WIREFRAME_PIPELINE, NULL);
    for (uint32_t i = 0; i < SWAPCHAIN_IMAGE_COUNT; i++) {
        vkDestroyFramebuffer(DEVICE, FRAMEBUFFERS[i], NULL);
    }
    for (uint32_t i = 0; i < SWAPCHAIN_IMAGE_COUNT; i++) {
        vkDestroyImageView(DEVICE, SWAPCHAIN_IMAGE_VIEWS[i], NULL);
        vkDestroyImageView(DEVICE, RENDER_TARGET_VIEWS[i], NULL);
    }
    vkDestroyImage(DEVICE, RENDER_TARGET, NULL);
    vkFreeMemory(DEVICE, RENDER_TARGET_MEMORY, NULL);
    free(SWAPCHAIN_IMAGE_VIEWS);
    free(RENDER_TARGET_VIEWS);
    frames_terminate();
    for (size_t i = 0; i < GLIDE_NUM_TMU; i++) {
        texture_avl_free(&TEXTURES[i]);
    }
    vkDestroyImageView(DEVICE, DEPTH_IMAGE_VIEW, NULL);
    vkDestroyImage(DEVICE, DEPTH_IMAGE, NULL);
    vkFreeMemory(DEVICE, DEPTH_IMAGE_MEMORY, NULL);
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
    LOG(LEVEL_TRACE, "Called\n");
    //vkDeviceWaitIdle(DEVICE);
}

FX_ENTRY FxBool FX_CALL grSstVRetraceOn()
{
    LOG(LEVEL_TRACE, "Called\n");
    return 1;
}

FX_ENTRY void FX_CALL grSstPerfStats(GrSstPerfStats_t* pStats)
{
    LOG(LEVEL_TRACE, "Called\n");
}

FX_ENTRY void FX_CALL grSstResetPerfStats(void)
{
    LOG(LEVEL_TRACE, "Called\n");
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

