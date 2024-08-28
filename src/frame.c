#include "frame.h"

#include <assert.h>
#include <string.h>

#include "buffer.h"
#include "common.h"
#include "drift.h"
#include "mesh.h"
#include "fx.h"
#include "sst.h"
#include "vulkan.h"

typedef float mat4[4][4];

struct Frame {
    VkCommandBuffer cb;
    VkSemaphore imageAcquiredSemaphore;
    VkSemaphore renderFinishedSemaphore;
    VkFence fence;
    size_t pipelineCount;
    struct Pipeline **pipelines;
    size_t poolSize;
    VkDescriptorPool pool;
    VkDescriptorSet *sets;

    size_t vertexCapacity;
    size_t vertexCount;
    VkBuffer vertexBuffer;
    VkDeviceMemory vertexBufferMemory;
    struct DrVertex *vertexBufferData;

    VkImage stagingFrame;
    VkDeviceMemory stagingFrameMemory;

    size_t fogTableCapacity;
    size_t fogTableCount;
    VkBuffer fogTableBuffer;
    VkDeviceMemory fogTableBufferMemory;
    VkDescriptorSet fogTableSet;
    uint32_t *fogTableVertexCounts;
};

struct FramesInfo {
    VkPhysicalDevice physicalDevice;
    VkDevice device;

    VkDescriptorSetLayout projectionMatrixSetLayout;
    VkDescriptorSetLayout colorTextureSetLayout;
    VkDescriptorSetLayout fogTableSetLayout;
    VkPipelineLayout pipelineLayout;

    VkBuffer projectionMatrixBuffer;
    VkDeviceMemory projectionMatrixBufferMemory;
    VkDescriptorSet projectionMatrixSet;

    VkDescriptorPool pool;

    size_t current;
    struct Frame frames[GLIDE_NUM_TMU];
};

struct FramesInfo FRAMES;

void frame_reset(struct Frame *frame)
{
    frame->vertexCount = 0;
}

void frame_present(struct Frame *frame)
{
    const VkPresentInfoKHR present_info = {
        .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .pNext              = NULL,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores    = &frame->renderFinishedSemaphore,
        .swapchainCount     = 1,
        .pSwapchains        = &SWAPCHAIN,
        .pImageIndices      = &CURRENT_BACKBUFFER,
        .pResults           = NULL,
    };
    CHECK_VULKAN_RESULT(vkQueuePresentKHR(QUEUE, &present_info));
}

