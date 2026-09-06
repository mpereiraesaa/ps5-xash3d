#ifndef PS5_XASH3D_GOLDSRC_PIPELINE_CACHE_H
#define PS5_XASH3D_GOLDSRC_PIPELINE_CACHE_H

#include "goldsrc_render_state.h"
#include "ps5_goldsrc_render_state.h"

typedef enum GoldSrcShaderVariant {
    GOLDSRC_SHADER_SURFACE = 0,
    GOLDSRC_SHADER_SURFACE_LIGHTMAP = 1,
    GOLDSRC_SHADER_SURFACE_FOG = 2,
    GOLDSRC_SHADER_SURFACE_LIGHTMAP_FOG = 3,
    GOLDSRC_SHADER_MASKED = 4,
    GOLDSRC_SHADER_MASKED_LIGHTMAP = 5,
    GOLDSRC_SHADER_MASKED_FOG = 6,
    GOLDSRC_SHADER_MASKED_LIGHTMAP_FOG = 7,
    GOLDSRC_SHADER_SCREEN_2D = 8,
    GOLDSRC_SHADER_VARIANT_COUNT = 9,
} GoldSrcShaderVariant;

enum {
    GOLDSRC_PIPELINE_3D_COUNT =
        GOLDSRC_BLEND_MODE_COUNT * 2 * GOLDSRC_CULL_MODE_COUNT * 2 * 2,
    GOLDSRC_PIPELINE_2D_COUNT = 3,
    GOLDSRC_PIPELINE_PERMUTATION_COUNT =
        GOLDSRC_PIPELINE_3D_COUNT + GOLDSRC_PIPELINE_2D_COUNT,
};

typedef struct GoldSrcPipelinePermutation {
    GoldSrcRenderState state;
    uint32_t key;
    GoldSrcRenderPass pass;
    GoldSrcShaderVariant shader;
    ps5_agc_register dynamic_cx[PS5_GOLDSRC_RENDER_REGISTER_COUNT];
} GoldSrcPipelinePermutation;

typedef struct GoldSrcPipelineCache {
    GoldSrcPipelinePermutation entries[GOLDSRC_PIPELINE_PERMUTATION_COUNT];
    uint32_t count;
} GoldSrcPipelineCache;

int goldsrc_pipeline_shader_variant(const GoldSrcRenderState *state,
                                    GoldSrcShaderVariant *out_variant);
const char *goldsrc_pipeline_shader_variant_name(GoldSrcShaderVariant variant);

int goldsrc_pipeline_cache_build(GoldSrcPipelineCache *out,
                                 uint32_t base_db_depth_control,
                                 uint32_t base_pa_su_sc_mode_cntl);

const GoldSrcPipelinePermutation *goldsrc_pipeline_cache_find(
    const GoldSrcPipelineCache *cache, const GoldSrcRenderState *state);

#endif
