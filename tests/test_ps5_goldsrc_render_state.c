#include "../src/ps5_goldsrc_render_state.h"

#include <assert.h>
#include <stddef.h>

static GoldSrcRenderState state(GoldSrcBlendMode blend,
                                GoldSrcCullMode cull,
                                uint8_t depth_write)
{
    return (GoldSrcRenderState){blend, cull, depth_write, 1u, 1u, 0u};
}

int main(void)
{
    ps5_agc_register regs[PS5_GOLDSRC_RENDER_REGISTER_COUNT];
    GoldSrcRenderState draw = state(
        GOLDSRC_BLEND_OPAQUE, GOLDSRC_CULL_BACK, 1u);
    assert(ps5_goldsrc_render_registers_build(
        regs, &draw, 0x000000b6u, 0x00a80244u) == 0);
    assert(regs[0].offset == PS5_GOLDSRC_CB_BLEND0_CONTROL);
    assert(regs[0].value == 0u);
    assert(regs[1].offset == PS5_GOLDSRC_DB_DEPTH_CONTROL);
    assert(regs[1].value == 0x000000b6u);
    assert(regs[2].offset == PS5_GOLDSRC_PA_SU_SC_MODE_CNTL);
    assert(regs[2].value == 0x00a80246u);

    draw = state(GOLDSRC_BLEND_ALPHA, GOLDSRC_CULL_FRONT, 0u);
    assert(ps5_goldsrc_render_registers_build(
        regs, &draw, 0x000000b6u, 0x00a80246u) == 0);
    assert(regs[0].value == 0x65010504u);
    assert(regs[1].value == 0x000000b2u);
    assert(regs[2].value == 0x00a80245u);

    draw = state(GOLDSRC_BLEND_ADDITIVE, GOLDSRC_CULL_NONE, 0u);
    assert(ps5_goldsrc_render_registers_build(
        regs, &draw, 0x000000b6u, 0x00a80247u) == 0);
    assert(regs[0].value == 0x61010104u);
    assert(regs[1].value == 0x000000b2u);
    assert(regs[2].value == 0x00a80244u);

    draw = state(GOLDSRC_BLEND_ALPHA_TEST, GOLDSRC_CULL_BACK, 1u);
    assert(ps5_goldsrc_render_registers_build(
        regs, &draw, 0x80000072u, 0x00a80240u) == 0);
    assert(regs[0].value == 0u);
    assert(regs[1].value == 0x80000076u);
    assert(regs[2].value == 0x00a80242u);

    assert(goldsrc_render_state_2d(GOLDSRC_BLEND_ALPHA, &draw) == 0);
    assert(ps5_goldsrc_render_registers_build(
        regs, &draw, 0x800000b6u, 0x00a80247u) == 0);
    assert(regs[0].value == 0x65010504u);
    assert(regs[1].value == 0x80000080u);
    assert(regs[2].value == 0x00a80244u);

    assert(goldsrc_render_state_2d(GOLDSRC_BLEND_SCREEN_MODULATE, &draw) == 0);
    assert(ps5_goldsrc_render_registers_build(
        regs, &draw, 0x800000b6u, 0x00a80247u) == 0);
    assert(regs[0].value == 0x64000200u);
    assert(regs[1].value == 0x80000080u);
    assert(regs[2].value == 0x00a80244u);

    assert(goldsrc_render_state_2d(GOLDSRC_BLEND_ALPHA_TEST, &draw) == 0);
    assert(ps5_goldsrc_render_registers_build(
        regs, &draw, 0x800000b6u, 0x00a80247u) == 0);
    assert(regs[0].value == 0u);
    assert(regs[1].value == 0x80000080u);
    assert(regs[2].value == 0x00a80244u);

    draw.screen_space = 2u;
    assert(ps5_goldsrc_render_registers_build(
        regs, &draw, 0u, 0u) == -1);
    assert(ps5_goldsrc_render_registers_build(
        NULL, &draw, 0u, 0u) == -1);
    assert(ps5_goldsrc_render_registers_build(
        regs, NULL, 0u, 0u) == -1);
    return 0;
}
