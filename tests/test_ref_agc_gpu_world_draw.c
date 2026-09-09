#include "ref_agc_gpu_world_draw.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int set_direct(uint32_t **cursor, uint32_t capacity, uint32_t offset,
                      const uint32_t *values, uint32_t count)
{
    if (capacity < count + 2u) return -1;
    *(*cursor)++ = offset; *(*cursor)++ = count;
    memcpy(*cursor, values, count * sizeof(*values));
    *cursor += count;
    return 0;
}

static int draw_indexed(uint32_t **cursor, uint32_t capacity,
                        uint32_t count, const uint16_t *indices,
                        const void *mapping, size_t bytes, uint64_t modifier)
{
    (void)indices; (void)mapping; (void)bytes; (void)modifier;
    if (capacity < 6u) return -1;
    for (unsigned i = 0u; i < 6u; ++i) *(*cursor)++ = count + i;
    return 0;
}

int main(void)
{
    uint8_t *memory = aligned_alloc(256u, 4096u);
    RefAgcGpuWorldCache cache = {0};
    uint32_t commands[64] = {0}, *cursor = commands;
    RefAgcGpuWorldComposeResult result;
    assert(memory);
    cache.base = memory;
    cache.gpu_base = UINT64_C(0x200000000);
    cache.bytes = 4096u;
    cache.initialized = cache.stats.active = 1;
    cache.stats.draw_count = 2u;
    cache.draws[0] = (RefAgcGpuWorldDraw){
        .index_offset = 0, .vertex_table_offset = 16,
        .texture_table_offset = 32, .index_count = 3, .surface_id = 10,
    };
    cache.draws[1] = (RefAgcGpuWorldDraw){
        .index_offset = 128, .vertex_table_offset = 144,
        .texture_table_offset = 160, .index_count = 6,
        .draw_flags = REF_AGC_WORLD_DRAW_ALPHA_TEST, .surface_id = 20,
    };
    const uint32_t *constant_table = (const uint32_t *)(memory + 512);
    assert(ref_agc_gpu_world_compose(
        &cursor, commands + 64, &cache, REF_AGC_WORLD_DRAW_ALPHA_TEST, 0,
        constant_table, memory, 4096u, 1u, set_direct, draw_indexed,
        &result) == 0);
    assert(result.draws == 1u && result.indices == 3u &&
           result.command_dwords == 13u);
    assert(ref_agc_gpu_world_compose(
        &cursor, commands + 64, &cache, REF_AGC_WORLD_DRAW_ALPHA_TEST,
        REF_AGC_WORLD_DRAW_ALPHA_TEST, constant_table, memory, 4096u, 1u,
        set_direct, draw_indexed, &result) == 0);
    assert(result.draws == 1u && result.indices == 6u &&
           result.command_dwords == 13u);
    assert(ref_agc_gpu_world_count_surface_range(
        &cache, 20u, 1u, REF_AGC_WORLD_DRAW_ALPHA_TEST,
        REF_AGC_WORLD_DRAW_ALPHA_TEST, &result) == 0);
    assert(result.draws == 1u && result.indices == 6u);
    assert(ref_agc_gpu_world_count_surface_range(
        &cache, 11u, 9u, 0u, 0u, &result) == 0);
    assert(result.draws == 0u && result.indices == 0u);
    assert(ref_agc_gpu_world_compose_surface_range(
        &cursor, commands + 64, &cache, 20u, 1u,
        REF_AGC_WORLD_DRAW_ALPHA_TEST, REF_AGC_WORLD_DRAW_ALPHA_TEST,
        constant_table, memory, 4096u, 1u, set_direct, draw_indexed,
        &result) == 0);
    assert(result.draws == 1u && result.indices == 6u);
    assert(ref_agc_gpu_world_compose_surface_range(
        &cursor, commands + 64, &cache, 10u, 1u,
        REF_AGC_WORLD_DRAW_ALPHA_TEST, REF_AGC_WORLD_DRAW_ALPHA_TEST,
        constant_table, memory, 4096u, 1u, set_direct, draw_indexed,
        &result) == 0);
    assert(result.draws == 0u && result.indices == 0u &&
           result.command_dwords == 0u);
    free(memory);
    puts("ref_agc GPU world draw tests passed");
    return 0;
}
