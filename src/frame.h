#ifndef FRAME_H
#define FRAME_H

#include "vulkan.h"
//#include "glide.h"
#include "pipeline.h"

#define MAX_CONCURRENT_FRAMES 2

struct DrVertex {
    float coord[4];
    float color[4];
    float st0[2];
    float st1[2];
    float st2[2];
};

struct Frame;

void frame_reset(struct Frame *frame);
void frame_present(struct Frame *frame);
void frame_lock(struct Frame *frame, void **ptr);
void frame_unlock(struct Frame *frame);
void frame_skip(struct Frame *frame);
void frame_render(struct Frame *frame, struct PipelineArray *pa);
void frame_add_vertices(struct Frame *frame, struct DrVertex *v1, struct DrVertex *v2, struct DrVertex *v3);
void frame_add_fog_table(struct Frame *frame, struct FogTable *table);

int frames_init(VkPhysicalDevice physical_device, VkDevice device, size_t width, size_t height);
void frames_terminate(void);
struct Frame *frames_get_current(void);
void frames_advance(void);

#endif

