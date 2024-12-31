#include "mesh.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "sst.h"
#include "tex.h"

struct PC {
    float constColor[4];
    float chromakeyValue[4];
    float fogColor[4];
    uint32_t textureCombineFunc[GLIDE_NUM_TMU];
    // Specialization constants don't change the layout of a block i.e. the size of the block is
    // determined at compile time and it's set as if the specialization constant takes its default value
    uint32_t padding1__[3 - GLIDE_NUM_TMU];
    uint32_t colorCombineFunc;
    uint32_t colorCombineFactor;
    uint32_t colorCombineLocal;
    uint32_t colorCombineOther;
    uint32_t alphaCombineFunc;
    uint32_t alphaCombineFactor;
    uint32_t alphaCombineLocal;
    uint32_t alphaCombineOther;
    uint32_t alphaTestFunc;
    float alphaTestValue;
    uint32_t chromakeyEnable;
    uint32_t fogMode;
};

size_t PC_STRUCT_SIZE = sizeof(struct PC);

struct MeshInfo {
    //uint32_t vertexCount;
    VkImageView views[GLIDE_NUM_TMU];
    struct SamplerConfig samplers[GLIDE_NUM_TMU];
    uint32_t constColor;
    uint32_t chromakeyValue;
    uint32_t fogColor;
    uint8_t textureCombineFunc[GLIDE_NUM_TMU];
    // Specialization constants don't change the layout of a block i.e. the size of the block is
    // determined at compile time and it's set as if the specialization constant takes its default value
    //uint32_t padding1__[3 - GLIDE_NUM_TMU];
    uint8_t colorCombineFunc;
    uint8_t colorCombineFactor;
    uint8_t colorCombineLocal;
    uint8_t colorCombineOther;
    uint8_t alphaCombineFunc;
    uint8_t alphaCombineFactor;
    uint8_t alphaCombineLocal;
    uint8_t alphaCombineOther;
    uint8_t alphaTestFunc;
    uint8_t alphaTestValue;
    uint8_t chromakeyEnable;
    uint8_t fogMode;
    bool nextFogTable;
};

struct Mesh {
    uint32_t vertexCount;
    VkSampler samplers[GLIDE_NUM_TMU];
    struct MeshInfo info;
};

struct StagingMesh {
    struct MeshInfo current;
    struct MeshInfo next;
    uint32_t count;
};

struct StagingMesh STAGING_MESH;

struct MeshArray {
    size_t vertexCount;
    size_t capacity;
    size_t count;
    struct Mesh meshes[];
};

#define SET_MESH_PROPERTY(prop, value) \
    if (STAGING_MESH.next.prop != STAGING_MESH.current.prop && value == STAGING_MESH.current.prop) { \
        STAGING_MESH.count--; \
    } else if (value != STAGING_MESH.current.prop) { \
        STAGING_MESH.count++; \
    } \
    \
    STAGING_MESH.next.prop = value;


void staging_mesh_set_filter_mode(uint32_t tmu, VkFilter min, VkFilter mag)
{
    SET_MESH_PROPERTY(samplers[tmu].minFilter, min);
    SET_MESH_PROPERTY(samplers[tmu].magFilter, mag);
}

void staging_mesh_set_clamp_mode(uint32_t tmu, VkSamplerAddressMode u, VkSamplerAddressMode v)
{
    SET_MESH_PROPERTY(samplers[tmu].uAddressMode, u);
    SET_MESH_PROPERTY(samplers[tmu].vAddressMode, v);
}

void staging_mesh_set_mipmap_enable(uint32_t tmu, bool enable)
{
    SET_MESH_PROPERTY(samplers[tmu].mipmapEnable, enable);
}

void staging_mesh_set_mipmap_mode(uint32_t tmu, VkSamplerMipmapMode mode)
{
    SET_MESH_PROPERTY(samplers[tmu].mipmapMode, mode);
}

void staging_mesh_set_mipmap_bias(uint32_t tmu, float val)
{
    SET_MESH_PROPERTY(samplers[tmu].bias, val);
}

void staging_mesh_set_view(uint32_t tmu, VkImageView view)
{
    SET_MESH_PROPERTY(views[tmu], view);
}

void staging_mesh_set_combine_function(uint32_t tmu, uint8_t fnc)
{
    SET_MESH_PROPERTY(textureCombineFunc[tmu], fnc);
}