void frame_lock(struct Frame *frame, void **ptr)
{
    vkDestroyImage(FRAMES.device, frame->stagingFrame, NULL);
    vkFreeMemory(FRAMES.device, frame->stagingFrameMemory, NULL);
    const VkImageCreateInfo create_info = {
        .sType                 = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext                 = NULL,
        .flags                 = 0,
        .imageType             = VK_IMAGE_TYPE_2D,
        .format                = VK_FORMAT_R5G6B5_UNORM_PACK16, // TODO: Pick the format based on mode
        .extent                = { RESOLUTION.width, RESOLUTION.height, 1 },
        .mipLevels             = 1,
        .arrayLayers           = 1,
        .samples               = VK_SAMPLE_COUNT_1_BIT,
        .tiling                = VK_IMAGE_TILING_LINEAR,
        .usage                 = VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
        //.queueFamilyIndexCount = ,
        //.pQueueFamilyIndices   = ,
        .initialLayout         = VK_IMAGE_LAYOUT_PREINITIALIZED,
    };
    CHECK_VULKAN_RESULT(vkCreateImage(FRAMES.device, &create_info, NULL, &frame->stagingFrame));

    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(FRAMES.device, frame->stagingFrame, &requirements);

    VkPhysicalDeviceMemoryProperties props;
    vkGetPhysicalDeviceMemoryProperties(FRAMES.physicalDevice, &props);

    uint32_t index = 0;
    for (uint32_t i = 0; i < props.memoryTypeCount; i++) {
        if ((requirements.memoryTypeBits & (1 << i)) && (props.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))) {
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
    CHECK_VULKAN_RESULT(vkAllocateMemory(DEVICE, &allocate_info, NULL, &frame->stagingFrameMemory));

    CHECK_VULKAN_RESULT(vkBindImageMemory(DEVICE, frame->stagingFrame, frame->stagingFrameMemory, 0));

    CHECK_VULKAN_RESULT(vkMapMemory(DEVICE, frame->stagingFrameMemory, 0, RESOLUTION.width * RESOLUTION.height * 2, 0, ptr));

    frame->fogTableCount = 0;
}

void frame_unlock(struct Frame *frame)
{
    vkUnmapMemory(DEVICE, frame->stagingFrameMemory);
}

void frame_copy(struct Frame *frame)
{
    LOG(LEVEL_TRACE, "Called\n");
    CHECK_VULKAN_RESULT(vkResetCommandBuffer(frame->cb, 0));

    const VkCommandBufferBeginInfo begin_info = {
        .sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext            = NULL,
        .flags            = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = NULL,
    };

    CHECK_VULKAN_RESULT(vkBeginCommandBuffer(frame->cb, &begin_info));

    VkImageMemoryBarrier barrier = {
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext               = NULL,
        .srcAccessMask       = VK_ACCESS_HOST_WRITE_BIT,
        .dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT,
        .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = frame->stagingFrame,
        .subresourceRange    = {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        },
    };
    vkCmdPipelineBarrier(frame->cb, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_DEPENDENCY_BY_REGION_BIT, 0, NULL, 0, NULL, 1, &barrier);

    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.image = SWAPCHAIN_IMAGES[CURRENT_BACKBUFFER];
    vkCmdPipelineBarrier(frame->cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_DEPENDENCY_BY_REGION_BIT, 0, NULL, 0, NULL, 1, &barrier);

    const VkImageBlit blit = {
        .srcSubresource = {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel       = 0,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        },
        .srcOffsets     = {
            { 0, 0, 0 },
            { RESOLUTION.width, RESOLUTION.height, 1 }

        },
        .dstSubresource = {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel       = 0,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        },
        .dstOffsets     = {
            { 0, 0, 0 },
            { SWAPCHAIN_EXTENT.width, SWAPCHAIN_EXTENT.height, 1 }
        },
    };

    vkCmdBlitImage(frame->cb, frame->stagingFrame, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, SWAPCHAIN_IMAGES[CURRENT_BACKBUFFER], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_NEAREST);

    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = 0;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    vkCmdPipelineBarrier(frame->cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_DEPENDENCY_BY_REGION_BIT, 0, NULL, 0, NULL, 1, &barrier);

    CHECK_VULKAN_RESULT(vkEndCommandBuffer(frame->cb));

    const VkSubmitInfo submit_info = {
        .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext                = NULL,
        .waitSemaphoreCount   = 1,
        .pWaitSemaphores      = &frame->imageAcquiredSemaphore,
        .pWaitDstStageMask    = (VkPipelineStageFlags[]) { VK_PIPELINE_STAGE_TRANSFER_BIT },
        .commandBufferCount   = 1,
        .pCommandBuffers      = &frame->cb,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores    = &frame->renderFinishedSemaphore,
    };

    CHECK_VULKAN_RESULT(vkResetFences(DEVICE, 1, &frame->fence));
    CHECK_VULKAN_RESULT(vkQueueSubmit(QUEUE, 1, &submit_info, frame->fence));
}

void frame_skip(struct Frame *frame)
{
    LOG(LEVEL_TRACE, "Called\n");

    if (!SKIP_RENDER) {
        const VkCommandBufferBeginInfo begin_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = NULL,
            .flags = 0,
            .pInheritanceInfo = NULL,
        };
        CHECK_VULKAN_RESULT(vkResetCommandBuffer(frame->cb, 0));
        CHECK_VULKAN_RESULT(vkBeginCommandBuffer(frame->cb, &begin_info));

        if (CURRENT_BACKBUFFER != CURRENT_FRONTBUFFER) {
            VkImageMemoryBarrier barrier[] = {
                {
                    .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                    .pNext               = NULL,
                    .srcAccessMask       = 0,
                    .dstAccessMask       = 0,
                    .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
                    .newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .image               = SWAPCHAIN_IMAGES[CURRENT_BACKBUFFER],
                    .subresourceRange    = {
                        .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                        .baseMipLevel   = 0,
                        .levelCount     = 1,
                        .baseArrayLayer = 0,
                        .layerCount     = 1,
                    },
                },
                {
                    .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                    .pNext               = NULL,
                    .srcAccessMask       = 0,
                    .dstAccessMask       = 0,
                    .oldLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                    .newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .image               = SWAPCHAIN_IMAGES[CURRENT_FRONTBUFFER],
                    .subresourceRange    = {
                        .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                        .baseMipLevel   = 0,
                        .levelCount     = 1,
                        .baseArrayLayer = 0,
                        .layerCount     = 1,
                    },
                }
            };
            vkCmdPipelineBarrier(frame->cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 2, barrier);

            VkImageCopy copy = {
                .srcSubresource = {
                    .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                    .mipLevel       = 0,
                    .baseArrayLayer = 0,
                    .layerCount     = 1,
                },
                .srcOffset      = { 0, 0, 0 },
                .dstSubresource = {
                    .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                    .mipLevel       = 0,
                    .baseArrayLayer = 0,
                    .layerCount     = 1,
                },
                .dstOffset      = { 0, 0, 0 },
                .extent         = { SWAPCHAIN_EXTENT.width, SWAPCHAIN_EXTENT.height, 1 },
            };

            vkCmdCopyImage(frame->cb, SWAPCHAIN_IMAGES[CURRENT_FRONTBUFFER], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, SWAPCHAIN_IMAGES[CURRENT_BACKBUFFER], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

            barrier[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier[0].dstAccessMask = 0;
            barrier[0].oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier[0].newLayout     = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            vkCmdPipelineBarrier(frame->cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0, NULL, 1, barrier);
        }

        CHECK_VULKAN_RESULT(vkEndCommandBuffer(frame->cb));

        VkSubmitInfo submit_info = {
            .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext                = NULL,
            .waitSemaphoreCount   = 1,
            .pWaitSemaphores      = &frame->imageAcquiredSemaphore,
            .pWaitDstStageMask    = (VkPipelineStageFlags[]) { VK_PIPELINE_STAGE_TRANSFER_BIT },
            .commandBufferCount   = 1,
            .pCommandBuffers      = &frame->cb,
            .signalSemaphoreCount = 1,
            .pSignalSemaphores    = &frame->renderFinishedSemaphore,
        };
        CHECK_VULKAN_RESULT(vkResetFences(DEVICE, 1, &frame->fence));
        CHECK_VULKAN_RESULT(vkQueueSubmit(QUEUE, 1, &submit_info, frame->fence));
    } else {
        SKIP_RENDER = false;
    }

    frame_present(frame);

    CHECK_VULKAN_RESULT(vkWaitForFences(FRAMES.device, 1, &frame->fence, VK_TRUE, UINT64_MAX));

    CURRENT_FRONTBUFFER = CURRENT_BACKBUFFER;
    CHECK_VULKAN_RESULT(vkAcquireNextImageKHR(FRAMES.device, SWAPCHAIN, UINT64_MAX, frame->imageAcquiredSemaphore, VK_NULL_HANDLE, &CURRENT_BACKBUFFER));
}

void frame_render(struct Frame *frame, struct PipelineArray *pa)
{
    LOG(LEVEL_TRACE, "Called\n");
    vkUnmapMemory(DEVICE, frame->vertexBufferMemory);

    if (pipeline_array_mesh_count(pa) == 0) {
        if (!SKIP_RENDER) {
            LOG(LEVEL_DEBUG, "Noop\n");
            const VkCommandBufferBeginInfo begin_info = {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                .pNext = NULL,
                .flags = 0,
                .pInheritanceInfo = NULL,
            };
            CHECK_VULKAN_RESULT(vkResetCommandBuffer(frame->cb, 0));
            CHECK_VULKAN_RESULT(vkBeginCommandBuffer(frame->cb, &begin_info));

            VkImageMemoryBarrier barrier = {
                .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .pNext               = NULL,
                .srcAccessMask       = 0,
                .dstAccessMask       = 0,
                .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image               = SWAPCHAIN_IMAGES[CURRENT_BACKBUFFER],
                .subresourceRange    = {
                    .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel   = 0,
                    .levelCount     = 1,
                    .baseArrayLayer = 0,
                    .layerCount     = 1,
                },
            };
            vkCmdPipelineBarrier(frame->cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);

            CHECK_VULKAN_RESULT(vkEndCommandBuffer(frame->cb));

            VkSubmitInfo submit_info = {
                .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                .pNext = NULL,
                .waitSemaphoreCount = 1,
                .pWaitSemaphores = &frame->imageAcquiredSemaphore,
                .pWaitDstStageMask = (VkPipelineStageFlags[]) { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT },
                .commandBufferCount = 1,
                .pCommandBuffers = &frame->cb,
                .signalSemaphoreCount = 1,
                .pSignalSemaphores = &frame->renderFinishedSemaphore,
            };
            CHECK_VULKAN_RESULT(vkResetFences(DEVICE, 1, &frame->fence));
            CHECK_VULKAN_RESULT(vkQueueSubmit(QUEUE, 1, &submit_info, frame->fence));

        } else {
            LOG(LEVEL_DEBUG, "Skipping render\n");
            SKIP_RENDER = false;
        }
    } else {
        if (frame->poolSize < pipeline_array_mesh_count(pa)) {
            VkDescriptorPoolSize sizes[] = {
                {
                    .type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .descriptorCount = pipeline_array_mesh_count(pa) * GLIDE_NUM_TMU,
                },
                {
                    .type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
                    .descriptorCount = pipeline_array_mesh_count(pa),
                },
            };
            const VkDescriptorPoolCreateInfo create_info = {
                .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                .pNext         = NULL,
                .flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
                .maxSets       = pipeline_array_mesh_count(pa),
                .poolSizeCount = sizeof(sizes) / sizeof(sizes[0]),
                .pPoolSizes    = sizes,
            };

            vkDestroyDescriptorPool(DEVICE, frame->pool, NULL);
            CHECK_VULKAN_RESULT(vkCreateDescriptorPool(DEVICE, &create_info, NULL, &frame->pool));
            frame->poolSize = pipeline_array_mesh_count(pa);
        }

        CHECK_VULKAN_RESULT(vkResetDescriptorPool(DEVICE, frame->pool, 0));

        {
            frame->sets = malloc(pipeline_array_mesh_count(pa) * sizeof(VkDescriptorSet));
            assert(frame->sets != NULL);
            VkDescriptorSetLayout *layouts = malloc(pipeline_array_mesh_count(pa) * sizeof(VkDescriptorSetLayout));
            assert(layouts != NULL);
            for (size_t i = 0; i < pipeline_array_mesh_count(pa); i++)
                layouts[i] = FRAMES.colorTextureSetLayout;
            const VkDescriptorSetAllocateInfo allocate_info = {
                .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                .pNext              = NULL,
                .descriptorPool     = frame->pool,
                .descriptorSetCount = pipeline_array_mesh_count(pa),
                .pSetLayouts        = layouts,
            };
            CHECK_VULKAN_RESULT(vkAllocateDescriptorSets(DEVICE, &allocate_info, frame->sets));
            for (size_t i = 0; i < pipeline_array_mesh_count(pa); i++) {
                char name[16];
                snprintf(name, 16, "Texture set #%zu", i);
                vk_name(FRAMES.device, VK_OBJECT_TYPE_DESCRIPTOR_SET, (uint64_t)frame->sets[i], name);
            }
            free(layouts);
        }

        for (size_t i = 0; i < frame->pipelineCount; i++) {
            pipeline_unref(frame->pipelines[i]);
        }
        free(frame->pipelines);

        frame->pipelines = malloc(pipeline_array_size(pa) * sizeof(struct Pipeline *));
        assert(frame->pipelines != NULL);

        struct PipelineConfig **configs = malloc(pipeline_array_size(pa) * sizeof(struct PipelineConfig *));
        assert(configs != NULL);

        pipeline_array_get_all(pa, configs);

        for (size_t i = 0; i < pipeline_array_size(pa); i++)
            frame->pipelines[i] = pipeline_get_for_config(configs[i], FRAMES.pipelineLayout);
        free(configs);

        frame->pipelineCount = pipeline_array_size(pa);

        VkDescriptorImageInfo *image_infos = malloc(pipeline_array_mesh_count(pa) * GLIDE_NUM_TMU * sizeof(VkDescriptorImageInfo));
        assert(image_infos != NULL);
        VkWriteDescriptorSet *writes = malloc(pipeline_array_mesh_count(pa) * sizeof(VkWriteDescriptorSet));
        assert(writes != NULL);

        pipeline_array_get_descriptor_writes(pa, frame->sets, writes, image_infos);
        vkUpdateDescriptorSets(DEVICE, pipeline_array_mesh_count(pa), writes, 0, NULL);
        free(writes);
        free(image_infos);

        CHECK_VULKAN_RESULT(vkResetCommandBuffer(frame->cb, 0));

        const VkCommandBufferBeginInfo begin_info = {
            .sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext            = NULL,
            .flags            = 0,
            .pInheritanceInfo = NULL,
        };
        CHECK_VULKAN_RESULT(vkBeginCommandBuffer(frame->cb, &begin_info));

        const VkRenderPassBeginInfo render_pass_begin_info = {
            .sType        = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .pNext        = NULL,
            .renderPass   = SHOULD_CLEAR ? CLEARING_RENDER_PASS : RENDER_PASS,
            .framebuffer  = FRAMEBUFFERS[CURRENT_BACKBUFFER],
            .renderArea   = {
                .offset = { 0, 0 },
                .extent = SWAPCHAIN_EXTENT,
            },
            .clearValueCount = 3,
            .pClearValues = (VkClearValue[]) {
                [0] = {
                    .color = CLEAR_COLOR_VALUE,
                },
                [2] = {
                    .depthStencil = CLEAR_DEPTH_VALUE
                }
            },
        };
        vkCmdBeginRenderPass(frame->cb, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);
        SHOULD_CLEAR = false;

        vkCmdBindDescriptorSets(frame->cb, VK_PIPELINE_BIND_POINT_GRAPHICS, FRAMES.pipelineLayout, 0, 1, &FRAMES.projectionMatrixSet, 0, NULL);
        vkCmdBindVertexBuffers(frame->cb, 0, 1, &frame->vertexBuffer, (VkDeviceSize[]) { 0 });

        uint32_t vertex_offset = 0;
        for (size_t i = 0; i < frame->fogTableCount; i++) {
            // Luckily, sizeof(struct FogTable) % 256 == 0 so it respects the maximal minimum alignment requirement for dynamic uniform buffers (which is 256)
            vkCmdBindDescriptorSets(frame->cb, VK_PIPELINE_BIND_POINT_GRAPHICS, FRAMES.pipelineLayout, 2, 1, &frame->fogTableSet, 1, (const uint32_t[]) { i * sizeof(struct FogTable) });
            vertex_offset += pipeline_array_render(pa, frame->cb, FRAMES.pipelineLayout, frame->pipelines, frame->fogTableVertexCounts[i], vertex_offset, frame->sets);
            //if (!pipeline_array_render(PIPELINES, frame->cb, frame->fogTableMeshCounts[i], mesh_offset)) {
            //}
        }

        vkCmdEndRenderPass(frame->cb);
        CHECK_VULKAN_RESULT(vkEndCommandBuffer(frame->cb));

        VkSubmitInfo submit_info = {
            .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext                = NULL,
            .waitSemaphoreCount   = 1,
            .pWaitSemaphores      = &frame->imageAcquiredSemaphore,
            .pWaitDstStageMask    = (VkPipelineStageFlags[]) { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT },
            .commandBufferCount   = 1,
            .pCommandBuffers      = &frame->cb,
            .signalSemaphoreCount = 1,
            .pSignalSemaphores    = &frame->renderFinishedSemaphore,
        };
        CHECK_VULKAN_RESULT(vkResetFences(DEVICE, 1, &frame->fence));
        CHECK_VULKAN_RESULT(vkQueueSubmit(QUEUE, 1, &submit_info, frame->fence));
    }
}

void frame_add_vertices(struct Frame *frame, struct DrVertex *v1, struct DrVertex *v2, struct DrVertex *v3)
{
    if (frame->vertexCount + 3 > frame->vertexCapacity) {
        vkUnmapMemory(DEVICE, frame->vertexBufferMemory);

        VkBuffer buf;
        VkDeviceMemory mem;
        const VkBufferCreateInfo create_info = {
            .sType                 = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .pNext                 = NULL,
            .flags                 = 0,
            .size                  = (frame->vertexCapacity + 4096) * sizeof(struct DrVertex),
            .usage                 = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            .sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
            //.queueFamilyIndexCount = ,
            //.pQueueFamilyIndices   = ,
        };
        CHECK_VULKAN_RESULT(vkCreateBuffer(DEVICE, &create_info, NULL, &buf));

        VkMemoryRequirements requirements;
        vkGetBufferMemoryRequirements(DEVICE, buf, &requirements);

        VkPhysicalDeviceMemoryProperties props;
        vkGetPhysicalDeviceMemoryProperties(PHYSICAL_DEVICE, &props);

        uint32_t index = 0;
        for (uint32_t i = 0; i < props.memoryTypeCount; i++) {
            if ((requirements.memoryTypeBits & (1 << i)) && (props.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))) {
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
        CHECK_VULKAN_RESULT(vkAllocateMemory(DEVICE, &allocate_info, NULL, &mem));
        CHECK_VULKAN_RESULT(vkBindBufferMemory(DEVICE, buf, mem, 0));

        if (frame->vertexBufferMemory != VK_NULL_HANDLE) {
            void *new;
            CHECK_VULKAN_RESULT(vkMapMemory(DEVICE, mem, 0, frame->vertexCount * sizeof(struct DrVertex), 0, &new));
            void *old;
            CHECK_VULKAN_RESULT(vkMapMemory(DEVICE, frame->vertexBufferMemory, 0, frame->vertexCount * sizeof(struct DrVertex), 0, &old));
            memcpy(new, old, frame->vertexCount * sizeof(struct DrVertex));
            vkUnmapMemory(DEVICE, frame->vertexBufferMemory);
            vkUnmapMemory(DEVICE, mem);
        }

        vkDestroyBuffer(DEVICE, frame->vertexBuffer, NULL);
        vkFreeMemory(DEVICE, frame->vertexBufferMemory, NULL);
        frame->vertexBuffer         = buf;
        frame->vertexBufferMemory   = mem;
        frame->vertexCapacity += 4096;

        void *mapped;
        CHECK_VULKAN_RESULT(vkMapMemory(DEVICE, frame->vertexBufferMemory, 0, frame->vertexCapacity * sizeof(struct DrVertex),0, &mapped));
        frame->vertexBufferData = mapped;
    }

    // TODO: Let the vertex shader calculate the normalized positions
    frame->vertexBufferData[frame->vertexCount+0] = *v1;
    frame->vertexBufferData[frame->vertexCount+1] = *v2;
    frame->vertexBufferData[frame->vertexCount+2] = *v3;

    frame->vertexCount += 3;
    frame->fogTableVertexCounts[frame->fogTableCount - 1] += 3;
}

int frames_init(VkPhysicalDevice physical_device, VkDevice device, size_t width, size_t height)
{
    const VkCommandBufferAllocateInfo allocate_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = NULL,
        .commandPool = POOL,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = MAX_CONCURRENT_FRAMES,
    };

    VkCommandBuffer cbs[MAX_CONCURRENT_FRAMES];
    CHECK_VULKAN_RESULT(vkAllocateCommandBuffers(device, &allocate_info, cbs));

    const VkFenceCreateInfo fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .pNext = NULL,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };

    const VkSemaphoreCreateInfo semaphore_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
    };

    {
        CHECK_VULKAN_RESULT(vk_create_buffer_and_memory(physical_device, device, sizeof(mat4), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &FRAMES.projectionMatrixBuffer, &FRAMES.projectionMatrixBufferMemory));
        vk_name(device, VK_OBJECT_TYPE_BUFFER, (uint64_t)FRAMES.projectionMatrixBuffer, "Projection matrix buffer");

        void *mapped;
        CHECK_VULKAN_RESULT(vkMapMemory(device, FRAMES.projectionMatrixBufferMemory, 0, VK_WHOLE_SIZE, 0, &mapped));
        mat4 mat;
        mat[0][0] = 2.0 / width ; mat[0][1] = 0            ; mat[0][2] = 0    ; mat[0][3] = 0   ;
        mat[1][0] = 0           ; mat[1][1] = 2.0 / height ; mat[1][2] = 0    ; mat[1][3] = 0   ;
        mat[2][0] = 0.0         ; mat[2][1] = 0.0          ; mat[2][2] = 1.0 ; mat[2][3] = 0.0 ;
        mat[3][0] = -1.0        ; mat[3][1] = -1.0         ; mat[3][2] = 0.0  ; mat[3][3] = 1.0 ;
        memcpy(mapped, mat, sizeof(mat4));
        vkUnmapMemory(device, FRAMES.projectionMatrixBufferMemory);
    }

    {
        VkDescriptorSetLayoutCreateInfo create_info = {
            .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .pNext        = NULL,
            .flags        = 0,
            .bindingCount = 1,
            .pBindings    = (VkDescriptorSetLayoutBinding[]) {
                {
                    .binding            = 0,
                    .descriptorType     = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    .descriptorCount    = 1,
                    .stageFlags         = VK_SHADER_STAGE_VERTEX_BIT,
                    .pImmutableSamplers = NULL,
                },
            },
        };
        CHECK_VULKAN_RESULT(vkCreateDescriptorSetLayout(device, &create_info, NULL, &FRAMES.projectionMatrixSetLayout));

        create_info.pBindings    = (VkDescriptorSetLayoutBinding[]) {
            {
                .binding            = 0,
                    .descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .descriptorCount    = GLIDE_NUM_TMU,
                    .stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT,
                    .pImmutableSamplers = NULL,
            },
        };
        CHECK_VULKAN_RESULT(vkCreateDescriptorSetLayout(device, &create_info, NULL, &FRAMES.colorTextureSetLayout));

        create_info.pBindings    = (VkDescriptorSetLayoutBinding[]) {
            {
                .binding            = 0,
                    .descriptorType     = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
                    .descriptorCount    = 1,
                    .stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT,
                    .pImmutableSamplers = NULL,
            },
        };
        CHECK_VULKAN_RESULT(vkCreateDescriptorSetLayout(device, &create_info, NULL, &FRAMES.fogTableSetLayout));
    }

    {
        VkDescriptorPoolSize sizes[] = {
            {
                .type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = 1,
            },
            {
                .type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
                .descriptorCount = MAX_CONCURRENT_FRAMES,
            },
        };
        VkDescriptorPoolCreateInfo create_info = {
            .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .pNext         = NULL,
            .flags         = 0,
            .maxSets       = 1 + MAX_CONCURRENT_FRAMES,
            .poolSizeCount = sizeof(sizes) / sizeof(sizes[0]),
            .pPoolSizes    = sizes,
        };
        CHECK_VULKAN_RESULT(vkCreateDescriptorPool(DEVICE, &create_info, NULL, &FRAMES.pool));
    }

    {
        VkDescriptorSetLayout layouts[1 + MAX_CONCURRENT_FRAMES] = { FRAMES.projectionMatrixSetLayout };
        for (size_t i = 0; i < MAX_CONCURRENT_FRAMES; i++)
            layouts[i+1] = FRAMES.fogTableSetLayout;
        const VkDescriptorSetAllocateInfo allocate_info = {
            .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .pNext              = NULL,
            .descriptorPool     = FRAMES.pool,
            .descriptorSetCount = sizeof(layouts) / sizeof(layouts[0]),
            .pSetLayouts        = layouts,
        };
        VkDescriptorSet sets[1 + MAX_CONCURRENT_FRAMES];
        vkAllocateDescriptorSets(DEVICE, &allocate_info, sets);
        FRAMES.projectionMatrixSet = sets[0];
        for (size_t i = 0; i < MAX_CONCURRENT_FRAMES; i++) {
            FRAMES.frames[i].fogTableSet = sets[i+1];
        }

        const VkDescriptorBufferInfo info = {
            .buffer = FRAMES.projectionMatrixBuffer,
            .offset = 0,
            .range  = sizeof(mat4),
        };

        const VkWriteDescriptorSet write = {
            .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext            = NULL,
            .dstSet           = FRAMES.projectionMatrixSet,
            .dstBinding       = 0,
            .dstArrayElement  = 0,
            .descriptorCount  = 1,
            .descriptorType   = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            //.pImageInfo       = ,
            .pBufferInfo      = &info,
            //.pTexelBufferView = ,
        };
        vkUpdateDescriptorSets(DEVICE, 1, &write, 0, NULL);
    }
    for (size_t i = 0; i < MAX_CONCURRENT_FRAMES; i++) {
        struct Frame *frame = FRAMES.frames + i;
        frame->cb = cbs[i];

        CHECK_VULKAN_RESULT(vkCreateFence(device, &fence_info, NULL, &frame->fence));

        CHECK_VULKAN_RESULT(vkCreateSemaphore(device, &semaphore_info, NULL, &frame->imageAcquiredSemaphore));
        CHECK_VULKAN_RESULT(vkCreateSemaphore(device, &semaphore_info, NULL, &frame->renderFinishedSemaphore));

        vk_create_buffer_and_memory(physical_device, device, 4096 * sizeof(struct DrVertex), VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &frame->vertexBuffer, &frame->vertexBufferMemory);
        frame->vertexCapacity = 4096;

        vk_create_buffer_and_memory(physical_device, device, sizeof(struct FogTable), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &frame->fogTableBuffer, &frame->fogTableBufferMemory);
        frame->fogTableVertexCounts = malloc(sizeof(uint32_t));
        frame->fogTableCapacity = 1;

        const VkWriteDescriptorSet write = {
            .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext            = NULL,
            .dstSet           = frame->fogTableSet,
            .dstBinding       = 0,
            .dstArrayElement  = 0,
            .descriptorCount  = 1,
            .descriptorType   = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
            //.pImageInfo       = ,
            .pBufferInfo      = (VkDescriptorBufferInfo[]) {
                {
                    .buffer = frame->fogTableBuffer,
                    .offset = 0,
                    .range  = sizeof(struct FogTable),
                }
            },
            //.pTexelBufferView = ,
        };
        vkUpdateDescriptorSets(DEVICE, 1, &write, 0, NULL);
    }

    {
        const VkDescriptorSetLayout layouts[] = { FRAMES.projectionMatrixSetLayout, FRAMES.colorTextureSetLayout, FRAMES.fogTableSetLayout };
        const VkPushConstantRange range = {
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset     = 0,
            .size       = PC_STRUCT_SIZE,
        };
        const VkPipelineLayoutCreateInfo create_info = {
            .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .pNext                  = NULL,
            .flags                  = 0,
            .setLayoutCount         = sizeof(layouts) / sizeof(layouts[0]),
            .pSetLayouts            = layouts,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges    = &range,
        };
        CHECK_VULKAN_RESULT(vkCreatePipelineLayout(device, &create_info, NULL, &FRAMES.pipelineLayout));
    }

    void *mapped;
    CHECK_VULKAN_RESULT(vkMapMemory(device, FRAMES.frames[0].vertexBufferMemory, 0, FRAMES.frames[0].vertexCapacity * sizeof(struct DrVertex),0, &mapped));
    FRAMES.frames[0].vertexBufferData = mapped;

    FRAMES.physicalDevice = physical_device;
    FRAMES.device = device;
    FRAMES.current = 0;
    CHECK_VULKAN_RESULT(vkAcquireNextImageKHR(DEVICE, SWAPCHAIN, UINT64_MAX, FRAMES.frames[0].imageAcquiredSemaphore, VK_NULL_HANDLE, &CURRENT_BACKBUFFER));

    return 0;
}

