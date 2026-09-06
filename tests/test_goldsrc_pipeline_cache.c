#include "../src/goldsrc_pipeline_cache.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

int main(void)
{
    GoldSrcPipelineCache cache;
    assert(goldsrc_pipeline_cache_build(
        &cache, 0x000000b6u, 0x00a80244u) == 0);
    assert(cache.count == 99u);

    uint8_t keys[256] = {0};
    unsigned shader_counts[GOLDSRC_SHADER_VARIANT_COUNT] = {0};
    unsigned pass_counts[GOLDSRC_PASS_SCREEN_2D + 1] = {0};
    for (uint32_t i = 0; i < cache.count; ++i) {
        const GoldSrcPipelinePermutation *entry = &cache.entries[i];
        assert(entry->key < sizeof(keys));
        assert(keys[entry->key] == 0u);
        keys[entry->key] = 1u;
        ++shader_counts[entry->shader];
        ++pass_counts[entry->pass];
        assert(entry->dynamic_cx[0].offset ==
               PS5_GOLDSRC_CB_BLEND0_CONTROL);
        assert(entry->dynamic_cx[1].offset ==
               PS5_GOLDSRC_DB_DEPTH_CONTROL);
        assert(entry->dynamic_cx[2].offset ==
               PS5_GOLDSRC_PA_SU_SC_MODE_CNTL);
        assert(goldsrc_pipeline_cache_find(&cache, &entry->state) == entry);
    }
    assert(pass_counts[GOLDSRC_PASS_OPAQUE] == 24u);
    assert(pass_counts[GOLDSRC_PASS_MASKED] == 24u);
    assert(pass_counts[GOLDSRC_PASS_TRANSLUCENT] == 24u);
    assert(pass_counts[GOLDSRC_PASS_ADDITIVE] == 24u);
    assert(pass_counts[GOLDSRC_PASS_SCREEN_2D] == 3u);
    for (unsigned i = GOLDSRC_SHADER_SURFACE;
         i <= GOLDSRC_SHADER_SURFACE_LIGHTMAP_FOG; ++i)
        assert(shader_counts[i] == 18u);
    for (unsigned i = GOLDSRC_SHADER_MASKED;
         i <= GOLDSRC_SHADER_MASKED_LIGHTMAP_FOG; ++i)
        assert(shader_counts[i] == 6u);
    assert(shader_counts[GOLDSRC_SHADER_SCREEN_2D] == 3u);

    GoldSrcRenderState query;
    assert(goldsrc_render_state_from_mode(
        GOLDSRC_RENDER_TRANS_TEXTURE, GOLDSRC_CULL_FRONT,
        1, 1, &query) == 0);
    const GoldSrcPipelinePermutation *entry =
        goldsrc_pipeline_cache_find(&cache, &query);
    assert(entry != NULL);
    assert(entry->pass == GOLDSRC_PASS_TRANSLUCENT);
    assert(entry->shader == GOLDSRC_SHADER_SURFACE_LIGHTMAP_FOG);
    assert(strcmp(goldsrc_pipeline_shader_variant_name(entry->shader),
                  "surface_lightmap_fog") == 0);
    assert(entry->dynamic_cx[0].value == 0x65010504u);
    assert(entry->dynamic_cx[1].value == 0x000000b2u);
    assert(entry->dynamic_cx[2].value == 0x00a80245u);

    assert(goldsrc_pipeline_cache_build(NULL, 0u, 0u) == -1);
    cache.count = GOLDSRC_PIPELINE_PERMUTATION_COUNT + 1u;
    assert(goldsrc_pipeline_cache_find(&cache, &query) == NULL);
    assert(goldsrc_pipeline_shader_variant_name(
        GOLDSRC_SHADER_VARIANT_COUNT) == NULL);
    return 0;
}