void staging_mesh_set_color_combine(uint8_t function, uint8_t factor, uint8_t local, uint8_t other)
{
    SET_MESH_PROPERTY(colorCombineFunc, function);
    SET_MESH_PROPERTY(colorCombineFactor, factor);
    SET_MESH_PROPERTY(colorCombineLocal, local);
    SET_MESH_PROPERTY(colorCombineOther, other);
}

void staging_mesh_set_alpha_combine(uint8_t function, uint8_t factor, uint8_t local, uint8_t other)
{
    SET_MESH_PROPERTY(alphaCombineFunc, function);
    SET_MESH_PROPERTY(alphaCombineFactor, factor);
    SET_MESH_PROPERTY(alphaCombineLocal, local);
    SET_MESH_PROPERTY(alphaCombineOther, other);
}

void staging_mesh_set_chromakey_enable(bool enable)
{
    SET_MESH_PROPERTY(chromakeyEnable, enable);
}

void staging_mesh_set_alpha_test_reference_value(uint8_t value)
{
    SET_MESH_PROPERTY(alphaTestValue, value);
}

void staging_mesh_set_alpha_test_function(uint8_t function)
{
    SET_MESH_PROPERTY(alphaTestFunc, function);
}

void staging_mesh_set_fog_mode(uint8_t mode)
{
    SET_MESH_PROPERTY(fogMode, mode);
}

void staging_mesh_set_const_color(uint32_t color)
{
    SET_MESH_PROPERTY(constColor, color);
}

void staging_mesh_set_chromakey_value(uint32_t color)
{
    SET_MESH_PROPERTY(chromakeyValue, color);
}

void staging_mesh_set_fog_color(uint32_t color)
{
    SET_MESH_PROPERTY(fogColor, color);
}

bool staging_mesh_changed(void)
{
    return STAGING_MESH.count > 0;
}

struct MeshInfo *staging_mesh_get(void)
{
    LOG(LEVEL_TRACE, "Recreating mesh with views: %llu, %llu\n", STAGING_MESH.next.views[0], STAGING_MESH.next.views[1]);
    STAGING_MESH.current = STAGING_MESH.next;
    STAGING_MESH.count = 0;
    return &STAGING_MESH.current;
}

void staging_mesh_force_change(void)
{
    STAGING_MESH.count++;
}

struct MeshArray *mesh_array_create(void)
{
    struct MeshArray *array = malloc(sizeof(struct MeshArray) + sizeof(struct Mesh));
    if (array == NULL) {
        return NULL;
    }

    array->vertexCount = 0;
    array->capacity = 1;
    array->count = 0;

    return array;
}

int mesh_array_append(struct MeshArray **array, struct MeshInfo *mesh)
{
    struct MeshArray *arr = *array;
    if (arr->count == arr->capacity) {
        void *temp = realloc(*array, sizeof(struct MeshArray) + arr->capacity * 2 * sizeof(struct Mesh));
        if (temp == NULL)
            return -1;

        *array = temp;
        arr = *array;
        arr->capacity *= 2;
    }

    for (size_t i = 0; i < GLIDE_NUM_TMU; i++) {
        arr->meshes[arr->count].samplers[i] = sampler_get_for_config(&mesh->samplers[i]);
    }

    arr->meshes[arr->count].info = *mesh;
    arr->meshes[arr->count].vertexCount = 0;
    arr->count++;

    return 0;
}

size_t mesh_array_size(struct MeshArray *array)
{
    return array->count;
}

void mesh_array_free(struct MeshArray *array)
{
    free(array);
}

void mesh_array_add_vertices(struct MeshArray *array, size_t count)
{
    array->vertexCount += count;
    array->meshes[array->count - 1].vertexCount += count;
}

uint32_t mesh_array_get_descriptor_writes(struct MeshArray *array, VkDescriptorSet *sets, VkWriteDescriptorSet *writes, VkDescriptorImageInfo *image_infos)
{
    uint32_t count = 0;
    for (size_t i = 0; i < array->count; i++) {
        struct Mesh *mesh = array->meshes + i;

        for (uint32_t j = 0; j < GLIDE_NUM_TMU; j++) {
            if (mesh->info.views[j] != VK_NULL_HANDLE) {
                image_infos[count].sampler = mesh->samplers[j];
                image_infos[count].imageView = mesh->info.views[j];
                image_infos[count].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                writes[count].sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[count].pNext            = NULL;
                writes[count].dstSet           = sets[i];
                writes[count].dstBinding       = 0;
                writes[count].dstArrayElement  = j;
                writes[count].descriptorCount  = 1;
                writes[count].descriptorType   = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                writes[count].pImageInfo       = &image_infos[count];
                count++;
            }
        }
    }

    return count;
}