void frame_add_fog_table(struct Frame *frame, struct FogTable *table)
{
    if (frame->fogTableCount == frame->fogTableCapacity) {
        VkBuffer buf;
        VkDeviceMemory mem;
        CHECK_VULKAN_RESULT(vk_create_buffer_and_memory(FRAMES.physicalDevice, FRAMES.device, (frame->fogTableCapacity * 2) * sizeof(struct FogTable), VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT ,&buf, &mem));
        if (frame->fogTableBufferMemory != VK_NULL_HANDLE) {
            void *new;
            CHECK_VULKAN_RESULT(vkMapMemory(FRAMES.device, mem, 0, frame->fogTableCount * sizeof(struct FogTable), 0, &new));
            void *old;
            CHECK_VULKAN_RESULT(vkMapMemory(FRAMES.device, frame->fogTableBufferMemory, 0, frame->fogTableCount * sizeof(struct FogTable), 0, &old));
            memcpy(new, old, frame->fogTableCount * sizeof(struct FogTable));
            vkUnmapMemory(FRAMES.device, frame->fogTableBufferMemory);
            vkUnmapMemory(FRAMES.device, mem);
        }

        void *temp = realloc(frame->fogTableVertexCounts, frame->fogTableCapacity * 2 * sizeof(uint32_t));
        if (temp == NULL)
            ;

        frame->fogTableVertexCounts = temp;

        vkDestroyBuffer(FRAMES.device, frame->fogTableBuffer, NULL);
        vkFreeMemory(FRAMES.device, frame->fogTableBufferMemory, NULL);
        frame->fogTableBuffer = buf;
        frame->fogTableBufferMemory = mem;
        frame->fogTableCapacity *= 2;

        const VkWriteDescriptorSet write = {
            .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext            = NULL,
            .dstSet           = frame->fogTableSet,
            .dstBinding       = 0,
            .dstArrayElement  = 0,
            .descriptorCount  = 1,
            .descriptorType   = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
            //.pImageInfo       = ,
            .pBufferInfo      = (VkDescriptorBufferInfo[]) {
                {
                    .buffer = frame->fogTableBuffer,
                    .offset = 0,
                    .range  = sizeof(struct FogTable),
                }
            },
            //.pTexelBufferView = ,
        };
        vkUpdateDescriptorSets(FRAMES.device, 1, &write, 0, NULL);
    }

    void *mapped;
    CHECK_VULKAN_RESULT(vkMapMemory(FRAMES.device, frame->fogTableBufferMemory, frame->fogTableCount * sizeof(struct FogTable), sizeof(struct FogTable), 0, &mapped));
    memcpy(mapped, table, sizeof(struct FogTable));
    vkUnmapMemory(FRAMES.device, frame->fogTableBufferMemory);

    frame->fogTableVertexCounts[frame->fogTableCount] = 0;
    frame->fogTableCount++;
}

