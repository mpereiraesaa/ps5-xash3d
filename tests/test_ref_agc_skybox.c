#include "ref_agc_skybox.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void flush_memory(const void *memory, size_t bytes, void *user)
{
    (void)user;
    assert(memory && bytes);
}

static int set_direct(uint32_t **cursor, uint32_t capacity,
                      uint32_t offset, const uint32_t *values,
                      uint32_t count)
{
    if (capacity < count + 2u) return -1;
    *(*cursor)++ = offset;
    *(*cursor)++ = count;
    memcpy(*cursor, values, count * sizeof(*values));
    *cursor += count;
    return 0;
}

static int draw_indexed(uint32_t **cursor, uint32_t capacity,
                        uint32_t count, const uint16_t *indices,
                        const void *mapping, size_t bytes,
                        uint64_t modifier)
{
    assert(indices && mapping && bytes && modifier && count == 6u);
    if (capacity < 6u) return -1;
    for (uint32_t word = 0u; word < 6u; ++word)
        *(*cursor)++ = count + word;
    return 0;
}

int main(void)
{
    enum { MAPPING_BYTES = 65536, TRANSIENT_OFFSET = 32768 };
    uint8_t *mapping = aligned_alloc(256u, MAPPING_BYTES);
    RefAgcGpuTextureCache *textures = calloc(1u, sizeof(*textures));
    assert(mapping && textures);
    assert(ref_agc_gpu_texture_cache_init(
        textures, mapping, UINT64_C(0x200000000), TRANSIENT_OFFSET,
        flush_memory, NULL) == 0);
    const uint8_t pixels[16] = {
        1, 2, 3, 255, 4, 5, 6, 255,
        7, 8, 9, 255, 10, 11, 12, 255,
    };
    RefAgcLiveSky sky = {.revision = 7u, .active = 1u};
    for (uint32_t side = 0u; side < REF_AGC_SKYBOX_SIDES; ++side) {
        RefAgcTextureView texture = {
            .handle = side + 1u, .width = 2u, .height = 2u, .depth = 1u,
            .mip_count = 1u, .revision = side + 1u,
            .content_hash = UINT64_C(0x1000) + side,
            .pixel_bytes = sizeof(pixels), .pixels = pixels,
            .name = "sky-side", .sampler_clamp = 1u, .active = 1,
        };
        assert(ref_agc_gpu_texture_cache_apply(textures, &texture, 1) == 0);
        sky.texture_handles[side] = side + 1u;
    }
    Ps5TransientRing ring;
    assert(ps5_transient_ring_init(
        &ring, mapping + TRANSIENT_OFFSET,
        MAPPING_BYTES - TRANSIENT_OFFSET, 2u, 256u) == 0);
    assert(ps5_transient_ring_begin(&ring, 0u, 0u, 0) == 0);
    RefAgcSkyboxFrame frame;
    assert(ref_agc_skybox_frame_build(
        &frame, &ring, 0u, mapping, MAPPING_BYTES, &sky, textures) == 0);
    assert(frame.active && frame.sky_revision == 7u &&
           frame.geometry_hash != 0u && frame.texture_hash != 0u &&
           frame.texture_handles[0] == 1u &&
           frame.texture_handles[1] == 3u &&
           frame.texture_handles[2] == 2u &&
           frame.texture_handles[5] == 6u);
    assert(frame.vertices[0].position[0] == 4096.0f &&
           frame.vertices[0].position[1] == -4096.0f &&
           frame.vertices[0].position[2] == -4096.0f);
    assert(frame.vertices[0].base_uv[0] == 0.0f &&
           frame.vertices[0].base_uv[1] == 1.0f);
    assert(frame.indices[35] == 23u);

    _Alignas(16) uint32_t constants[4] = {0};
    memcpy(mapping + 30000u, constants, sizeof(constants));
    const uint32_t *constant_table = (const uint32_t *)(mapping + 30000u);
    uint32_t commands[128] = {0};
    uint32_t *cursor = commands;
    RefAgcSkyboxComposeResult composed;
    assert(ref_agc_skybox_compose(
        &cursor, commands + 128, &frame, constant_table,
        mapping, MAPPING_BYTES, 1u, set_direct, draw_indexed,
        &composed) == 0);
    assert(composed.draws == 6u && composed.indices == 36u &&
           composed.command_dwords == 78u);

    RefAgcLiveSky inactive = {.revision = 8u};
    assert(ps5_transient_ring_begin(&ring, 1u, 0u, 0) == 0);
    assert(ref_agc_skybox_frame_build(
        &frame, &ring, 1u, mapping, MAPPING_BYTES,
        &inactive, textures) == REF_AGC_SKYBOX_INACTIVE);
    assert(!frame.active);
    RefAgcLiveSky unresolved = sky;
    unresolved.texture_handles[4] = 99u;
    const size_t checkpoint = ring.slots[1].used;
    assert(ref_agc_skybox_frame_build(
        &frame, &ring, 1u, mapping, MAPPING_BYTES,
        &unresolved, textures) == REF_AGC_SKYBOX_TEXTURE_UNRESOLVED);
    assert(!frame.active && ring.slots[1].used == checkpoint);
    ref_agc_gpu_texture_cache_destroy(textures);
    free(textures);
    free(mapping);
    puts("ref_agc skybox tests passed");
    return 0;
}
