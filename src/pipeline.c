#include "pipeline.h"

#include <assert.h>

#include "common.h"
#include "sst.h"
#include "frame.h"

struct PipelineConfig {
    VkBool32 depthBiasEnable;
    VkBool32 depthTestEnable;
    VkBool32 depthWriteEnable;
    VkCompareOp depthCompareOp;
    VkBlendFactor srcColorBlendFactor;
    VkBlendFactor dstColorBlendFactor;
    VkBlendFactor srcAlphaBlendFactor;
    VkBlendFactor dstAlphaBlendFactor;
    VkBool32 colorWriteEnable;
    VkBool32 alphaWriteEnable;
};

struct StagingPipeline {
    struct PipelineConfig current;
    struct PipelineConfig next;
    uint32_t count;
};

struct StagingPipeline STAGING_PIPELINE = {
    .next = {
        .colorWriteEnable = VK_TRUE,
        .alphaWriteEnable = VK_TRUE,
    },
    .count = 1
};

struct PipelineInfo {
    struct MeshArray *meshes;
    struct PipelineConfig config;
};

struct PipelineArray {
    size_t capacity;
    size_t count;
    struct PipelineInfo *infos;
};

struct PipelineArray PIPELINES_;
struct PipelineArray *PIPELINES = &PIPELINES_;

static const VkSpecializationInfo SPECIALIZATION_INFO = {
    .mapEntryCount = 1,
    .pMapEntries   = (VkSpecializationMapEntry[]) {
        {
            .constantID = 0,
            .offset     = 0,
            .size       = 4,
        }
    },
    .dataSize      = 4,
    .pData         = (uint32_t[]) { GLIDE_NUM_TMU },
};

VkPipelineShaderStageCreateInfo SHADER_INFO[2] = {
    {
        .sType               = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .pNext               = NULL,
        .flags               = 0,
        .stage               = VK_SHADER_STAGE_VERTEX_BIT,
        //.module              = , // Should be initialized in grSstWinOpen
        .pName               = "main",
        .pSpecializationInfo = &SPECIALIZATION_INFO,
    },
    {
        .sType               = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .pNext               = NULL,
        .flags               = 0,
        .stage               = VK_SHADER_STAGE_FRAGMENT_BIT,
        //.module              = , // Should be initialized in grSstWinOpen
        .pName               = "main",
        .pSpecializationInfo = &SPECIALIZATION_INFO,
    }
};

static const VkPipelineVertexInputStateCreateInfo INPUT_STATE = {
    .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    .pNext                           = NULL,
    .flags                           = 0,
    .vertexBindingDescriptionCount   = 1,
    .pVertexBindingDescriptions      = (VkVertexInputBindingDescription[]) {
        {
            .binding   = 0,
            .stride    = sizeof(struct DrVertex),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
        },
        //{
        //    .binding = 1,
        //    .stride = sizeof(struct DrInstance),
        //    .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE,
        //}
    },

    .vertexAttributeDescriptionCount = 5,
    .pVertexAttributeDescriptions    = (VkVertexInputAttributeDescription[]) {
        // coord
        {
            .location = 0,
            .binding  = 0,
            .format   = VK_FORMAT_R32G32B32A32_SFLOAT,
            .offset   = offsetof(struct DrVertex, coord),
        },
        // color
        {
            .location = 1,
            .binding  = 0,
            .format   = VK_FORMAT_R32G32B32A32_SFLOAT,
            .offset   = offsetof(struct DrVertex, color),
        },
        // st0
        {
            .location = 2,
            .binding = 0,
            .format = VK_FORMAT_R32G32_SFLOAT,
            .offset = offsetof(struct DrVertex, st0),
        },
        // st1
        {
            .location = 3,
            .binding = 0,
            .format = VK_FORMAT_R32G32_SFLOAT,
            .offset = offsetof(struct DrVertex, st1),
        },
        // st2
        {
            .location = 4,
            .binding = 0,
            .format = VK_FORMAT_R32G32_SFLOAT,
            .offset = offsetof(struct DrVertex, st2),
        },
    },
};

