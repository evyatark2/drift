#include "tex.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan_core.h>

#include "mesh.h"
#include "common.h"
#include "drift.h"
#include "glide.h"
#include "sst.h"

#define TEXTURE_MEMORY_SIZE (4 * 1024 * 1024)

VkBuffer TEXTURE_BUFFER[GLIDE_NUM_TMU];
VkDeviceMemory TEXTURE_MEMORY[GLIDE_NUM_TMU];

struct TransferNode {
    VkCommandBuffer cb;
    VkFence fence;
    VkDeviceMemory mem;
    VkBuffer buf;
};

struct {
    size_t capacity;
    size_t start;
    size_t len;
    struct TransferNode *nodes;
} CB_RING;

static struct TransferNode *get_cb();

static int texture_node_key_cmp(struct TextureNodeKey *t1, struct TextureNodeKey *t2);

struct TextureNode *texture_avl_insert(struct TextureAVL *avl, struct TextureNodeKey *key);

struct TextureNode *texture_avl_get(struct TextureAVL *avl, struct TextureNodeKey *key);
void texture_avl_invalidate(struct TextureAVL *avl, size_t low, size_t high);

struct TextureAVL TEXTURES[GLIDE_NUM_TMU];
bool SHOULD_RESET[GLIDE_NUM_TMU];

static VkFormat gr_format_to_vk_format(GrTextureFormat_t format);
static int32_t get_tex_memory(FxU32 even_odd, const GrTexInfo* info);
static int32_t get_aligned_tex_memory(FxU32 even_odd, const GrTexInfo* info);

struct SamplerNodeOption;

union SamplerNode {
    struct SamplerNodeOption *children;
    struct {
        size_t count;
        float *biases;
        VkSampler *samplers;
    };
};

struct SamplerNodeOption {
    bool occupied;
    union SamplerNode node;
};

union SamplerNode SAMPLER_CACHE_ROOT;

FX_ENTRY void FX_CALL grTexFilterMode(GrChipID_t tmu, GrTextureFilterMode_t min, GrTextureFilterMode_t mag)
{
    LOG(LEVEL_TRACE, "Setting filter mode for TMU %d to (min: %d, mag: %d)\n", tmu, min, mag);
    // GrTextureFilterMode_t and VkFilter overlap in values

    staging_mesh_set_filter_mode(tmu, min == GR_TEXTUREFILTER_POINT_SAMPLED ? VK_FILTER_NEAREST : VK_FILTER_LINEAR, mag == GR_TEXTUREFILTER_POINT_SAMPLED ? VK_FILTER_NEAREST : VK_FILTER_LINEAR);
}

FX_ENTRY void FX_CALL grTexLodBiasValue(GrChipID_t tmu, float value)
{
    LOG(LEVEL_TRACE, "Setting LOD bias for TMU %d to %f\n", tmu, value);
    staging_mesh_set_mipmap_bias(tmu, value);
}

FX_ENTRY void FX_CALL grTexDownloadTable(GrChipID_t tmu, GrTexTable_t type, void* data)
{
    LOG(LEVEL_TRACE, "Called\n");
}

