#include "ps5_goldsrc_render_state.h"

#include <stddef.h>

enum {
    PS5_BLEND_ZERO = 0,
    PS5_BLEND_ONE = 1,
    /* AMD PAL gfx9_plus_merged_enum.h: BlendOp::BLEND_SRC_COLOR. */
    PS5_BLEND_SRC_COLOR = 2,
    PS5_BLEND_SRC_ALPHA = 4,
    PS5_BLEND_ONE_MINUS_SRC_ALPHA = 5,
    PS5_BLEND_COLOR_DEST_SHIFT = 8,
    PS5_BLEND_ALPHA_SRC_SHIFT = 16,
    PS5_BLEND_ALPHA_DEST_SHIFT = 24,
    PS5_BLEND_SEPARATE_ALPHA = 1u << 29,
    PS5_BLEND_ENABLE = 1u << 30,
    PS5_DEPTH_Z_ENABLE = 1u << 1,
    PS5_DEPTH_Z_WRITE_ENABLE = 1u << 2,
    PS5_DEPTH_ZFUNC_MASK = 7u << 4,
    PS5_CULL_FRONT = 1u << 0,
    PS5_CULL_BACK = 1u << 1,
    PS5_CULL_MASK = PS5_CULL_FRONT | PS5_CULL_BACK,
};

static uint32_t blend_control(GoldSrcBlendMode blend)
{
    if (blend == GOLDSRC_BLEND_SCREEN_ALPHA_MASKED)
        blend = GOLDSRC_BLEND_ALPHA;
    else if (blend == GOLDSRC_BLEND_SCREEN_ADDITIVE_MASKED)
        blend = GOLDSRC_BLEND_ADDITIVE;
    else if (blend == GOLDSRC_BLEND_SCREEN_MODULATE_MASKED)
        blend = GOLDSRC_BLEND_SCREEN_MODULATE;
    if (blend == GOLDSRC_BLEND_SCREEN_MODULATE) {
        /* glBlendFunc(GL_ZERO, GL_SRC_COLOR): dst *= source, including A. */
        return PS5_BLEND_ENABLE | PS5_BLEND_SEPARATE_ALPHA |
               (PS5_BLEND_SRC_COLOR << PS5_BLEND_COLOR_DEST_SHIFT) |
               (PS5_BLEND_SRC_ALPHA << PS5_BLEND_ALPHA_DEST_SHIFT);
    }
    if (blend == GOLDSRC_BLEND_ALPHA) {
        return PS5_BLEND_ENABLE | PS5_BLEND_SEPARATE_ALPHA |
               PS5_BLEND_SRC_ALPHA |
               (PS5_BLEND_ONE_MINUS_SRC_ALPHA <<
                    PS5_BLEND_COLOR_DEST_SHIFT) |
               (PS5_BLEND_ONE << PS5_BLEND_ALPHA_SRC_SHIFT) |
               (PS5_BLEND_ONE_MINUS_SRC_ALPHA <<
                    PS5_BLEND_ALPHA_DEST_SHIFT);
    }
    if (blend == GOLDSRC_BLEND_ADDITIVE) {
        return PS5_BLEND_ENABLE | PS5_BLEND_SEPARATE_ALPHA |
               PS5_BLEND_SRC_ALPHA |
               (PS5_BLEND_ONE << PS5_BLEND_COLOR_DEST_SHIFT) |
               (PS5_BLEND_ONE << PS5_BLEND_ALPHA_SRC_SHIFT) |
               (PS5_BLEND_ONE << PS5_BLEND_ALPHA_DEST_SHIFT);
    }
    return 0u;
}

int ps5_goldsrc_render_registers_build(
    ps5_agc_register out[PS5_GOLDSRC_RENDER_REGISTER_COUNT],
    const GoldSrcRenderState *state,
    uint32_t base_db_depth_control,
    uint32_t base_pa_su_sc_mode_cntl)
{
    if (!out || goldsrc_render_state_validate(state) != 0)
        return -1;

    uint32_t depth = base_db_depth_control;
    if (state->screen_space) {
        depth &= ~(PS5_DEPTH_Z_ENABLE | PS5_DEPTH_Z_WRITE_ENABLE |
                   PS5_DEPTH_ZFUNC_MASK);
    } else {
        depth |= PS5_DEPTH_Z_ENABLE;
        if (state->depth_write)
            depth |= PS5_DEPTH_Z_WRITE_ENABLE;
        else
            depth &= ~PS5_DEPTH_Z_WRITE_ENABLE;
    }

    uint32_t raster = base_pa_su_sc_mode_cntl & ~PS5_CULL_MASK;
    if (state->cull == GOLDSRC_CULL_FRONT)
        raster |= PS5_CULL_FRONT;
    else if (state->cull == GOLDSRC_CULL_BACK)
        raster |= PS5_CULL_BACK;

    out[0] = (ps5_agc_register){
        PS5_GOLDSRC_CB_BLEND0_CONTROL, blend_control(state->blend)
    };
    out[1] = (ps5_agc_register){PS5_GOLDSRC_DB_DEPTH_CONTROL, depth};
    out[2] = (ps5_agc_register){PS5_GOLDSRC_PA_SU_SC_MODE_CNTL, raster};
    return 0;
}