static const VkPipelineInputAssemblyStateCreateInfo ASSEMBLY_STATE = {
    .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
    .pNext                  = NULL,
    .flags                  = 0,
    .topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    .primitiveRestartEnable = VK_FALSE,
};

VkPipelineViewportStateCreateInfo VIEWPORT_STATE = {
    .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
    .pNext         = NULL,
    .flags         = 0,
    .viewportCount = 1,
    .pViewports    = NULL,
    .scissorCount  = 1,
    .pScissors     = NULL,
};

static const VkPipelineMultisampleStateCreateInfo MULTISAMPLE_STATE = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
    .pNext = NULL,
    .flags = 0,
    .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    .sampleShadingEnable = VK_FALSE,
    .minSampleShading = 0.0f,
    .pSampleMask = NULL,
    .alphaToCoverageEnable = VK_FALSE,
    .alphaToOneEnable = VK_FALSE,
};

struct PipelineNode;

struct Pipeline {
    struct PipelineNode *parent;
    size_t refCount;
    VkPipeline pipeline;
};

struct PipelineNode {
    struct PipelineNode *parent;
    union {
        struct {
            size_t count;
            struct PipelineNode *children;
        };
        struct Pipeline *leaf;
    };
};

struct PipelineNode PIPELINE_CACHE_ROOT = {
    .parent = NULL,
    .count = 0,
    .children = NULL
};

// The 1 is so that the first call to grDraw() will build a new pipeline

FX_ENTRY void FX_CALL grColorMask(FxBool rgb, FxBool a)
{
    LOG(LEVEL_TRACE, "Called\n");
    staging_pipeline_set_color_write_enable(rgb);
    staging_pipeline_set_alpha_write_enable(a);
}

FX_ENTRY void FX_CALL grDepthBufferMode(GrDepthBufferMode_t mode)
{
    LOG(LEVEL_TRACE, "Setting to %d\n", mode);

    staging_pipeline_set_depth_buffer_enable(mode == GR_DEPTHBUFFER_WBUFFER);
}

FX_ENTRY void FX_CALL grDepthMask(FxBool enable)
{
    LOG(LEVEL_TRACE, "Enabling depth write: %d\n", enable);
    staging_pipeline_set_depth_write_enable(enable != 0 ? true : false);
}

FX_ENTRY void FX_CALL grDepthBufferFunction(GrCmpFnc_t function)
{
    LOG(LEVEL_INFO, "Setting to %d\n", function);
    staging_pipeline_set_depth_compare_op(function); // GrCmpFnc_t and VkCompareOp values are overlapping
}

FX_ENTRY void FX_CALL grColorCombine(GrCombineFunction_t function, GrCombineFactor_t factor, GrCombineLocal_t local, GrCombineOther_t other, FxBool invert)
{
    //LOG(LEVEL_DEBUG, "Called\n");

    staging_mesh_set_color_combine(function, factor, local, other);
}

FX_ENTRY void FX_CALL grAlphaCombine(GrCombineFunction_t function, GrCombineFactor_t factor, GrCombineLocal_t local, GrCombineOther_t other, FxBool invert)
{
    staging_mesh_set_alpha_combine(function, factor, local, other);
}

static VkBlendFactor gr_fac_to_vk_fac(bool is_src, GrAlphaBlendFnc_t fac);

FX_ENTRY void FX_CALL grAlphaBlendFunction(GrAlphaBlendFnc_t rgb_sf, GrAlphaBlendFnc_t rgb_df, GrAlphaBlendFnc_t alpha_sf, GrAlphaBlendFnc_t alpha_df)
{
    LOG(LEVEL_TRACE, "Called\n");

    staging_pipeline_set_alpha_blend(gr_fac_to_vk_fac(true, rgb_sf), gr_fac_to_vk_fac(false, rgb_df), gr_fac_to_vk_fac(true, alpha_sf), gr_fac_to_vk_fac(false, alpha_df));
}