FX_ENTRY void FX_CALL grTexSource(GrChipID_t tmu, FxU32 start_address, FxU32 even_odd, GrTexInfo* info)
{
    LOG(LEVEL_TRACE, "Called on %d:0x%x\n", tmu, start_address);

    if (SHOULD_RESET[tmu]) {
        texture_avl_free(&TEXTURES[tmu]);
        SHOULD_RESET[tmu] = false;
    }

    GrLOD_t large_lod = info->largeLod;
    GrLOD_t small_lod = info->smallLod;
    if ((even_odd == GR_MIPMAPLEVELMASK_EVEN && large_lod % 2 == 1) || (even_odd == GR_MIPMAPLEVELMASK_ODD && large_lod % 2 == 0))
        large_lod++;
    if ((even_odd == GR_MIPMAPLEVELMASK_EVEN && small_lod % 2 == 1) || (even_odd == GR_MIPMAPLEVELMASK_ODD && small_lod % 2 == 0))
        small_lod--;

    VkExtent2D extent;
    uint32_t aspect = info->aspectRatio - 3;
    if (aspect > 3) // aspect was underflowed
        aspect = -aspect;

    if (info->aspectRatio < 3) {
        extent.width = 1 << (8 - large_lod);
        extent.height = extent.width / (1 << aspect);
    } else {
        extent.height = 1 << (8 - large_lod);
        extent.width = extent.height / (1 << aspect);
    }

    struct TextureNodeKey key = {
        .address = start_address,
        .even_odd = even_odd,
        .tiny = small_lod,
        .large = large_lod,
        .aspect = info->aspectRatio,
        .format = info->format,
    };

    struct TextureNode *node = texture_avl_get(&TEXTURES[tmu], &key);

    if (node == NULL) {
        struct TransferNode *tnode = get_cb();

        LOG(LEVEL_DEBUG, "Creating texutre\n");

        node = texture_avl_insert(&TEXTURES[tmu], &key);
        VkImageCreateInfo image_info = {
            .sType                 = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .pNext                 = NULL,
            .flags                 = 0,
            .imageType             = VK_IMAGE_TYPE_2D,
            .format                = gr_format_to_vk_format(info->format),
            .extent                = (VkExtent3D) { extent.width, extent.height, 1 },
            .mipLevels             = small_lod - large_lod + 1,
            .arrayLayers           = 1,
            .samples               = VK_SAMPLE_COUNT_1_BIT,
            .tiling                = VK_IMAGE_TILING_OPTIMAL,
            .usage                 = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | (even_odd != GR_MIPMAPLEVELMASK_BOTH ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0),
            .sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
            //.queueFamilyIndexCount = ,
            //.pQueueFamilyIndices   = ,
            .initialLayout         = VK_IMAGE_LAYOUT_UNDEFINED,
        };
        CHECK_VULKAN_RESULT(vkCreateImage(DEVICE, &image_info, NULL, &node->image));

        VkMemoryRequirements reqs;
        vkGetImageMemoryRequirements(DEVICE, node->image, &reqs);

        VkPhysicalDeviceMemoryProperties props;
        vkGetPhysicalDeviceMemoryProperties(PHYSICAL_DEVICE, &props);

        uint32_t i;
        for (i = 0; i < props.memoryTypeCount; i++) {
            if ((reqs.memoryTypeBits & (1 << i)) && (props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
                break;
            }
        }

        const VkMemoryAllocateInfo allocate_info = {
            .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .pNext           = NULL,
            .allocationSize  = reqs.size,
            .memoryTypeIndex = i,
        };
        CHECK_VULKAN_RESULT(vkAllocateMemory(DEVICE, &allocate_info, NULL, &node->memory));

        CHECK_VULKAN_RESULT(vkBindImageMemory(DEVICE, node->image, node->memory, 0));

        const VkImageViewCreateInfo view_info = {
            .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext            = NULL,
            .flags            = 0,
            .image            = node->image,
            .viewType         = VK_IMAGE_VIEW_TYPE_2D,
            .format           = gr_format_to_vk_format(info->format),
            .components       = {
                VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY
            },
            .subresourceRange = {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel   = 0,
                .levelCount     = small_lod - large_lod + 1,
                .baseArrayLayer = 0,
                .layerCount     = 1,
            },
        };
        CHECK_VULKAN_RESULT(vkCreateImageView(DEVICE, &view_info, NULL, &node->view));

        const VkCommandBufferBeginInfo begin_info = {
            .sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext            = NULL,
            .flags            = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            .pInheritanceInfo = NULL,
        };

        VkCommandBuffer cb = tnode->cb;
        vkBeginCommandBuffer(cb, &begin_info);

        VkImageMemoryBarrier barrier = {
            .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext               = NULL,
            .srcAccessMask       = 0,
            .dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            //.srcQueueFamilyIndex = ,
            //.dstQueueFamilyIndex = ,
            .image               = node->image,
            .subresourceRange    = {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel   = 0,
                .levelCount     = small_lod - large_lod + 1,
                .baseArrayLayer = 0,
                .layerCount     = 1,
            },
        };
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);

        size_t region_count = small_lod - large_lod + 1;
        if (even_odd != GR_MIPMAPLEVELMASK_BOTH) {
            region_count /= 2;
        }
        VkBufferImageCopy regions[region_count];

        int32_t offset = start_address;
        uint32_t mip = 0;
        for (uint32_t i = 0; i < region_count; i++) {
            regions[i].bufferOffset = offset;
            regions[i].bufferRowLength = 0;
            regions[i].bufferImageHeight = 0;
            regions[i].imageSubresource = (VkImageSubresourceLayers) {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel       = mip,
                .baseArrayLayer = 0,
                .layerCount     = 1,
            };
            regions[i].imageOffset = (VkOffset3D) { 0, 0 };
            regions[i].imageExtent = (VkExtent3D) { extent.width / (1 << mip), extent.height / (1 << mip), 1 };

            offset += (extent.width / (1 << mip)) * (extent.height / (1 << mip)) * (info->format >= GR_TEXFMT_16BIT ? 2 : 1);
            mip++;
            if (even_odd != GR_MIPMAPLEVELMASK_BOTH)
                mip++;
        }

        vkCmdCopyBufferToImage(cb, TEXTURE_BUFFER[tmu], node->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, region_count, regions);

        // Prepare the texture for a transfer to an image
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);

        vkEndCommandBuffer(cb);

        VkSubmitInfo submit_info = {
            .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext                = NULL,
            .waitSemaphoreCount   = 0,
            //.pWaitSemaphores      = ,
            //.pWaitDstStageMask    = ,
            .commandBufferCount   = 1,
            .pCommandBuffers      = &cb,
            .signalSemaphoreCount = 0,
            //.pSignalSemaphores    = ,
        };
        CHECK_VULKAN_RESULT(vkQueueSubmit(QUEUE, 1, &submit_info, tnode->fence));
    }

    staging_mesh_set_view(tmu, node->view);
}