void frames_terminate(void)
{
    for (size_t i = 0; i < MAX_CONCURRENT_FRAMES; i++) {
        struct Frame *frame = FRAMES.frames + i;
        vkDestroyFence(FRAMES.device, frame->fence, NULL);
        vkDestroyBuffer(FRAMES.device, frame->vertexBuffer, NULL);
        vkFreeMemory(FRAMES.device, frame->vertexBufferMemory, NULL);
        free(frame->pipelines);

        vkDestroyImage(FRAMES.device, frame->stagingFrame, NULL);
        vkFreeMemory(FRAMES.device, frame->stagingFrameMemory, NULL);
        vkDestroyDescriptorPool(FRAMES.device, frame->pool, NULL);
        free(frame->sets);

        vkDestroyBuffer(FRAMES.device, frame->fogTableBuffer, NULL);
        vkFreeMemory(FRAMES.device, frame->fogTableBufferMemory, NULL);
        free(frame->fogTableVertexCounts);

        vkDestroySemaphore(FRAMES.device, frame->imageAcquiredSemaphore, NULL);
        vkDestroySemaphore(FRAMES.device, frame->renderFinishedSemaphore, NULL);
    }

    vkDestroyPipelineLayout(FRAMES.device, FRAMES.pipelineLayout, NULL);
    vkDestroyDescriptorSetLayout(FRAMES.device, FRAMES.projectionMatrixSetLayout, NULL);
    vkDestroyDescriptorSetLayout(FRAMES.device, FRAMES.colorTextureSetLayout, NULL);
    vkDestroyDescriptorSetLayout(FRAMES.device, FRAMES.fogTableSetLayout, NULL);
    vkDestroyBuffer(FRAMES.device, FRAMES.projectionMatrixBuffer, NULL);
    vkFreeMemory(FRAMES.device, FRAMES.projectionMatrixBufferMemory, NULL);
    vkDestroyDescriptorPool(FRAMES.device, FRAMES.pool, NULL);
}