int pipeline_info_add_mesh(struct PipelineInfo *info, struct MeshInfo *mesh)
{
    return mesh_array_append(&info->meshes, mesh);
}

#define SET_PIPELINE_PROPERTY(prop, value) \
    if (STAGING_PIPELINE.next.prop != STAGING_PIPELINE.current.prop && value == STAGING_PIPELINE.current.prop) { \
        STAGING_PIPELINE.count--; \
    } else if (value != STAGING_PIPELINE.current.prop) { \
        STAGING_PIPELINE.count++; \
    } \
    \
    STAGING_PIPELINE.next.prop = value;

void staging_pipeline_set_depth_buffer_enable(bool enable)
{
    VkBool32 e = enable ? VK_TRUE : VK_FALSE;
    SET_PIPELINE_PROPERTY(depthTestEnable, e);
}

void staging_pipeline_set_depth_write_enable(bool enable)
{
    VkBool32 e = enable ? VK_TRUE : VK_FALSE;
    SET_PIPELINE_PROPERTY(depthWriteEnable, e);
}

void staging_pipeline_set_depth_compare_op(VkCompareOp op)
{
    SET_PIPELINE_PROPERTY(depthCompareOp, op);
}

void staging_pipeline_set_alpha_blend(VkBlendFactor src_color, VkBlendFactor dst_color, VkBlendFactor src_alpha, VkBlendFactor dst_alpha)
{
    SET_PIPELINE_PROPERTY(srcColorBlendFactor, src_color);
    SET_PIPELINE_PROPERTY(dstColorBlendFactor, dst_color);
    SET_PIPELINE_PROPERTY(srcAlphaBlendFactor, src_alpha);
    SET_PIPELINE_PROPERTY(dstAlphaBlendFactor, dst_alpha);
}

void staging_pipeline_set_color_write_enable(bool enable)
{
    VkBool32 e = enable ? VK_TRUE : VK_FALSE;
    SET_PIPELINE_PROPERTY(colorWriteEnable, e);
}

void staging_pipeline_set_alpha_write_enable(bool enable)
{
    VkBool32 e = enable ? VK_TRUE : VK_FALSE;
    SET_PIPELINE_PROPERTY(alphaWriteEnable, e);
}

bool staging_pipeline_changed()
{
    return STAGING_PIPELINE.count > 0;
}

int pipeline_array_init(struct PipelineArray *pa)
{
    pa->infos = malloc(sizeof(struct PipelineInfo));
    if (pa->infos == NULL) {
        return -1;
    }

    pa->capacity = 1;
    pa->count = 0;

    return 0;
}

struct PipelineInfo *pipeline_array_get_last(struct PipelineArray *pls)
{
    return &pls->infos[pls->count - 1];
}

void pipeline_array_get_all(struct PipelineArray *pa, struct PipelineConfig **configs)
{
    for (size_t i = 0; i < pa->count; i++)
        configs[i] = &pa->infos[i].config;
}

struct PipelineConfig *staging_pipeline_get(void)
{
    STAGING_PIPELINE.current = STAGING_PIPELINE.next;
    STAGING_PIPELINE.count = 0;
    return &STAGING_PIPELINE.current;
}

void staging_pipeline_force_change(void)
{
    // This is a trick to get the next call to staging_pipeline_changed() return true
    // We could copy sp->next to sp->current and set sp->count to 1 but that would be a lot slower
    // If we would have just set this to 1 and not copy sp->next to sp->current, consider the following
    // grDepthMask(DISABLE) // Changes the pipeline from WBUFFER (current is WBUFFER, next is DISABLE); increases sp->count to 1
    // ~No draw calls happen here~
    // grBufferSwap() // Would have set sp->count to 1
    // grDepthMask(WBUFFER) // Changes the pipeline back to WBUFFER; decreases sp->count to 0 (since current is still WBUFFER)
    // grDrawTriangle() // Pipeline should be recreated at this point since this is a brand new frame but it won't since sp->count is 0
    STAGING_PIPELINE.count++;
}

