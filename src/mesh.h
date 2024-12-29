#ifndef DRIFT_MESH_H
#define DRIFT_MESH_H

#include <stdbool.h>
#include <stdint.h>

#include "vulkan.h"

extern size_t PC_STRUCT_SIZE;

struct MeshInfo;

void staging_mesh_set_filter_mode(uint32_t tmu, VkFilter min, VkFilter max);
void staging_mesh_set_clamp_mode(uint32_t tmu, VkSamplerAddressMode u, VkSamplerAddressMode v);
void staging_mesh_set_mipmap_enable(uint32_t tmu, bool enable);
void staging_mesh_set_mipmap_mode(uint32_t tmu, VkSamplerMipmapMode mode);
void staging_mesh_set_view(uint32_t tmu, VkImageView view);
void staging_mesh_set_combine_function(uint32_t tmu, uint8_t fnc);
void staging_mesh_set_color_combine(uint8_t function, uint8_t factor, uint8_t local, uint8_t other);
void staging_mesh_set_alpha_combine(uint8_t function, uint8_t factor, uint8_t local, uint8_t other);
void staging_mesh_set_chromakey_enable(bool enable);
void staging_mesh_set_alpha_test_reference_value(uint8_t value);
void staging_mesh_set_alpha_test_function(uint8_t function);
void staging_mesh_set_fog_mode(uint8_t mode);
void staging_mesh_set_const_color(uint32_t color);
void staging_mesh_set_chromakey_value(uint32_t color);
void staging_mesh_set_fog_color(uint32_t mode);
bool staging_mesh_changed();
struct MeshInfo *staging_mesh_get();

struct MeshGoup;
struct MeshGroupArray;
struct MeshGroupArray *mesh_group_array_create();
void mesh_group_array_append(struct MeshGroupArray *array);
void mesh_group_array_add_mesh(struct MeshGroupArray *array, struct MeshInfo *info);
void mesh_group_array_free(struct MeshGroupArray *array);

struct Mesh;
struct MeshArray;
struct MeshArray *mesh_array_create();
int mesh_array_append(struct MeshArray **array, struct MeshInfo *mesh);
size_t mesh_array_size(struct MeshArray *array);
void mesh_array_free(struct MeshArray *array);
void mesh_array_add_vertices(struct MeshArray *array, size_t count);
struct Mesh *mesh_array_get(struct MeshArray *array);
uint32_t mesh_array_get_descriptor_writes(struct MeshArray *array, VkDescriptorSet *sets, VkWriteDescriptorSet *writes, VkDescriptorImageInfo *image_infos);
uint32_t mesh_array_render(struct MeshArray *array, VkCommandBuffer cb, VkPipelineLayout layout, VkDescriptorSet *sets, uint32_t vertex_count, uint32_t vertex_offset, uint32_t base_offset);
uint32_t mesh_array_vertex_count(struct MeshArray *array);

#endif