struct Frame *frames_get_current(void)
{
    return &FRAMES.frames[FRAMES.current];
}

void frames_advance()
{
    FRAMES.current++;
    FRAMES.current %= MAX_CONCURRENT_FRAMES;
    struct Frame *frame = FRAMES.frames + FRAMES.current;

    CURRENT_FRONTBUFFER = CURRENT_BACKBUFFER;
    CHECK_VULKAN_RESULT(vkWaitForFences(FRAMES.device, 1, &frame->fence, VK_TRUE, UINT64_MAX));

    CHECK_VULKAN_RESULT(vkAcquireNextImageKHR(FRAMES.device, SWAPCHAIN, UINT64_MAX, frame->imageAcquiredSemaphore, VK_NULL_HANDLE, &CURRENT_BACKBUFFER));

    pipeline_array_reset(PIPELINES);
    staging_pipeline_reset();

    staging_fog_table_reset();
    frame->fogTableCount = 0;

    frame->vertexCount = 0;

    void *mapped;
    CHECK_VULKAN_RESULT(vkMapMemory(FRAMES.device, frame->vertexBufferMemory, 0, frame->vertexCapacity * sizeof(struct DrVertex), 0, &mapped));
    frame->vertexBufferData = mapped;

    free(frame->sets);
    frame->sets = NULL;
}
