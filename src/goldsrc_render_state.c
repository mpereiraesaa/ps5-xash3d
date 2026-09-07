#include "goldsrc_render_state.h"

#include <string.h>

static int valid_boolean(uint8_t value)
{
    return value == 0u || value == 1u;
}

int goldsrc_render_state_validate(const GoldSrcRenderState *state)
{
    if (!state || state->blend >= GOLDSRC_BLEND_MODE_COUNT ||
        state->cull >= GOLDSRC_CULL_MODE_COUNT ||
        !valid_boolean(state->depth_write) ||
        !valid_boolean(state->fog) || !valid_boolean(state->lightmap) ||
        !valid_boolean(state->screen_space))
        return -1;
    if (state->screen_space &&
        (state->depth_write || state->fog || state->lightmap ||
         state->cull != GOLDSRC_CULL_NONE ||
         state->blend == GOLDSRC_BLEND_ALPHA_TEST))
        return -2;
    return 0;
}

int goldsrc_render_state_from_mode(uint32_t render_mode,
                                   GoldSrcCullMode cull,
                                   int fog, int lightmap,
                                   GoldSrcRenderState *out)
{
    if (!out || render_mode >= GOLDSRC_RENDER_MODE_COUNT ||
        cull >= GOLDSRC_CULL_MODE_COUNT ||
        (fog != 0 && fog != 1) || (lightmap != 0 && lightmap != 1))
        return -1;
    GoldSrcRenderState state;
    memset(&state, 0, sizeof(state));
    state.cull = cull;
    state.fog = (uint8_t)fog;
    state.lightmap = (uint8_t)lightmap;
    switch ((GoldSrcRenderMode)render_mode) {
    case GOLDSRC_RENDER_NORMAL:
        state.blend = GOLDSRC_BLEND_OPAQUE;
        state.depth_write = 1u;
        break;
    case GOLDSRC_RENDER_TRANS_COLOR:
    case GOLDSRC_RENDER_TRANS_TEXTURE:
        state.blend = GOLDSRC_BLEND_ALPHA;
        break;
    case GOLDSRC_RENDER_GLOW:
    case GOLDSRC_RENDER_TRANS_ADD:
        state.blend = GOLDSRC_BLEND_ADDITIVE;
        break;
    case GOLDSRC_RENDER_TRANS_ALPHA:
        state.blend = GOLDSRC_BLEND_ALPHA_TEST;
        state.depth_write = 1u;
        break;
    default:
        return -1;
    }
    if (goldsrc_render_state_validate(&state) != 0)
        return -1;
    *out = state;
    return 0;
}

int goldsrc_render_state_2d(GoldSrcBlendMode blend,
                            GoldSrcRenderState *out)
{
    if (!out || (blend != GOLDSRC_BLEND_OPAQUE &&
                 blend != GOLDSRC_BLEND_ALPHA &&
                 blend != GOLDSRC_BLEND_ADDITIVE))
        return -1;
    const GoldSrcRenderState state = {
        blend, GOLDSRC_CULL_NONE, 0u, 0u, 0u, 1u
    };
    *out = state;
    return 0;
}

int goldsrc_render_state_key(const GoldSrcRenderState *state,
                             uint32_t *out_key)
{
    if (!out_key || goldsrc_render_state_validate(state) != 0)
        return -1;
    *out_key = ((uint32_t)state->blend <<
                   GOLDSRC_RENDER_KEY_BLEND_SHIFT) |
               ((uint32_t)state->depth_write <<
                   GOLDSRC_RENDER_KEY_DEPTH_WRITE_SHIFT) |
               ((uint32_t)state->cull << GOLDSRC_RENDER_KEY_CULL_SHIFT) |
               ((uint32_t)state->fog << GOLDSRC_RENDER_KEY_FOG_SHIFT) |
               ((uint32_t)state->lightmap <<
                   GOLDSRC_RENDER_KEY_LIGHTMAP_SHIFT) |
               ((uint32_t)state->screen_space <<
                   GOLDSRC_RENDER_KEY_SCREEN_SPACE_SHIFT);
    return 0;
}

int goldsrc_render_state_pass(const GoldSrcRenderState *state,
                              GoldSrcRenderPass *out_pass)
{
    if (!out_pass || goldsrc_render_state_validate(state) != 0)
        return -1;
    if (state->screen_space)
        *out_pass = GOLDSRC_PASS_SCREEN_2D;
    else if (state->blend == GOLDSRC_BLEND_OPAQUE)
        *out_pass = GOLDSRC_PASS_OPAQUE;
    else if (state->blend == GOLDSRC_BLEND_ALPHA_TEST)
        *out_pass = GOLDSRC_PASS_MASKED;
    else if (state->blend == GOLDSRC_BLEND_ALPHA)
        *out_pass = GOLDSRC_PASS_TRANSLUCENT;
    else
        *out_pass = GOLDSRC_PASS_ADDITIVE;
    return 0;
}

int goldsrc_render_state_requires_back_to_front(
    const GoldSrcRenderState *state, int *out_required)
{
    if (!out_required || goldsrc_render_state_validate(state) != 0)
        return -1;
    *out_required = !state->screen_space &&
        (state->blend == GOLDSRC_BLEND_ALPHA ||
         state->blend == GOLDSRC_BLEND_ADDITIVE);
    return 0;
}