FX_ENTRY void FX_CALL grTexCombineFunction(GrChipID_t tmu, GrTextureCombineFnc_t fnc)
{
    LOG(LEVEL_TRACE, "Called with %d:%d\n", tmu, fnc);
    staging_mesh_set_combine_function(tmu, fnc);
}

FX_ENTRY void FX_CALL grTexClampMode(GrChipID_t tmu, GrTextureClampMode_t s, GrTextureClampMode_t t)
{
    LOG(LEVEL_TRACE, "Called with %d, %d\n", s, t);

    VkSamplerAddressMode v = s == GR_TEXTURECLAMP_WRAP ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VkSamplerAddressMode u = t == GR_TEXTURECLAMP_WRAP ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    staging_mesh_set_clamp_mode(tmu, v, u);
}

FX_ENTRY FxU32 FX_CALL grTexMinAddress(GrChipID_t tmu)
{
    LOG(LEVEL_TRACE, "Called\n");
    return 0;
}

FX_ENTRY FxU32 FX_CALL grTexMaxAddress(GrChipID_t tmu)
{
    LOG(LEVEL_TRACE, "Called\n");
    return TEXTURE_MEMORY_SIZE;
}

FX_ENTRY void FX_CALL grTexDownloadMipMap(GrChipID_t tmu, FxU32 start_address, FxU32 even_odd, GrTexInfo* info)
{
    // Even though the spec clearly states that start_address must be 8-byte aligned, some programs simply ignore it
    // so we align the memory in get_aligned_tex_memory()
    // TODO: support even_odd values other than BOTH
    LOG(LEVEL_DEBUG, "Downloading to %d:0x%x; size: %d\n", tmu, start_address, get_tex_memory(even_odd, info));
    LOG(LEVEL_DEBUG, "Called: even_odd: %u, sLOD: %d, lLOD: %d, aspect: %d, format: %d\n", even_odd, info->smallLod, info->largeLod, info->aspectRatio, info->format);

    // TODO: Flush pending renders as a grTexDownloadMipMap() can be called in the middle of rendering

    // Wait for all other frames
    //VkFence fences[MAX_CONCURRENT_FRAMES - 1];
    //for (size_t i = 0, j = 0; i < MAX_CONCURRENT_FRAMES; i++, j++) {
    //    // No need to wait for the current frame's fence
    //    if (i == CURRENT_FRAME) {
    //        j--;
    //        continue;
    //    }
    //    fences[j] = FRAME_INFOS[i].fence;
    //}
    //CHECK_VULKAN_RESULT(vkWaitForFences(DEVICE, MAX_CONCURRENT_FRAMES - 1, fences, VK_TRUE, UINT64_MAX));

    size_t size = get_tex_memory(even_odd, info);
    VkBufferCreateInfo create_info = {
        .sType                 = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext                 = NULL,
        .flags                 = 0,
        .size                  = size,
        .usage                 = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
        //.queueFamilyIndexCount = ,
        //.pQueueFamilyIndices   = ,
    };

    struct TransferNode *node = get_cb();
    vkCreateBuffer(DEVICE, &create_info, NULL, &node->buf);

    VkMemoryRequirements reqs;
    vkGetBufferMemoryRequirements(DEVICE, node->buf, &reqs);

    VkPhysicalDeviceMemoryProperties props;
    vkGetPhysicalDeviceMemoryProperties(PHYSICAL_DEVICE, &props);

    uint32_t i;
    for (i = 0; i < props.memoryTypeCount; i++) {
        if ((reqs.memoryTypeBits & (1 << i)) && (props.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))) {
            break;
        }
    }

    VkMemoryAllocateInfo allocate_info = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext           = NULL,
        .allocationSize  = reqs.size,
        .memoryTypeIndex = i,
    };
    vkAllocateMemory(DEVICE, &allocate_info, NULL, &node->mem);

    vkBindBufferMemory(DEVICE, node->buf, node->mem, 0);

    void* mapped;
    CHECK_VULKAN_RESULT(vkMapMemory(DEVICE, node->mem, 0, size, 0, &mapped));
    memcpy(mapped, info->data, size);
    vkUnmapMemory(DEVICE, node->mem);

    const VkCommandBufferBeginInfo begin_info = {
        .sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext            = NULL,
        .flags            = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = NULL,
    };

    VkCommandBuffer cb = node->cb;
    vkBeginCommandBuffer(cb, &begin_info);

    VkBufferMemoryBarrier barrier = {
        .sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .pNext               = NULL,
        .srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
        //.srcQueueFamilyIndex = ,
        //.dstQueueFamilyIndex = ,
        .buffer              = TEXTURE_BUFFER[tmu],
        .offset              = start_address,
        .size                = size,
    };
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 1, &barrier, 0, NULL);

    VkBufferCopy region = {
        .srcOffset = 0,
        .dstOffset = start_address,
        .size = size,
    };
    vkCmdCopyBuffer(cb, node->buf, TEXTURE_BUFFER[tmu], 1, &region);

    // Prepare the texture for a transfer to an image
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 1, &barrier, 0, NULL);

    vkEndCommandBuffer(cb);

    VkSubmitInfo submit_info = {
        .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext                = NULL,
        .waitSemaphoreCount   = 0,
        //.pWaitSemaphores      = ,
        //.pWaitDstStageMask    = ,
        .commandBufferCount   = 1,
        .pCommandBuffers      = &cb,
        .signalSemaphoreCount = 0,
        //.pSignalSemaphores    = ,
    };
    CHECK_VULKAN_RESULT(vkQueueSubmit(QUEUE, 1, &submit_info, node->fence));

    SHOULD_RESET[tmu] = true;
    staging_mesh_set_view(tmu, VK_NULL_HANDLE);
}

