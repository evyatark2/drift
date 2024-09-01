#ifndef DRIFT_FX_H
#define DRIFT_FX_H

#include <stdbool.h>
#include <stdint.h>

#ifndef GLIDE_LIB
#if defined(GLIDE3) && defined(GLIDE3_ALPHA)
#define GR_FOG_TABLE_SIZE 128
#else /* !(defined(GLIDE3) && defined(GLIDE3_ALPHA)) */
#define GR_FOG_TABLE_SIZE 64
#endif /* !(defined(GLIDE3) && defined(GLIDE3_ALPHA)) */
#endif /* GLIDE_LIB */

struct FogTable {
    uint32_t table[GR_FOG_TABLE_SIZE];
};

void staging_fog_table_set(const uint8_t *data); 
bool staging_fog_table_changed(void);
struct FogTable *staging_fog_table_get(void);
void staging_fog_table_force_change(void);

//extern bool FOG_TABLE_CHANGED;
//extern struct FogTable NEXT_TABLE;

#endif
