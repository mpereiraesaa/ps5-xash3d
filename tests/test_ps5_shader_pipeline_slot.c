#include "../src/ps5_shader_pipeline_slot.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static const ps5_agc_register pre_cx[10] = {
    {0x1ff, 0x80}, {0x2d3, 0x20001}, {0x207, 0}, {0x1c2, 1}, {0x1c3, 4},
    {0x1b1, 0}, {0x2ab, 1}, {0x2e4, 0}, {0x2ce, 1}, {0x291, 0x2004007e},
};
static const ps5_agc_register pixel_cx[9] = {
    {0x08f, 0x0f}, {0x203, 0x810}, {0x310, 0}, {0x1b8, 0x01000000},
    {0x1b4, 2}, {0x1b3, 2}, {0x1b6, 2}, {0x1c5, 4}, {0x1c4, 0},
};
static const struct ps5_shader_metadata metadata = {
    .gs_rsrc1 = 0x2a2c0142, .gs_rsrc2 = 0x38,
    .ps_rsrc1 = 0x022c0001, .ps_rsrc2 = 2,
    .ge_cntl = 0xfc80, .shader_stages_en = 0x02412010,
    .gs_out_prim_type = 2, .draw_modifier = 0x1234,
    .pre_raster_cx = pre_cx, .pre_raster_cx_count = 10,
    .pixel_cx = pixel_cx, .pixel_cx_count = 9,
};

static unsigned create_calls;

static int32_t fake_create(void **object, void *header, void *code)
{
    assert(object && header && code);
    *object = header;
    ++create_calls;
    return 0;
}

static int32_t fake_link(void *cx, void *uc, void *reserved,
                         void *pre_raster, void *pixel, uint32_t primitive)
{
    assert(cx && uc && !reserved && pre_raster && pixel && primitive == 4u);
    memset(cx, 0x3a, sizeof(struct ps5_agc_linked_cx));
    memset(uc, 0x5c, sizeof(struct ps5_agc_linked_uc));
    return 0;
}

int main(void)
{
    uint8_t *slot = aligned_alloc(
        PS5_SHADER_PIPELINE_SLOT_BYTES, PS5_SHADER_PIPELINE_SLOT_BYTES);
    assert(slot != NULL);
    uint8_t gs[388];
    uint8_t ps[404];
    memset(gs, 0x71, sizeof(gs));
    memset(ps, 0x92, sizeof(ps));
    const Ps5EmbeddedShaderPair embedded = {
        gs, sizeof(gs), ps, sizeof(ps), &metadata,
    };
    ps5_agc_register colors[2][PS5_PIPELINE_RT_REGISTERS];
    static const uint32_t offsets[PS5_PIPELINE_RT_REGISTERS] = {
        0x318, 0x31b, 0x31c, 0x31d, 0x31e, 0x31f, 0x321, 0x323,
        0x324, 0x325, 0x390, 0x398, 0x3a0, 0x3a8, 0x3b0, 0x3b8,
    };
    for (unsigned buffer = 0; buffer < 2; ++buffer)
        for (unsigned i = 0; i < PS5_PIPELINE_RT_REGISTERS; ++i)
            colors[buffer][i] = (ps5_agc_register){
                offsets[i], 0x1000u * buffer + i,
            };
    Ps5ShaderPipelineSlotResult result = {0};
    assert(ps5_shader_pipeline_slot_build(
        slot, PS5_SHADER_PIPELINE_SLOT_BYTES, &embedded, colors,
        1920u, 1080u, fake_create, fake_link, &result) == 0);
    assert(create_calls == 2u && result.draw_modifier == 0x1234u);
    assert(result.pipelines[0] ==
           (struct ps5_pipeline_registers *)(slot +
               PS5_SHADER_PIPELINE_REGISTERS_OFFSET));
    assert(result.pipelines[1] == result.pipelines[0] + 1);
    assert(result.pipelines[0]->cx[0].value == 0u);
    assert(result.pipelines[1]->cx[0].value == 0x1000u);
    assert(memcmp(slot + PS5_SHADER_PIPELINE_GS_CODE_OFFSET,
                  gs, sizeof(gs)) == 0);
    assert(memcmp(slot + PS5_SHADER_PIPELINE_PS_CODE_OFFSET,
                  ps, sizeof(ps)) == 0);
    assert(memcmp(slot + PS5_SHADER_PIPELINE_GS_CODE_OFFSET + sizeof(gs),
                  "barefoot", 8u) == 0);

    Ps5EmbeddedShaderPair oversized = embedded;
    oversized.gs_bytes = PS5_SHADER_PIPELINE_PS_CODE_OFFSET -
                         PS5_SHADER_PIPELINE_GS_CODE_OFFSET;
    assert(ps5_shader_pipeline_slot_build(
        slot, PS5_SHADER_PIPELINE_SLOT_BYTES, &oversized, colors,
        1920u, 1080u, fake_create, fake_link, &result) == -1);
    assert(ps5_shader_pipeline_slot_build(
        slot, PS5_SHADER_PIPELINE_SLOT_BYTES - 1u, &embedded, colors,
        1920u, 1080u, fake_create, fake_link, &result) == -1);
    assert(ps5_shader_pipeline_slot_build(
        NULL, PS5_SHADER_PIPELINE_SLOT_BYTES, &embedded, colors,
        1920u, 1080u, fake_create, fake_link, &result) == -1);
    free(slot);
    return 0;
}
