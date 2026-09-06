#include "goldsrc_state_matrix.h"

#include <stddef.h>
#include <string.h>

static const GoldSrcStateMatrixCase cases[GOLDSRC_STATE_MATRIX_CASE_COUNT] = {
    {"opaque", {GOLDSRC_BLEND_OPAQUE, GOLDSRC_CULL_NONE, 1u, 0u, 1u, 0u},
     {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f}},
    {"alpha", {GOLDSRC_BLEND_ALPHA, GOLDSRC_CULL_NONE, 0u, 0u, 1u, 0u},
     {1.0f, 0.65f, 0.65f, 0.45f}, {0.0f, 0.0f, 0.0f, 0.0f}},
    {"additive", {GOLDSRC_BLEND_ADDITIVE, GOLDSRC_CULL_NONE, 0u, 0u, 1u, 0u},
     {0.35f, 0.65f, 1.0f, 0.45f}, {0.0f, 0.0f, 0.0f, 0.0f}},
    {"alpha-test", {GOLDSRC_BLEND_ALPHA_TEST, GOLDSRC_CULL_NONE, 1u, 0u, 1u, 0u},
     {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f}},
    {"depth-write-off", {GOLDSRC_BLEND_OPAQUE, GOLDSRC_CULL_NONE, 0u, 0u, 1u, 0u},
     {1.0f, 0.85f, 0.40f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f}},
    {"cull-front", {GOLDSRC_BLEND_OPAQUE, GOLDSRC_CULL_FRONT, 1u, 0u, 1u, 0u},
     {0.60f, 1.0f, 0.60f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f}},
    {"cull-back", {GOLDSRC_BLEND_OPAQUE, GOLDSRC_CULL_BACK, 1u, 0u, 1u, 0u},
     {1.0f, 0.60f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f}},
    {"fog", {GOLDSRC_BLEND_OPAQUE, GOLDSRC_CULL_NONE, 1u, 1u, 1u, 0u},
     {1.0f, 1.0f, 1.0f, 1.0f}, {0.08f, 0.18f, 0.35f, 0.002f}},
    {"lightmap-off", {GOLDSRC_BLEND_OPAQUE, GOLDSRC_CULL_NONE, 1u, 0u, 0u, 0u},
     {0.65f, 0.65f, 0.65f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f}},
};

const GoldSrcStateMatrixCase *goldsrc_state_matrix_case(uint32_t index)
{
    return index < GOLDSRC_STATE_MATRIX_CASE_COUNT ? &cases[index] : NULL;
}

uint32_t goldsrc_state_matrix_index(uint64_t frame_index)
{
    return (uint32_t)((frame_index / GOLDSRC_STATE_MATRIX_HOLD_FRAMES) %
                      GOLDSRC_STATE_MATRIX_CASE_COUNT);
}

int goldsrc_state_matrix_validate(void)
{
    uint32_t keys[GOLDSRC_STATE_MATRIX_CASE_COUNT];
    unsigned blend_mask = 0u, cull_mask = 0u, depth_mask = 0u;
    unsigned fog_mask = 0u, lightmap_mask = 0u;
    for (uint32_t i = 0u; i < GOLDSRC_STATE_MATRIX_CASE_COUNT; ++i) {
        if (!cases[i].name || !cases[i].name[0] ||
            goldsrc_render_state_validate(&cases[i].state) != 0 ||
            goldsrc_render_state_key(&cases[i].state, &keys[i]) != 0 ||
            cases[i].render_color[3] <= 0.0f)
            return -1;
        for (uint32_t j = 0u; j < i; ++j)
            if (keys[i] == keys[j] || strcmp(cases[i].name, cases[j].name) == 0)
                return -1;
        blend_mask |= 1u << cases[i].state.blend;
        cull_mask |= 1u << cases[i].state.cull;
        depth_mask |= 1u << cases[i].state.depth_write;
        fog_mask |= 1u << cases[i].state.fog;
        lightmap_mask |= 1u << cases[i].state.lightmap;
    }
    return blend_mask == (1u << GOLDSRC_BLEND_MODE_COUNT) - 1u &&
           cull_mask == (1u << GOLDSRC_CULL_MODE_COUNT) - 1u &&
           depth_mask == 3u && fog_mask == 3u && lightmap_mask == 3u
               ? 0
               : -1;
}
