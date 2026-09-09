#include "ref_agc_gpu_world_cache.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct FlushState {
    unsigned calls;
    size_t bytes;
} FlushState;

static void flush_memory(const void *memory, size_t bytes, void *user)
{
    FlushState *state = user;
    assert(memory && bytes);
    ++state->calls;
    state->bytes += bytes;
}

int main(void)
{
    enum { TEXTURE_BYTES = 4096, WORLD_BYTES = 65536 };
    uint8_t *texture_memory = aligned_alloc(256u, TEXTURE_BYTES);
    uint8_t *world_memory = aligned_alloc(256u, WORLD_BYTES);
    RefAgcGpuTextureCache *textures = calloc(1, sizeof(*textures));
    RefAgcGpuWorldCache *world = calloc(1, sizeof(*world));
    FlushState texture_flush = {0}, world_flush = {0};
    const uint8_t pixels[16] = {
        1, 2, 3, 255, 4, 5, 6, 255,
        7, 8, 9, 255, 10, 11, 12, 255,
    };
    const uint8_t lightmap_pixels[16] = {
        31, 32, 33, 255, 34, 35, 36, 255,
        37, 38, 39, 255, 40, 41, 42, 255,
    };
    const RefAgcTextureView texture = {
        .handle = 5, .width = 2, .height = 2, .depth = 1,
        .mip_count = 1, .revision = 1, .content_hash = 0x55,
        .pixel_bytes = sizeof(pixels), .pixels = pixels, .name = "wall",
        .active = 1,
    };
    const RefAgcWorldVertex vertices[7] = {
        {{0, 0, 0}, {0, 0}, {0, 0}, 10},
        {{1, 0, 0}, {1, 0}, {1, 0}, 10},
        {{1, 1, 0}, {1, 1}, {1, 1}, 10},
        {{0, 1, 0}, {0, 1}, {0, 1}, 10},
        {{2, 0, 0}, {0, 0}, {0, 0}, 11},
        {{3, 0, 0}, {1, 0}, {1, 0}, 11},
        {{2, 1, 0}, {0, 1}, {0, 1}, 11},
    };
    const uint32_t indices[9] = {0, 1, 2, 0, 2, 3, 4, 5, 6};
    const RefAgcWorldDraw draws[2] = {
        {0, 6, 5, 10, 0, REF_AGC_WORLD_DRAW_LIGHTMAP, {0, 0}},
        {6, 3, 5, 11, 0,
         REF_AGC_WORLD_DRAW_ALPHA_TEST | REF_AGC_WORLD_DRAW_TURB,
         {0, 0}},
    };
    const RefAgcWorldView view = {
        .revision = 1, .content_hash = 0x1234,
        .model_name = "maps/c1a0.bsp", .model_flags = 1u << 29,
        .vertices = vertices, .vertex_count = 7,
        .indices = indices, .index_count = 9,
        .draws = draws, .draw_count = 2, .active = 1,
        .lightmap_pixels = lightmap_pixels,
        .lightmap_width = 2, .lightmap_height = 2,
        .lightmap_row_pitch = 8,
        .lightmap_pixel_bytes = sizeof(lightmap_pixels),
        .lightmapped_draw_count = 1,
        .turbulent_draw_count = 1,
    };
    RefAgcGpuWorldStats stats;

    assert(texture_memory && world_memory && textures && world);
    assert(ref_agc_gpu_texture_cache_init(
        textures, texture_memory, UINT64_C(0x200000000), TEXTURE_BYTES,
        flush_memory, &texture_flush) == 0);
    assert(ref_agc_gpu_texture_cache_apply(textures, &texture, 1) == 0);
    assert(ref_agc_gpu_world_cache_init(
        world, world_memory, UINT64_C(0x210000000), WORLD_BYTES,
        flush_memory, &world_flush) == 0);
    assert(ref_agc_gpu_world_cache_apply(world, &view, textures, 1) == 0);
    assert(ref_agc_gpu_world_cache_validate(world) == 0);
    assert(ref_agc_gpu_world_cache_stats(world, &stats) == 0);
    assert(stats.revision == 1u && stats.publishes == 1u && stats.active);
    assert(stats.vertex_count == 7u && stats.index_count == 9u &&
           stats.draw_count == 2u && stats.texture_tables == 2u);
    assert(stats.lightmapped_draw_count == 1u &&
           stats.sky_draw_count == 0u &&
           stats.turbulent_draw_count == 1u &&
           stats.sky_index_count == 0u &&
           stats.turbulent_index_count == 3u &&
           stats.lightmap_width == 2u && stats.lightmap_height == 2u &&
           stats.lightmap_row_pitch == 256u &&
           stats.lightmap_bytes == 512u);
    assert(stats.lightmap_rgb_sum == 438u &&
           stats.lightmap_nonzero_texels == 4u &&
           stats.lightmap_rgb_min == 31u && stats.lightmap_rgb_max == 42u);
    assert(stats.resident_bytes > 7u * sizeof(RefAgcWorldVertex));
    assert(stats.source_hash == 0x1234 && stats.upload_hash != 0u);
    assert(world_flush.calls == 1u);

    const RefAgcGpuWorldDraw *first =
        ref_agc_gpu_world_cache_draw(world, 0u);
    const uint32_t *first_texture_table =
        ref_agc_gpu_world_cache_texture_table(world, first);
    assert(memcmp(first_texture_table + REF_AGC_GPU_TEXTURE_DESCRIPTOR_DWORDS,
                  world->lightmap_descriptor,
                  sizeof(world->lightmap_descriptor)) == 0);
    assert(memcmp(first_texture_table,
                  first_texture_table + REF_AGC_GPU_TEXTURE_DESCRIPTOR_DWORDS,
                  sizeof(world->lightmap_descriptor)) != 0);

    const RefAgcGpuWorldDraw *second =
        ref_agc_gpu_world_cache_draw(world, 1u);
    assert(second && second->vertex_count == 3u && second->index_count == 3u);
    const uint16_t *second_indices =
        ref_agc_gpu_world_cache_indices(world, second);
    assert(second_indices[0] == 0u && second_indices[1] == 1u &&
           second_indices[2] == 2u);
    const uint32_t *texture_table =
        ref_agc_gpu_world_cache_texture_table(world, second);
    assert(memcmp(texture_table,
                  texture_table + REF_AGC_GPU_TEXTURE_DESCRIPTOR_DWORDS,
                  REF_AGC_GPU_TEXTURE_DESCRIPTOR_DWORDS *
                      sizeof(uint32_t)) == 0);
    assert(ref_agc_gpu_world_cache_apply(world, &view, textures, 0) == 0);

    RefAgcWorldView replacement = view;
    replacement.revision = 2u;
    assert(ref_agc_gpu_world_cache_apply(
        world, &replacement, textures, 0) ==
        REF_AGC_GPU_WORLD_RETIREMENT_REQUIRED);
    assert(ref_agc_gpu_world_cache_apply(
        world, &replacement, textures, 1) == 0);
    assert(ref_agc_gpu_world_cache_refresh_textures(
        world, textures, 0) == REF_AGC_GPU_WORLD_RETIREMENT_REQUIRED);
    assert(ref_agc_gpu_world_cache_refresh_textures(world, textures, 1) == 0);

    RefAgcWorldView cleared = {.revision = 3, .active = 0};
    assert(ref_agc_gpu_world_cache_apply(world, &cleared, textures, 0) ==
           REF_AGC_GPU_WORLD_RETIREMENT_REQUIRED);
    assert(ref_agc_gpu_world_cache_apply(world, &cleared, textures, 1) == 0);
    assert(ref_agc_gpu_world_cache_stats(world, &stats) == 0);
    assert(!stats.active && stats.clears == 1u && stats.resident_bytes == 0u);

    ref_agc_gpu_world_cache_destroy(world);
    ref_agc_gpu_texture_cache_destroy(textures);
    free(world); free(textures);
    free(world_memory); free(texture_memory);
    puts("ref_agc GPU world cache tests passed");
    return 0;
}