int pipeline_array_append(struct PipelineArray *pa, struct PipelineConfig *config)
{
    if (pa->count == pa->capacity) {
        void *temp = realloc(pa->infos, pa->capacity * 2 * sizeof(struct PipelineInfo));
        if (temp == NULL)
            return -1;

        pa->infos = temp;
        pa->capacity *= 2;
    }

    pa->infos[pa->count].meshes = mesh_array_create();
    if (pa->infos[pa->count].meshes == NULL) {
        return -1;
    }

    pa->infos[pa->count].config = *config;
    pa->count++;
    return 0;
}

void pipeline_array_reset(struct PipelineArray *pa)
{
    for (size_t i = 0; i < pa->count; i++)
        mesh_array_free(pa->infos[i].meshes);
    pa->count = 0;
}

size_t pipeline_array_size(struct PipelineArray *pa)
{
    return pa->count;
}

void pipeline_array_add_triangle(struct PipelineArray *pa)
{
    mesh_array_add_vertices(pa->infos[pa->count - 1].meshes, 3);
}

size_t pipeline_array_mesh_count(struct PipelineArray *pa)
{
    size_t count = 0;
    for (size_t i = 0; i < pa->count; i++)
        count += mesh_array_size(pa->infos[i].meshes);

    return count;
}

void pipeline_array_get_descriptor_writes(struct PipelineArray *pa, VkDescriptorSet *sets, VkWriteDescriptorSet *writes, VkDescriptorImageInfo *image_infos)
{
    uint32_t current_mesh = 0;
    for (size_t i = 0; i < pa->count; i++) {
        struct PipelineInfo *pipeline = pa->infos + i;
        mesh_array_get_descriptor_writes(pipeline->meshes, sets + current_mesh, &writes[current_mesh], &image_infos[current_mesh * GLIDE_NUM_TMU]);
        current_mesh += mesh_array_size(pipeline->meshes);
    }
}

uint32_t pipeline_array_render(struct PipelineArray *array, VkCommandBuffer cb, VkPipelineLayout layout, struct Pipeline **pipelines, uint32_t vertex_count, uint32_t vertex_offset, VkDescriptorSet *sets)
{
    uint32_t vertices_drawn = 0;
    uint32_t current_pipeline;
    uint32_t temp = vertex_offset;
    uint32_t base_offset = 0;
    uint32_t mesh_offset = 0;
    for (current_pipeline = 0; current_pipeline < array->count; current_pipeline++) {
        if (temp < mesh_array_vertex_count(array->infos[current_pipeline].meshes)) {
            break;
        }

        base_offset += mesh_array_vertex_count(array->infos[current_pipeline].meshes);
        temp -= mesh_array_vertex_count(array->infos[current_pipeline].meshes);
        mesh_offset += mesh_array_size(array->infos[current_pipeline].meshes);
    }

    for (uint32_t i = current_pipeline; i < array->count; i++) {
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_get_vk_pipeline(pipelines[i]));
        struct PipelineInfo *pipeline = array->infos + i;
        vertices_drawn += mesh_array_render(pipeline->meshes, cb, layout, &sets[mesh_offset], vertex_count - vertices_drawn, vertex_offset + vertices_drawn, base_offset);
        if (vertices_drawn == vertex_count)
            break;
        base_offset += mesh_array_vertex_count(pipeline->meshes);
        mesh_offset += mesh_array_size(array->infos[i].meshes);
    }

    return vertices_drawn;
}

#define PIPELINE_CONFIG_OPTION_COUNT 10

