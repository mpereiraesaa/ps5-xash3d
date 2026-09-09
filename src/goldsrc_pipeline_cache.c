#include "goldsrc_pipeline_cache.h"

#include <stddef.h>
#include <string.h>

int goldsrc_pipeline_shader_variant(const GoldSrcRenderState *state,
                                    GoldSrcShaderVariant *out_variant)
{
    if (!out_variant || goldsrc_render_state_validate(state) != 0)
        return -1;
    if (state->screen_space) {
        *out_variant = state->blend == GOLDSRC_BLEND_ALPHA_TEST ?
            GOLDSRC_SHADER_SCREEN_2D_MASKED : GOLDSRC_SHADER_SCREEN_2D;
        return 0;
    }
    const unsigned feature = (unsigned)state->lightmap |
                             ((unsigned)state->fog << 1);
    *out_variant = (GoldSrcShaderVariant)(
        (state->blend == GOLDSRC_BLEND_ALPHA_TEST ?
             GOLDSRC_SHADER_MASKED : GOLDSRC_SHADER_SURFACE) + feature);
    return 0;
}

const char *goldsrc_pipeline_shader_variant_name(GoldSrcShaderVariant variant)
{
    static const char *const names[GOLDSRC_SHADER_VARIANT_COUNT] = {
        "surface", "surface_lightmap", "surface_fog",
        "surface_lightmap_fog", "masked", "masked_lightmap",
        "masked_fog", "masked_lightmap_fog", "screen_2d", "screen_2d_masked",
    };
    return variant < GOLDSRC_SHADER_VARIANT_COUNT ? names[variant] : NULL;
}

static int append(GoldSrcPipelineCache *cache,
                  const GoldSrcRenderState *state,
                  uint32_t base_db_depth_control,
                  uint32_t base_pa_su_sc_mode_cntl)
{
    if (cache->count >= GOLDSRC_PIPELINE_PERMUTATION_COUNT)
        return -1;
    GoldSrcPipelinePermutation *entry = &cache->entries[cache->count];
    entry->state = *state;
    if (goldsrc_render_state_key(state, &entry->key) != 0 ||
        goldsrc_render_state_pass(state, &entry->pass) != 0 ||
        goldsrc_pipeline_shader_variant(state, &entry->shader) != 0 ||
        ps5_goldsrc_render_registers_build(
            entry->dynamic_cx, state, base_db_depth_control,
            base_pa_su_sc_mode_cntl) != 0)
        return -1;
    ++cache->count;
    return 0;
}

int goldsrc_pipeline_cache_build(GoldSrcPipelineCache *out,
                                 uint32_t base_db_depth_control,
                                 uint32_t base_pa_su_sc_mode_cntl)
{
    if (!out)
        return -1;
    memset(out, 0, sizeof(*out));
    for (unsigned blend = 0; blend < GOLDSRC_BLEND_MODE_COUNT; ++blend)
        for (unsigned depth = 0; depth < 2; ++depth)
            for (unsigned cull = 0; cull < GOLDSRC_CULL_MODE_COUNT; ++cull)
                for (unsigned fog = 0; fog < 2; ++fog)
                    for (unsigned lightmap = 0; lightmap < 2; ++lightmap) {
                        const GoldSrcRenderState state = {
                            (GoldSrcBlendMode)blend,
                            (GoldSrcCullMode)cull,
                            (uint8_t)depth,
                            (uint8_t)fog,
                            (uint8_t)lightmap,
                            0u,
                        };
                        if (append(out, &state, base_db_depth_control,
                                   base_pa_su_sc_mode_cntl) != 0)
                            return -1;
                    }
    const GoldSrcBlendMode screen_blends[GOLDSRC_PIPELINE_2D_COUNT] = {
        GOLDSRC_BLEND_OPAQUE, GOLDSRC_BLEND_ALPHA, GOLDSRC_BLEND_ADDITIVE,
        GOLDSRC_BLEND_ALPHA_TEST, GOLDSRC_BLEND_SCREEN_MODULATE,
    };
    for (unsigned i = 0; i < GOLDSRC_PIPELINE_2D_COUNT; ++i) {
        GoldSrcRenderState state;
        if (goldsrc_render_state_2d(screen_blends[i], &state) != 0 ||
            append(out, &state, base_db_depth_control,
                   base_pa_su_sc_mode_cntl) != 0)
            return -1;
    }
    return out->count == GOLDSRC_PIPELINE_PERMUTATION_COUNT ? 0 : -1;
}

const GoldSrcPipelinePermutation *goldsrc_pipeline_cache_find(
    const GoldSrcPipelineCache *cache, const GoldSrcRenderState *state)
{
    uint32_t key;
    if (!cache || cache->count > GOLDSRC_PIPELINE_PERMUTATION_COUNT ||
        goldsrc_render_state_key(state, &key) != 0)
        return NULL;
    for (uint32_t i = 0; i < cache->count; ++i)
        if (cache->entries[i].key == key)
            return &cache->entries[i];
    return NULL;
}
