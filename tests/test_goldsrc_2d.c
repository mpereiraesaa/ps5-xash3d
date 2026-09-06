#include "../src/goldsrc_2d.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static int set_sh(uint32_t **cursor, uint32_t capacity, uint32_t offset,
                  const uint32_t *values, uint32_t count)
{
    assert(cursor && values && capacity >= count + 2u);
    assert(offset == 0x8du || offset == 0x0du);
    *cursor += count + 2u;
    return 0;
}

static int draw(uint32_t **cursor, uint32_t capacity, uint32_t count,
                const uint16_t *indices, const void *mapping,
                size_t mapping_bytes, uint64_t modifier)
{
    assert(cursor && capacity >= 6u && count && indices && mapping);
    assert(mapping_bytes == 262144u && modifier == UINT64_C(0x1234));
    *cursor += 6u;
    return 0;
}

int main(void)
{
    _Alignas(256) uint8_t memory[262144];
    memset(memory, 0, sizeof(memory));
    Ps5TransientRing ring;
    assert(ps5_transient_ring_init(&ring, memory, sizeof(memory),
                                   2u, 256u) == 0);
    assert(ps5_transient_ring_begin(&ring, 0u, 0u, 0) == 0);
    GoldSrc2DFrame frame;
    assert(goldsrc_2d_frame_build(&frame, &ring, 0u, memory,
                                  sizeof(memory), 1920u, 1080u, 42u) == 0);
    assert(frame.atlas_hash != 0u && frame.layout_hash != 0u);
    assert(frame.console_quads == 2u && frame.menu_quads == 3u);
    assert(frame.hud_quads == 4u && frame.font_quads == 78u);
    assert(frame.alpha_first_index == 0u);
    assert(frame.alpha_index_count > frame.additive_index_count);
    assert(frame.additive_first_index == frame.alpha_index_count);
    assert(frame.additive_index_count == 12u);
    assert(frame.vertex_count ==
           (frame.alpha_index_count + frame.additive_index_count) / 6u * 4u);
    assert(frame.transient_bytes < ring.slots[0].bytes);
    assert(frame.vertices[0].position[0] == 48.0f);

    uint32_t commands[64] = {0};
    uint32_t *cursor = commands;
    GoldSrc2DComposeResult composed = {0};
    assert(goldsrc_2d_compose_range(
        &cursor, commands + 64u, &frame, frame.alpha_first_index,
        frame.alpha_index_count, memory, sizeof(memory), UINT64_C(0x1234),
        set_sh, draw,
        &composed) == 0);
    assert(goldsrc_2d_compose_range(
        &cursor, commands + 64u, &frame, frame.additive_first_index,
        frame.additive_index_count, memory, sizeof(memory), UINT64_C(0x1234),
        set_sh, draw,
        &composed) == 0);
    assert(composed.draws == 2u);
    assert(composed.indices == frame.alpha_index_count +
                                frame.additive_index_count);
    assert(composed.command_dwords == 26u);
    return 0;
}
