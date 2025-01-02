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
    float bias;
};

//struct SamplerConfig;
VkSampler sampler_get_for_config(struct SamplerConfig *config);

struct TextureNode;

struct TextureAVL {
    struct TextureNode *root;
};

void texture_avl_free(struct TextureAVL *avl);
extern struct TextureAVL TEXTURES[GLIDE_NUM_TMU];

int textures_init(void);
void textures_terminate(void);

#endif

