#include "fx.h"

#include <string.h>

#include "glide.h"
#include "common.h"
#include "mesh.h"

/******
* Fog *
*******/

struct StagingFogTable {
    bool chagned;
    struct FogTable current;
    struct FogTable next;
};

struct StagingFogTable STAGING_FOG_TABLE = {
    .chagned = true
};

FX_ENTRY void FX_CALL grFogMode(GrFogMode_t mode)
{
    LOG(LEVEL_TRACE, "Called with %d\n", mode);
    staging_mesh_set_fog_mode(mode);
}

FX_ENTRY void FX_CALL grFogTable(const GrFog_t *ft)
{
    LOG(LEVEL_TRACE, "Called\n");
    staging_fog_table_set(ft);
}

FX_ENTRY void FX_CALL grFogColorValue(GrColor_t fog_color)
{
    LOG(LEVEL_TRACE, "Called\n");
    staging_mesh_set_fog_color(fog_color);
}

/************
* Chromakey *
*************/

FX_ENTRY void FX_CALL grChromakeyValue(GrColor_t value)
{
    LOG(LEVEL_TRACE, "Value: %u\n", value);
    staging_mesh_set_chromakey_value(value);
}

FX_ENTRY void FX_CALL grChromakeyMode(GrChromakeyMode_t mode)
{
    LOG(LEVEL_TRACE, "Enable: %d\n", mode);
    staging_mesh_set_chromakey_enable(mode == GR_CHROMAKEY_ENABLE ? true : false);
}

/****************
* Alpha testing *
*****************/

FX_ENTRY void FX_CALL grAlphaTestReferenceValue(GrAlpha_t value)
{
    LOG(LEVEL_TRACE, "Called\n");
    staging_mesh_set_alpha_test_reference_value(value);
}

FX_ENTRY void FX_CALL grAlphaTestFunction(GrCmpFnc_t function)
{
    LOG(LEVEL_TRACE, "Called\n");
    staging_mesh_set_alpha_test_function(function);
}

void staging_fog_table_set(const uint8_t *data)
{
    for (size_t i = 0; i < GR_FOG_TABLE_SIZE; i++) {
        STAGING_FOG_TABLE.next.table[i] = data[i];
        if (STAGING_FOG_TABLE.next.table[i] != STAGING_FOG_TABLE.current.table[i])
            STAGING_FOG_TABLE.chagned = true;

    }
}

bool staging_fog_table_changed(void)
{
    return STAGING_FOG_TABLE.chagned;
}

struct FogTable *staging_fog_table_get(void)
{
    STAGING_FOG_TABLE.chagned = false;
    return &STAGING_FOG_TABLE.next;
}

void staging_fog_table_force_change(void)
{
    STAGING_FOG_TABLE.chagned = true;
}

