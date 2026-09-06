#include "../src/goldsrc_sprite_particles.h"

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
                                   2u, 256u) == PS5_TRANSIENT_OK);
    const float camera[3] = {64.0f, 32.0f, -16.0f};
    const float forward[3] = {0.25f, -0.05f, 1.0f};

    assert(ps5_transient_ring_begin(&ring, 0u, 0u, 0) ==
           PS5_TRANSIENT_OK);
    GoldSrcSpriteParticleFrame first;
    assert(goldsrc_sprite_particle_frame_build(
               &first, &ring, 0u, memory, sizeof(memory), camera, forward,
               16.0f/9.0f, 0u) == 0);
    assert(first.mode == GOLDSRC_EFFECT_MODE_CONTROL);
    assert(first.sprite_quads == GOLDSRC_EFFECT_SPRITE_QUADS);
    assert(first.alpha_particles == GOLDSRC_EFFECT_ALPHA_PARTICLE_QUADS);
    assert(first.additive_particles ==
           GOLDSRC_EFFECT_ADDITIVE_PARTICLE_QUADS);
    assert(first.vertex_count == GOLDSRC_EFFECT_TOTAL_QUADS * 4u);
    assert(first.sprite_index_count == 6u);
    assert(first.alpha_particle_first_index == 6u);
    assert(first.alpha_particle_index_count == 144u);
    assert(first.additive_particle_first_index == 150u);
    assert(first.additive_particle_index_count == 288u);
    assert(first.atlas_hash != 0u && first.layout_hash != 0u);
    assert(first.transient_bytes > 8192u && first.transient_bytes < 32768u);
    assert(first.constant_table && first.vertex_table && first.texture_table);

    uint32_t commands[64] = {0};
    uint32_t *cursor = commands;
    GoldSrcSpriteParticleComposeResult composed = {0};
    assert(goldsrc_sprite_particle_compose_range(
               &cursor, commands + 64, &first,
               first.sprite_first_index, first.sprite_index_count,
               memory, sizeof(memory), UINT64_C(0x1234), set_sh, draw,
               &composed) == 0);
    assert(goldsrc_sprite_particle_compose_range(
               &cursor, commands + 64, &first,
               first.alpha_particle_first_index,
               first.alpha_particle_index_count,
               memory, sizeof(memory), UINT64_C(0x1234), set_sh, draw,
               &composed) == 0);
    assert(goldsrc_sprite_particle_compose_range(
               &cursor, commands + 64, &first,
               first.additive_particle_first_index,
               first.additive_particle_index_count,
               memory, sizeof(memory), UINT64_C(0x1234), set_sh, draw,
               &composed) == 0);
    assert(composed.draws == 3u && composed.indices == 438u);
    assert(composed.command_dwords == 39u && cursor == commands + 39u);

    assert(ps5_transient_ring_abort_unsubmitted(&ring, 0u) ==
           PS5_TRANSIENT_OK);
    assert(ps5_transient_ring_begin(&ring, 1u, 1u, 0) ==
           PS5_TRANSIENT_OK);
    GoldSrcSpriteParticleFrame second;
    assert(goldsrc_sprite_particle_frame_build(
               &second, &ring, 1u, memory, sizeof(memory), camera, forward,
               16.0f/9.0f, 601u) == 0);
    assert(second.mode == GOLDSRC_EFFECT_MODE_SPRITE);
    assert(second.atlas_hash == first.atlas_hash);
    assert(second.layout_hash != first.layout_hash);
    assert(second.transient_bytes == first.transient_bytes);
    assert(ps5_transient_ring_abort_unsubmitted(&ring, 1u) ==
           PS5_TRANSIENT_OK);

    assert(goldsrc_sprite_particle_mode(1199u) ==
           GOLDSRC_EFFECT_MODE_SPRITE);
    assert(goldsrc_sprite_particle_mode(1200u) ==
           GOLDSRC_EFFECT_MODE_PARTICLES);
    assert(goldsrc_sprite_particle_mode(1800u) ==
           GOLDSRC_EFFECT_MODE_COMBINED);
    assert(goldsrc_sprite_particle_mode(2400u) ==
           GOLDSRC_EFFECT_MODE_CONTROL);
    assert(strcmp(goldsrc_sprite_particle_mode_name(
                      GOLDSRC_EFFECT_MODE_PARTICLES), "particles") == 0);
    assert(strcmp(goldsrc_sprite_particle_mode_name(
                      GOLDSRC_EFFECT_MODE_COUNT), "invalid") == 0);

    Ps5TransientRing small;
    _Alignas(256) uint8_t small_memory[16384];
    assert(ps5_transient_ring_init(&small, small_memory,
                                   sizeof(small_memory), 2u, 256u) ==
           PS5_TRANSIENT_OK);
    assert(ps5_transient_ring_begin(&small, 0u, 0u, 0) ==
           PS5_TRANSIENT_OK);
    const size_t checkpoint = small.slots[0].used;
    assert(goldsrc_sprite_particle_frame_build(
               &second, &small, 0u, small_memory, sizeof(small_memory),
               camera, forward, 16.0f/9.0f, 0u) == -2);
    assert(small.slots[0].used == checkpoint);
    return 0;
}