struct Pipeline *pipeline_get_for_config(struct PipelineConfig *config, VkPipelineLayout layout)
{
    struct ConfigList {
        uint32_t type; // 0 for VkBool32; 1 for VkCompareOp; 2 for VkBlendFactor
        union {
            VkBool32 b;
            VkCompareOp op;
            VkBlendFactor f;
        };
    };

    struct ConfigList list[PIPELINE_CONFIG_OPTION_COUNT];
    list[0].type = 0;
    list[0].b = config->depthBiasEnable;
    list[1].type = 0;
    list[1].b = config->depthTestEnable;
    list[2].type = 0;
    list[2].b = config->depthWriteEnable;
    list[3].type = 1;
    list[3].op = config->depthCompareOp;
    list[4].type = 2;
    list[4].f = config->srcColorBlendFactor;
    list[5].type = 2;
    list[5].f = config->dstColorBlendFactor;
    list[6].type = 2;
    list[6].f = config->srcAlphaBlendFactor;
    list[7].type = 2;
    list[7].f = config->dstAlphaBlendFactor;
    list[8].type = 0;
    list[8].b = config->colorWriteEnable;
    list[9].type = 0;
    list[9].b = config->alphaWriteEnable;

    struct PipelineNode *parent = NULL;
    struct PipelineNode *node = &PIPELINE_CACHE_ROOT;
    for (uint32_t i = 0; i < PIPELINE_CONFIG_OPTION_COUNT; i++) {
        size_t child_count;
        uint32_t index;
        switch (list[i].type) {
        case 0:
            child_count = 2;
            index = list[i].b ? 1 : 0;
        break;
        case 1:
            child_count = 8;
            index = list[i].op;
        break;
        case 2:
            child_count = 11;
            index = list[i].f == VK_BLEND_FACTOR_SRC_ALPHA_SATURATE ? 10 : list[i].f;
        break;
        }
        if (node->children == NULL) {
            node->children = malloc(child_count * sizeof(struct PipelineNode));
            assert(node->children != NULL);
            if (parent != NULL)
                parent->count++;
            if (i < PIPELINE_CONFIG_OPTION_COUNT - 1) {
                for (size_t i = 0; i < child_count; i++) {
                    node->children[i].parent = node;
                    node->children[i].count = 0;
                    node->children[i].children = NULL;
                }
            } else {
                for (size_t i = 0; i < child_count; i++) {
                    node->children[i].parent = node;
                    node->children[i].leaf = NULL;
                }
            }
        }

        parent = node;
        node = &node->children[index];
    }

