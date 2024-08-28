#ifndef DRIFT_PIPELINE_H
#define DRIFT_PIPELINE_H

#include <stdbool.h>

#include "vulkan.h"
#include "glide.h"
#include "mesh.h"

#include "tex.h"

struct PipelineInfo;
int pipeline_info_add_mesh(struct PipelineInfo *cfg, struct MeshInfo *mesh);
struct Mesh *pipeline_info_get_mesh(struct PipelineInfo *cfg);

void staging_pipeline_set_depth_buffer_enable(bool enable);
void staging_pipeline_set_depth_write_enable(bool enable);
void staging_pipeline_set_depth_compare_op(VkCompareOp op);
void staging_pipeline_set_alpha_blend(VkBlendFactor, VkBlendFactor, VkBlendFactor, VkBlendFactor);
void staging_pipeline_set_color_write_enable(bool enable);
void staging_pipeline_set_alpha_write_enable(bool enable);
bool staging_pipeline_changed(void);
struct PipelineConfig *staging_pipeline_get(void);
void staging_pipeline_reset(void);

struct PipelineConfig;

struct Pipeline;

struct PipelineArray;
extern struct PipelineArray *PIPELINES;

int pipeline_array_init(struct PipelineArray *pa);
int pipeline_array_append(struct PipelineArray *pls, struct PipelineConfig *config);
void pipeline_array_reset(struct PipelineArray *pls);
size_t pipeline_array_size(struct PipelineArray *pa);
void pipeline_array_add_triangle(struct PipelineArray *pa);
size_t pipeline_array_mesh_count(struct PipelineArray *pa);
void pipeline_array_get_descriptor_writes(struct PipelineArray *pa, VkDescriptorSet *sets, VkWriteDescriptorSet *writes, VkDescriptorImageInfo *image_infos);
void pipeline_array_get_create_infos(struct PipelineArray *pa, VkGraphicsPipelineCreateInfo *create_infos);
struct PipelineInfo *pipeline_array_get_last(struct PipelineArray *pls);
void pipeline_array_get_all(struct PipelineArray *pa, struct PipelineConfig **configs);
uint32_t pipeline_array_render(struct PipelineArray *array, VkCommandBuffer cb, VkPipelineLayout layout, struct Pipeline **pipelines, uint32_t vertex_count, uint32_t vertex_offset, VkDescriptorSet *sets);

VkPipeline pipeline_get_vk_pipeline(struct Pipeline *pipeline);
void pipeline_unref(struct Pipeline *pipeline);

struct Pipeline *pipeline_get_for_config(struct PipelineConfig *config, VkPipelineLayout layout);
void pipeline_get_for_configs(size_t config_len, struct PipelineConfig *config, VkPipeline *pipelines);

extern VkPipelineShaderStageCreateInfo SHADER_INFO[2];
extern VkPipelineViewportStateCreateInfo VIEWPORT_STATE;

#endif

