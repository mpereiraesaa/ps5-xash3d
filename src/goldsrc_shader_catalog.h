#ifndef PS5_XASH3D_GOLDSRC_SHADER_CATALOG_H
#define PS5_XASH3D_GOLDSRC_SHADER_CATALOG_H

#include "goldsrc_pipeline_cache.h"
#include "ps5_shader_header.h"

typedef struct GoldSrcShaderAsset {
    const char *name;
    GoldSrcShaderVariant variant;
    const uint8_t *gs_start;
    const uint8_t *gs_end;
    const uint8_t *ps_start;
    const uint8_t *ps_end;
    const struct ps5_shader_metadata *metadata;
} GoldSrcShaderAsset;

#endif
