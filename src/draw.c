#include "draw.h"

#include <assert.h>
#include <stdbool.h>

#include "glide.h"

#include "common.h"
#include "frame.h"
#include "mesh.h"
#include "pipeline.h"

uint32_t CURRENT_SET;
VkDescriptorSet SETS[MAX_CONCURRENT_DRAWS];

VkPipeline WIREFRAME_PIPELINE;

FX_ENTRY void FX_CALL grDrawTriangle(const GrVertex *v1, const GrVertex *v2, const GrVertex *v3)
{
    LOG_CALL();

    staging_pipeline_set_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

    if (staging_pipeline_changed()) {
        LOG(LEVEL_TRACE, "Recreating pipeline\n");
        if (pipeline_array_append(PIPELINES, staging_pipeline_get()) == -1)
            assert(0);
        struct PipelineInfo *pi = pipeline_array_get_last(PIPELINES);
        if (pipeline_info_add_mesh(pi, staging_mesh_get()) == -1)
            assert(0);
    }

    if (staging_mesh_changed()) {
        struct PipelineInfo *pi = pipeline_array_get_last(PIPELINES);
        if (pipeline_info_add_mesh(pi, staging_mesh_get()) == -1)
            assert(0);
        //struct Mesh *mesh = pipeline_info_get_mesh(pi);
    }

    if (staging_fog_table_changed())
        frame_add_fog_table(frames_get_current(), staging_fog_table_get());

    struct DrVertex a = {
        .coord = { v1->x, v1->y, v1->z, v1->oow },
        .color = { v1->r, v1->g, v1->b, v1->a },
        .st0 = { v1->tmuvtx[0].sow, v1->tmuvtx[0].tow },
        .st1 = { v1->tmuvtx[1].sow, v1->tmuvtx[1].tow },
        .st2 = { v1->tmuvtx[2].sow, v1->tmuvtx[2].tow },
    };
    struct DrVertex b = {
        .coord = { v2->x, v2->y, v2->z, v2->oow },
        .color = { v2->r, v2->g, v2->b, v2->a },
        .st0 = { v2->tmuvtx[0].sow, v2->tmuvtx[0].tow },
        .st1 = { v2->tmuvtx[1].sow, v2->tmuvtx[1].tow },
        .st2 = { v2->tmuvtx[2].sow, v2->tmuvtx[2].tow },
    };
    struct DrVertex c = {
        .coord = { v3->x, v3->y, v3->z, v3->oow },
        .color = { v3->r, v3->g, v3->b, v3->a },
        .st0 = { v3->tmuvtx[0].sow, v3->tmuvtx[0].tow },
        .st1 = { v3->tmuvtx[1].sow, v3->tmuvtx[1].tow },
        .st2 = { v3->tmuvtx[2].sow, v3->tmuvtx[2].tow },
    };
    frame_add_triangle(frames_get_current(), &a, &b, &c);
    pipeline_array_add_primitive(PIPELINES);
}

FX_ENTRY void FX_CALL grDrawLine(const GrVertex* v1, const GrVertex* v2)
{
    LOG_CALL();
    staging_pipeline_set_topology(VK_PRIMITIVE_TOPOLOGY_LINE_LIST);

    if (staging_pipeline_changed()) {
        LOG(LEVEL_TRACE, "Recreating pipeline\n");
        if (pipeline_array_append(PIPELINES, staging_pipeline_get()) == -1)
            assert(0);
        struct PipelineInfo *pi = pipeline_array_get_last(PIPELINES);
        if (pipeline_info_add_mesh(pi, staging_mesh_get()) == -1)
            assert(0);
    }

    if (staging_mesh_changed()) {
        struct PipelineInfo *pi = pipeline_array_get_last(PIPELINES);
        if (pipeline_info_add_mesh(pi, staging_mesh_get()) == -1)
            assert(0);
        //struct Mesh *mesh = pipeline_info_get_mesh(pi);
    }

    if (staging_fog_table_changed())
        frame_add_fog_table(frames_get_current(), staging_fog_table_get());

    struct DrVertex a = {
        .coord = { v1->x, v1->y, v1->z, v1->oow > 1 ? 1 : v1->oow },
        .color = { v1->r, v1->g, v1->b, v1->a },
        .st0 = { v1->tmuvtx[0].sow, v1->tmuvtx[0].tow },
        .st1 = { v1->tmuvtx[1].sow, v1->tmuvtx[1].tow },
        .st2 = { v1->tmuvtx[2].sow, v1->tmuvtx[2].tow },
    };
    struct DrVertex b = {
        .coord = { v2->x, v2->y, v2->z, v2->oow > 1 ? 1 : v2->oow },
        .color = { v2->r, v2->g, v2->b, v2->a },
        .st0 = { v2->tmuvtx[0].sow, v2->tmuvtx[0].tow },
        .st1 = { v2->tmuvtx[1].sow, v2->tmuvtx[1].tow },
        .st2 = { v2->tmuvtx[2].sow, v2->tmuvtx[2].tow },
    };
    frame_add_line(frames_get_current(), &a, &b);
    pipeline_array_add_primitive(PIPELINES);
}
