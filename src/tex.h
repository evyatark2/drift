#ifndef DRIFT_TEX_H
#define DRIFT_TEX_H

#include <stdbool.h>

#include "vulkan.h"
#include "glide.h"
#include "fx.h"

struct SamplerConfig {
    VkFilter minFilter;
    VkFilter magFilter;
    bool mipmapEnable;
    VkSamplerMipmapMode mipmapMode;
    VkSamplerAddressMode uAddressMode;
    VkSamplerAddressMode vAddressMode;
};

//struct SamplerConfig;
VkSampler sampler_get_for_config(struct SamplerConfig *config);

struct TextureNodeKey {
    uint32_t address;
    uint8_t even_odd;
    uint8_t tiny;
    uint8_t large;
    uint8_t aspect;
    uint8_t format;
};

struct TextureNode {
    struct TextureNode *parent;
    struct TextureNode *left;
    struct TextureNode *right;
    struct TextureNodeKey key;
    size_t height;
    size_t high;
    size_t refs;
    bool invalid;
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
};

struct TextureAVL {
    struct TextureNode *root;
};

void texture_avl_free(struct TextureAVL *avl);
extern struct TextureAVL TEXTURES[GLIDE_NUM_TMU];

int textures_init(void);
void textures_terminate(void);

#endif