uint32_t mesh_array_render(struct MeshArray *array, VkCommandBuffer cb, VkPipelineLayout layout, VkDescriptorSet *sets, uint32_t vertex_count, uint32_t vertex_offset, uint32_t base_offset)
{
    uint32_t current_mesh = 0;
    for (current_mesh = 0; current_mesh < array->count; current_mesh++) {
        if (vertex_offset - base_offset < array->meshes[current_mesh].vertexCount) {
            break;
        }

        base_offset += array->meshes[current_mesh].vertexCount;
    }

    uint32_t vertices_drawn = 0;

    if (vertex_offset != base_offset) {
        // We are in the middle of a mesh, descriptors have already been bound from a previous call
        if (array->meshes[current_mesh].vertexCount - (vertex_offset - base_offset) > vertex_count) {
            vkCmdDraw(cb, vertex_count, 1, vertex_offset, 0);
            return vertex_count;
        } else {
            vkCmdDraw(cb, array->meshes[current_mesh].vertexCount - (vertex_offset - base_offset), 1, vertex_offset, 0);
            vertices_drawn += array->meshes[current_mesh].vertexCount - (vertex_offset - base_offset);
            current_mesh++;
        }
    }

    for (uint32_t i = current_mesh; i < array->count; i++) {
        struct Mesh *mesh = array->meshes + i;

        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 1, 1, &sets[i], 0, NULL);
        // FIXME: Assuming a ABGR format
        struct PC pc = {
            .constColor = {
                (mesh->info.constColor & 0xFF) / 255.0,
                (mesh->info.constColor >> 8 & 0xFF) / 255.0,
                (mesh->info.constColor >> 16 & 0xFF) / 255.0,
                (mesh->info.constColor >> 24 & 0xFF) / 255.0,
            },
            .chromakeyValue = {
                (mesh->info.chromakeyValue & 0xFF) / 255.0,
                (mesh->info.chromakeyValue >> 8 & 0xFF) / 255.0,
                (mesh->info.chromakeyValue >> 16 & 0xFF) / 255.0,
                (mesh->info.chromakeyValue >> 24 & 0xFF) / 255.0,
            },
            .fogColor = {
                (mesh->info.fogColor & 0xFF) / 255.0,
                (mesh->info.fogColor >> 8 & 0xFF) / 255.0,
                (mesh->info.fogColor >> 16 & 0xFF) / 255.0,
                (mesh->info.fogColor >> 24 & 0xFF) / 255.0,
            },
            .colorCombineFunc = mesh->info.colorCombineFunc,
            .colorCombineFactor = mesh->info.colorCombineFactor,
            .colorCombineLocal = mesh->info.colorCombineLocal,
            .colorCombineOther = mesh->info.colorCombineOther,
            .alphaCombineFunc = mesh->info.alphaCombineFunc,
            .alphaCombineFactor = mesh->info.alphaCombineFactor,
            .alphaCombineLocal = mesh->info.alphaCombineLocal,
            .alphaCombineOther = mesh->info.alphaCombineOther,
            .alphaTestFunc = mesh->info.alphaTestFunc,
            .alphaTestValue = mesh->info.alphaTestValue,
            .chromakeyEnable = mesh->info.chromakeyEnable,
            .fogMode = mesh->info.fogMode,
        };

        for (size_t i = 0; i < GLIDE_NUM_TMU; i++)
            pc.textureCombineFunc[i] = mesh->info.textureCombineFunc[i];
        vkCmdPushConstants(cb, layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(struct PC), &pc);

        if (vertex_count - vertices_drawn < mesh->vertexCount) {
            vkCmdDraw(cb, vertex_count - vertices_drawn, 1, vertex_offset + vertices_drawn, 0);
            return vertex_count;
        } else {
            vkCmdDraw(cb, mesh->vertexCount, 1, vertex_offset + vertices_drawn, 0);
            vertices_drawn += mesh->vertexCount;
            if (vertex_count == vertices_drawn)
                return vertex_count;

        }
    }

    return vertices_drawn;
}

uint32_t mesh_array_vertex_count(struct MeshArray *array)
{
    return array->vertexCount;
}