FX_ENTRY FxU32 FX_CALL grTexTextureMemRequired(FxU32 even_odd, GrTexInfo* info)
{
    LOG(LEVEL_TRACE, "Called: even_odd: %u, sLOD: %d, lLOD: %d, aspect: %d, format: %d\n", even_odd, info->smallLod, info->largeLod, info->aspectRatio, info->format);
    FxU32 sum = get_aligned_tex_memory(even_odd, info);
    return sum;
}

FX_ENTRY void FX_CALL grTexMipMapMode(GrChipID_t tmu, GrMipMapMode_t mode, FxBool lodBlend)
{
    LOG(LEVEL_TRACE, "Called\n");
    // Vulkan doesn't support dithered mipmap so we just use linear filtering in that case
    // since it is stated in the Glide spec that it is "almost indistinguishable from trilinear filtering"
    staging_mesh_set_mipmap_enable(tmu, mode != GR_MIPMAP_DISABLE);
    staging_mesh_set_mipmap_mode(tmu, (lodBlend || mode == GR_MIPMAP_NEAREST_DITHER) ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST);
}

int textures_init()
{
    VkBufferCreateInfo create_info = {
        .sType                 = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext                 = NULL,
        .flags                 = 0,
        .size                  = TEXTURE_MEMORY_SIZE,
        .usage                 = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
        //.queueFamilyIndexCount = ,
        //.pQueueFamilyIndices   = ,
    };

    for (size_t tmu = 0; tmu < GLIDE_NUM_TMU; tmu++) {
        CHECK_VULKAN_RESULT(vkCreateBuffer(DEVICE, &create_info, NULL, &TEXTURE_BUFFER[tmu]));
    }

    VkPhysicalDeviceMemoryProperties props;
    vkGetPhysicalDeviceMemoryProperties(PHYSICAL_DEVICE, &props);

    for (size_t tmu = 0; tmu < GLIDE_NUM_TMU; tmu++) {
        VkMemoryRequirements reqs;
        vkGetBufferMemoryRequirements(DEVICE, TEXTURE_BUFFER[tmu], &reqs);

        uint32_t i;
        for (i = 0; i < props.memoryTypeCount; i++) {
            if ((reqs.memoryTypeBits & (1 << i)) && (props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
                break;
            }
        }

        const struct VkMemoryAllocateInfo allocate_info = {
            .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .pNext           = NULL,
            .allocationSize  = reqs.size,
            .memoryTypeIndex = i,
        };

        CHECK_VULKAN_RESULT(vkAllocateMemory(DEVICE, &allocate_info, NULL, &TEXTURE_MEMORY[tmu]));
        CHECK_VULKAN_RESULT(vkBindBufferMemory(DEVICE, TEXTURE_BUFFER[tmu], TEXTURE_MEMORY[tmu], 0));
    }

    CB_RING.nodes = malloc(sizeof(struct TransferNode));
    assert(CB_RING.nodes != NULL);

    {
        VkCommandBufferAllocateInfo allocate_info = {
            .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .pNext              = NULL,
            .commandPool        = POOL,
            .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        vkAllocateCommandBuffers(DEVICE, &allocate_info, &CB_RING.nodes[0].cb);

        VkFenceCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .pNext = NULL,
            .flags = 0,
        };
        vkCreateFence(DEVICE, &create_info, NULL, &CB_RING.nodes[0].fence);

        CB_RING.nodes[0].buf = VK_NULL_HANDLE;
        CB_RING.nodes[0].mem = VK_NULL_HANDLE;
    }

    CB_RING.capacity = 1;
    CB_RING.start = 0;
    CB_RING.len = 0;

    return 0;
}

void textures_terminate(void)
{
    VkCommandBuffer cbs[CB_RING.capacity];
    for (size_t i = 0; i < CB_RING.capacity; i++) {
        vkDestroyBuffer(DEVICE, CB_RING.nodes[i].buf, NULL);
        vkFreeMemory(DEVICE, CB_RING.nodes[i].mem, NULL);
        vkDestroyFence(DEVICE, CB_RING.nodes[i].fence, NULL);
        cbs[i] = CB_RING.nodes[i].cb;
    }

    vkFreeCommandBuffers(DEVICE, POOL, CB_RING.capacity, cbs);
    free(CB_RING.nodes);

    for (size_t tmu = 0; tmu < GLIDE_NUM_TMU; tmu++) {
        texture_avl_free(&TEXTURES[tmu]);
        vkDestroyBuffer(DEVICE, TEXTURE_BUFFER[tmu], NULL);
        vkFreeMemory(DEVICE, TEXTURE_MEMORY[tmu], NULL);
    }
}

static VkFormat gr_format_to_vk_format(GrTextureFormat_t format)
{
    switch (format) {
    case GR_TEXFMT_RGB_565:
        return VK_FORMAT_R5G6B5_UNORM_PACK16;
    case GR_TEXFMT_ARGB_1555:
        return VK_FORMAT_A1R5G5B5_UNORM_PACK16;
    case GR_TEXFMT_ARGB_4444:
        return VK_FORMAT_A4R4G4B4_UNORM_PACK16;
    }

    assert(0);
    return 0;
}

static int32_t get_tex_memory(FxU32 even_odd, const GrTexInfo* info)
{
    uint32_t aspect = info->aspectRatio - 3;
    if (aspect > 3) // aspect was underflowed
        aspect = -aspect;

    int32_t mem = 0;
    for (int32_t i = info->smallLod; i >= info->largeLod; i--) {
        if ((1 << (i % 2)) & even_odd) {
            int32_t wide = 1 << (8-i);
            int32_t slim = wide / (1 << aspect);
            mem += wide * slim;
        }
    }

    if (info->format >= GR_TEXFMT_16BIT)
        mem *= 2;

    return mem;
}

static int32_t get_aligned_tex_memory(FxU32 even_odd, const GrTexInfo* info)
{
    int32_t ret = get_tex_memory(even_odd, info);

    // 8-byte align
    ret = ret + (7 - (ret - 1) % 8);

    return ret;
}

static struct TransferNode *get_cb()
{
    struct TransferNode *node = NULL;

    VkResult res;
    while (CB_RING.len > 0 && (res = vkWaitForFences(DEVICE, 1, &CB_RING.nodes[CB_RING.start].fence, VK_FALSE, 0)) == VK_SUCCESS) {
        node = &CB_RING.nodes[CB_RING.start];
        vkDestroyBuffer(DEVICE, node->buf, NULL);
        node->buf = VK_NULL_HANDLE;
        vkFreeMemory(DEVICE, node->mem, NULL);
        node->mem = VK_NULL_HANDLE;
        vkResetFences(DEVICE, 1, &node->fence);
        CB_RING.start++;
        CB_RING.start %= CB_RING.capacity;
        CB_RING.len--;
    }

    if (node == NULL || res == VK_TIMEOUT) {
        if (CB_RING.len == CB_RING.capacity) {
            CB_RING.nodes = realloc(CB_RING.nodes, CB_RING.capacity * 2 * sizeof(struct TransferNode));
            assert(CB_RING.nodes != NULL);

            memcpy(&CB_RING.nodes[CB_RING.capacity], CB_RING.nodes, CB_RING.start * sizeof(struct TransferNode));

            VkCommandBuffer cbs[CB_RING.capacity];
            VkCommandBufferAllocateInfo allocate_info = {
                .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                .pNext              = NULL,
                .commandPool        = POOL,
                .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                .commandBufferCount = CB_RING.capacity,
            };
            CHECK_VULKAN_RESULT(vkAllocateCommandBuffers(DEVICE, &allocate_info, cbs));

            VkFenceCreateInfo create_info = {
                .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                .pNext = NULL,
                .flags = 0,
            };
            for (size_t i_ = 0; i_ < CB_RING.capacity; i_++) {
                size_t i = (CB_RING.start + CB_RING.len + i_) % (CB_RING.capacity * 2);
                CHECK_VULKAN_RESULT(vkCreateFence(DEVICE, &create_info, NULL, &CB_RING.nodes[i].fence));
                CB_RING.nodes[i].cb = cbs[i_];
                CB_RING.nodes[i].buf = VK_NULL_HANDLE;
                CB_RING.nodes[i].mem = VK_NULL_HANDLE;
            }

            CB_RING.capacity *= 2;
        }

        node = &CB_RING.nodes[(CB_RING.start + CB_RING.len) % CB_RING.capacity];
    } else {
        node = &CB_RING.nodes[CB_RING.start];
    }

    CB_RING.len++;
    return node;
}

static int texture_node_key_cmp(struct TextureNodeKey *t1, struct TextureNodeKey *t2)
{
    if (t1->address != t2->address)
        return (int)t1->address - (int)t2->address;

    if (t1->even_odd != t2->even_odd)
        return (int)t1->even_odd - (int)t2->even_odd;

    if (t1->tiny != t2->tiny)
        return (int)t1->tiny - (int)t2->tiny;

    if (t1->large != t2->large)
        return (int)t1->large - (int)t2->large;

    if (t1->aspect != t2->aspect)
        return (int)t1->aspect - (int)t2->aspect;

    if (t1->format != t2->format)
        return (int)t1->format - (int)t2->format;

    return 0;
}

#define MAX(a, b) ((a) > (b) ? (a) : (b))

static void fix(struct TextureNode *node);

struct TextureNode *texture_avl_insert(struct TextureAVL *avl, struct TextureNodeKey *key)
{
    struct TextureNode *new, *p, *node;
    LOG(LEVEL_TRACE, "Called\n");
    new = malloc(sizeof(struct TextureNode));
    assert(new != NULL);
    new->left = NULL;
    new->right = NULL;
    new->height = 1;
    new->key = *key;
    new->refs = 1;
    new->invalid = false;

    p = NULL;
    node = avl->root;
    while (node != NULL) {
        p = node;
        if (texture_node_key_cmp(&node->key, key) < 0) {
            node = node->right;
        } else {
            node = node->left;
        }
    }

    new->parent = p;

    if (p == NULL) {
        avl->root = new;
        return new;
    } else if (texture_node_key_cmp(&p->key, key) < 0) {
        p->right = new;
    } else { // parent->address > address
        p->left = new;
    }

    // Fixup
    node = new;
    while (node->parent->parent != NULL) {
        struct TextureNode *gp;
        p = node->parent;
        gp = p->parent;
        size_t rheight = p->right == NULL ? 0 : p->right->height;
        size_t lheight = p->left == NULL ? 0 : p->left->height;
        p->height = (rheight > lheight ? rheight : lheight) + 1;
        if (p->left != NULL)
            p->high = MAX(p->high, p->left->high);
        if (p->right != NULL)
            p->high = MAX(p->high, p->right->high);


        int balance = (gp->right == NULL ? 0 : gp->right->height) - (gp->left == NULL ? 0 : gp->left->height);

        if (balance > 1) {
            if (node == p->left) { // RL case
                p->left = node->right;
                fix(p);
                if (p->left != NULL)
                    p->left->parent = p;

                node->right = p;
                fix(node);

                p->parent = node;
                node->parent = gp;
                struct TextureNode *temp = node;
                node = p;
                p = temp;
            }
            // RR case; RL case continuation
            gp->right = p->left;
            fix(gp);
            if (gp->right != NULL)
                gp->right->parent = gp;

            p->left = gp;
            fix(p);

            p->parent = gp->parent;
            gp->parent = p;
            if (p->parent != NULL) {
                if (p->parent->left == gp) {
                    p->parent->left = p;
                } else {
                    p->parent->right = p;
                }
                fix(p->parent);
            } else {
                avl->root = p;
            }
        } else if (balance < -1) {
            if (node == p->right) { // LR case
                p->right = node->left;
                fix(p);
                if (p->right != NULL)
                    p->right->parent = p;

                node->left = p;
                fix(node);

                p->parent = node;
                node->parent = gp;
                struct TextureNode *temp = node;
                node = p;
                p = temp;
            }
            // LL case; LR case continuation
            gp->left = p->right;
            fix(gp);
            if (gp->left != NULL)
                gp->left->parent = gp;

            p->right = gp;
            fix(p);

            p->parent = gp->parent;
            gp->parent = p;
            if (p->parent != NULL) {
                if (p->parent->left == gp) {
                    p->parent->left = p;
                } else {
                    p->parent->right = p;
                }
                fix(p->parent);
            } else {
                avl->root = p;
            }
        } else {
            node = node->parent;
        }
    }

    fix(avl->root);

    return new;
}

static void fix(struct TextureNode *n)
{
    size_t rh = n->right == NULL ? 0 : n->right->height;
    size_t lh = n->left == NULL ? 0 : n->left->height;
    n->height = (rh > lh ? rh : lh) + 1;
    n->high = n->key.address + get_aligned_tex_memory(n->key.even_odd, &(GrTexInfo) {
        .aspectRatio = n->key.aspect,
        .format = n->key.format,
        .smallLod = n->key.tiny,
        .largeLod = n->key.large });
    if (n->left != NULL)
        n->high = MAX(n->high, n->left->high);
    if (n->right != NULL)
        n->high = MAX(n->high, n->right->high);
}

static void texture_avl_free_rec(struct TextureNode *node);

void texture_avl_free(struct TextureAVL *avl)
{
    LOG(LEVEL_TRACE, "Called\n");
    assert(avl != NULL);
    texture_avl_free_rec(avl->root);
    avl->root = NULL;
}

struct TextureNode *texture_avl_get(struct TextureAVL *avl, struct TextureNodeKey *key)
{
    LOG(LEVEL_TRACE, "Called\n");
    struct TextureNode *node = avl->root;
    while (node != NULL) {
        if (texture_node_key_cmp(&node->key, key) == 0) {
            return node;
        } else if (texture_node_key_cmp(&node->key, key) < 0) {
            node = node->right;
        } else {
            node = node->left;
        }
    }

    return NULL;
}

void texture_avl_invalidate(struct TextureAVL *avl, size_t low, size_t high)
{
    struct TextureNode *n = avl->root;
}

#define SAMPLER_CONFIG_OPTION_COUNT 6

VkSampler sampler_get_for_config(struct SamplerConfig *config)
{
    struct ConfigList {
        uint32_t type; // 0 for VkFilter; 1 for bool; 2 for VkSamplerMipmapMode; 3 for VkSamplerAddressMode
        union {
            VkFilter f;
            bool b;
            VkSamplerMipmapMode mmp;
            VkSamplerAddressMode am;
        };
    };

    struct ConfigList list[SAMPLER_CONFIG_OPTION_COUNT];
    list[0].type = 0;
    list[0].f = config->minFilter;
    list[1].type = 0;
    list[1].f = config->magFilter;
    list[2].type = 1;
    list[2].b = config->mipmapEnable;
    list[3].type = 2;
    list[3].mmp = config->mipmapMode;
    list[4].type = 3;
    list[4].am = config->uAddressMode;
    list[5].type = 3;
    list[5].am = config->vAddressMode;

    union SamplerNode *node = &SAMPLER_CACHE_ROOT;
    for (uint32_t i = 0; i < SAMPLER_CONFIG_OPTION_COUNT; i++) {
        size_t child_count;
        uint32_t index;
        switch (list[i].type) {
        case 0:
            child_count = 2;
            index = list[i].f;
        break;
        case 1:
            child_count = 2;
            index = list[i].b;
        break;
        case 2:
            child_count = 2;
            index = list[i].mmp;
        break;
        case 3:
            child_count = 2;
            index = list[i].am == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE ? 1 : 0;
        break;
        }
        if (node->children == NULL) {
            node->children = calloc(child_count, sizeof(struct SamplerNodeOption));
            assert(node->children != NULL);
        }

        if (!node->children[index].occupied) {
            node->children[index].occupied = true;
        }
        node = &node->children[index].node;
    }

    for (size_t i = 0; i < node->count; i++) {
        if (node->biases[i] == config->bias)
            return node->samplers[i];
    }

    node->samplers = realloc(node->samplers, (node->count + 1) * sizeof(VkSampler));
    assert(node->samplers != NULL);
    node->biases = realloc(node->biases, (node->count + 1) * sizeof(float));
    assert(node->biases != NULL);

    LOG(LEVEL_DEBUG, "Creating sampler #%d\n", node->count);
    const VkSamplerCreateInfo create_info = {
        .sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .pNext                   = NULL,
        .flags                   = 0,
        .magFilter               = config->magFilter,
        .minFilter               = config->minFilter,
        .mipmapMode              = config->mipmapMode,
        .addressModeU            = config->uAddressMode,
        .addressModeV            = config->vAddressMode,
        .addressModeW            = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .mipLodBias              = config->bias,
        .anisotropyEnable        = VK_FALSE,
        //.maxAnisotropy           = ,
        .compareEnable           = VK_FALSE,
        //.compareOp               = ,
        .minLod                  = 0.0f,
        .maxLod                  = config->mipmapEnable ? VK_LOD_CLAMP_NONE : 0.0f,
        //.borderColor             = ,
        .unnormalizedCoordinates = VK_FALSE,
    };

    CHECK_VULKAN_RESULT(vkCreateSampler(DEVICE, &create_info, NULL, &node->samplers[node->count]));

    node->biases[node->count] = config->bias;
    node->count++;
    return node->samplers[node->count - 1];
}

static void texture_avl_free_rec(struct TextureNode *node)
{
    LOG(LEVEL_TRACE, "Creating pipeline\n");
    if (node == NULL)
        return;

    struct TextureNode *left = node->left;
    struct TextureNode *right = node->right;

    vkDestroyImageView(DEVICE, node->view, NULL);
    vkDestroyImage(DEVICE, node->image, NULL);
    vkFreeMemory(DEVICE, node->memory, NULL);
    free(node);

    texture_avl_free_rec(left);
    texture_avl_free_rec(right);
}

