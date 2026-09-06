#ifndef PS5_XASH3D_PS5_GOLDSRC_RENDER_STATE_H
#define PS5_XASH3D_PS5_GOLDSRC_RENDER_STATE_H

#include "../include/ps5_agc.h"
#include "goldsrc_render_state.h"

enum {
    PS5_GOLDSRC_RENDER_REGISTER_COUNT = 3,
    PS5_GOLDSRC_CB_BLEND0_CONTROL = 0x1e0,
    PS5_GOLDSRC_DB_DEPTH_CONTROL = 0x200,
    PS5_GOLDSRC_PA_SU_SC_MODE_CNTL = 0x205,
};

/*
 * Build the dynamic GFX10.3 state owned by a GoldSrc draw. The base values
 * carry pipeline-owned depth comparison, winding and rasterization fields;
 * this function changes only depth enable/write and cull selection there.
 */
int ps5_goldsrc_render_registers_build(
    ps5_agc_register out[PS5_GOLDSRC_RENDER_REGISTER_COUNT],
    const GoldSrcRenderState *state,
    uint32_t base_db_depth_control,
    uint32_t base_pa_su_sc_mode_cntl);

#endif