    if (node->leaf == NULL) {
        LOG(LEVEL_DEBUG, "Creating pipeline\n");
        node->leaf = malloc(sizeof(struct Pipeline));
        node->leaf->parent = parent;
        node->leaf->refCount = 0;
        assert(node->leaf != NULL);

        VkPipelineRasterizationStateCreateInfo rasterization_state = {
            .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .pNext                   = NULL,
            .flags                   = 0,
            .depthClampEnable        = VK_FALSE,
            .rasterizerDiscardEnable = VK_FALSE,
            .polygonMode             = VK_POLYGON_MODE_FILL,
            .cullMode                = VK_CULL_MODE_NONE,
            .frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE,
            .depthBiasEnable         = VK_FALSE,
            //.depthBiasConstantFactor = ,
            //.depthBiasClamp          = ,
            //.depthBiasSlopeFactor    = ,
            .lineWidth               = 1.0f,
        };

        const VkPipelineDepthStencilStateCreateInfo depth_state = {
            .sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .pNext                 = NULL,
            .flags                 = 0,
            .depthTestEnable       = config->depthTestEnable,
            .depthWriteEnable      = config->depthWriteEnable,
            .depthCompareOp        = config->depthCompareOp,
            .depthBoundsTestEnable = VK_FALSE,
            .stencilTestEnable     = VK_FALSE,
            //.front                 = ,
            //.back                  = ,
            //.minDepthBounds        = ,
            //.maxDepthBounds        = ,
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
                    .blendEnable         = VK_TRUE,
                    .srcColorBlendFactor = config->srcColorBlendFactor,
                    .dstColorBlendFactor = config->dstColorBlendFactor,
                    .colorBlendOp        = VK_BLEND_OP_ADD,
                    .srcAlphaBlendFactor = config->srcAlphaBlendFactor,
                    .dstAlphaBlendFactor = config->dstAlphaBlendFactor,
                    .alphaBlendOp        = VK_BLEND_OP_ADD,
                    .colorWriteMask      = (config->colorWriteEnable ? VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT : 0) | (config->alphaWriteEnable ? VK_COLOR_COMPONENT_A_BIT : 0),
                }
            },
            //.blendConstants  = ,
        };

        const VkGraphicsPipelineCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .pNext = NULL,
            .flags = 0,
            .stageCount = 2,
            .pStages = SHADER_INFO,
            .pVertexInputState = &INPUT_STATE,
            .pInputAssemblyState = &ASSEMBLY_STATE,
            .pTessellationState = NULL,
            .pViewportState = &VIEWPORT_STATE,
            .pRasterizationState = &rasterization_state,
            .pMultisampleState = &MULTISAMPLE_STATE,
            .pDepthStencilState = &depth_state,
            .pColorBlendState = &color_blend_state,
            .pDynamicState = NULL,
            .layout = layout,
            .renderPass = RENDER_PASS,
            .subpass = 0,
            .basePipelineHandle = VK_NULL_HANDLE,
            .basePipelineIndex = -1,
        };

        CHECK_VULKAN_RESULT(vkCreateGraphicsPipelines(DEVICE, VK_NULL_HANDLE, 1, &create_info, NULL, &node->leaf->pipeline));
    }

    node->leaf->refCount++;

    return node->leaf;
}

void pipeline_get_for_configs(size_t config_len, struct PipelineConfig *config, VkPipeline *pipelines)
{
}

VkPipeline pipeline_get_vk_pipeline(struct Pipeline *pipeline)
{
    return pipeline->pipeline;
}

void pipeline_unref(struct Pipeline *pipeline)
{
    pipeline->refCount--;
    if (pipeline->refCount == 0) {
        vkDestroyPipeline(DEVICE, pipeline->pipeline, NULL);
        pipeline->parent->leaf = NULL;
        LOG(LEVEL_DEBUG, "Destroying pipeline\n");
        struct PipelineNode *node = pipeline->parent->parent;
        while (node != NULL) {
            node->count--;
            if (node->count != 0)
                break;
            free(node->children);
            node->children = NULL;
            node = node->parent;
        }
        free(pipeline);
    }
}

static VkBlendFactor gr_fac_to_vk_fac(bool is_src, GrAlphaBlendFnc_t fac)
{
    switch (fac) {
    case GR_BLEND_ZERO:
        return VK_BLEND_FACTOR_ZERO;
    case GR_BLEND_SRC_ALPHA:
        return VK_BLEND_FACTOR_SRC_ALPHA;
    case GR_BLEND_SRC_COLOR: // || GR_BLEND_DST_COLOR
        return is_src ? VK_BLEND_FACTOR_SRC_COLOR : VK_BLEND_FACTOR_DST_COLOR;
    case GR_BLEND_DST_ALPHA:
        return VK_BLEND_FACTOR_DST_ALPHA;
    case GR_BLEND_ONE:
        return VK_BLEND_FACTOR_ONE;
    case GR_BLEND_ONE_MINUS_SRC_ALPHA:
        return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case GR_BLEND_ONE_MINUS_SRC_COLOR: // GR_BLEND_ONE_MINUS_DST_COLOR
        return is_src ? VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR : VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    case GR_BLEND_ONE_MINUS_DST_ALPHA:
        return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    case GR_BLEND_ALPHA_SATURATE:
        return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
    default:
        assert(0);
        return 0;
    }
}

