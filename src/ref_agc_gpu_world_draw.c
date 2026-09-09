#include "ref_agc_gpu_world_draw.h"

#include "bsp_resource_draw.h"
#include "ps5_gpu_span.h"

#include <limits.h>

enum {
    REF_AGC_GPU_WORLD_DWORDS_PER_DRAW = 13,
    REF_AGC_GPU_WORLD_CONSTANT_TABLE_DWORDS = 4,
};

int ref_agc_gpu_world_required_dwords(uint32_t draws, uint32_t *out)
{
    if (!out || draws > UINT32_MAX / REF_AGC_GPU_WORLD_DWORDS_PER_DRAW)
        return -1;
    *out = draws * REF_AGC_GPU_WORLD_DWORDS_PER_DRAW;
    return 0;
}

int ref_agc_gpu_world_count_surface_range(
    const RefAgcGpuWorldCache *cache, uint32_t first_surface,
    uint32_t surface_count, uint32_t flag_mask, uint32_t flag_value,
    RefAgcGpuWorldComposeResult *out)
{
    if (!cache || !cache->initialized || !cache->stats.active || !out ||
        surface_count == 0u ||
        first_surface > UINT32_MAX - (surface_count - 1u) ||
        (flag_value & ~flag_mask) != 0u)
        return -1;
    *out = (RefAgcGpuWorldComposeResult){0};
    for (uint32_t i = 0u; i < cache->stats.draw_count; ++i) {
        const RefAgcGpuWorldDraw *draw = &cache->draws[i];
        if (draw->surface_id < first_surface ||
            draw->surface_id - first_surface >= surface_count ||
            (draw->draw_flags & flag_mask) != flag_value)
            continue;
        ++out->draws;
        out->indices += draw->index_count;
    }
    return 0;
}

int ref_agc_gpu_world_compose(
    uint32_t **cursor, uint32_t *end, const RefAgcGpuWorldCache *cache,
    uint32_t flag_mask, uint32_t flag_value,
    const uint32_t *constant_table, const void *gpu_mapping,
    size_t gpu_mapping_bytes, uint64_t modifier,
    BspSetShDirectFn set_sh_direct, BspDrawIndexedFn draw_indexed,
    RefAgcGpuWorldComposeResult *out)
{
    return ref_agc_gpu_world_compose_surface_range(
        cursor, end, cache, 0u, UINT32_MAX, flag_mask, flag_value,
        constant_table, gpu_mapping, gpu_mapping_bytes, modifier,
        set_sh_direct, draw_indexed, out);
}

int ref_agc_gpu_world_compose_surface_range(
    uint32_t **cursor, uint32_t *end, const RefAgcGpuWorldCache *cache,
    uint32_t first_surface, uint32_t surface_count,
    uint32_t flag_mask, uint32_t flag_value,
    const uint32_t *constant_table, const void *gpu_mapping,
    size_t gpu_mapping_bytes, uint64_t modifier,
    BspSetShDirectFn set_sh_direct, BspDrawIndexedFn draw_indexed,
    RefAgcGpuWorldComposeResult *out)
{
    uint32_t selected = 0u, required = 0u;
    if (!cursor || !*cursor || !end || *cursor > end || !cache ||
        !cache->initialized || !cache->stats.active || !constant_table ||
        !gpu_mapping || !gpu_mapping_bytes || !modifier || !set_sh_direct ||
        !draw_indexed || !out || surface_count == 0u ||
        first_surface > UINT32_MAX - (surface_count - 1u) ||
        flag_value & ~flag_mask ||
        ((uintptr_t)constant_table & 15u) != 0u ||
        !ps5_gpu_span_visible(gpu_mapping, gpu_mapping_bytes,
                              constant_table,
                              REF_AGC_GPU_WORLD_CONSTANT_TABLE_DWORDS *
                                  sizeof(uint32_t)))
        return -1;
    for (uint32_t i = 0u; i < cache->stats.draw_count; ++i) {
        const RefAgcGpuWorldDraw *draw = &cache->draws[i];
        if (draw->surface_id < first_surface ||
            draw->surface_id - first_surface >= surface_count)
            continue;
        if ((draw->draw_flags & flag_mask) != flag_value)
            continue;
        const uint16_t *indices = ref_agc_gpu_world_cache_indices(cache, draw);
        const uint32_t *vertex_table =
            ref_agc_gpu_world_cache_vertex_table(cache, draw);
        const uint32_t *texture_table =
            ref_agc_gpu_world_cache_texture_table(cache, draw);
        if (!indices || !vertex_table || !texture_table ||
            ((uintptr_t)vertex_table & 15u) != 0u ||
            ((uintptr_t)texture_table & 15u) != 0u ||
            !ps5_gpu_span_visible(gpu_mapping, gpu_mapping_bytes, indices,
                                  (size_t)draw->index_count *
                                      sizeof(uint16_t)) ||
            !ps5_gpu_span_visible(gpu_mapping, gpu_mapping_bytes,
                                  vertex_table, 4u * sizeof(uint32_t)) ||
            !ps5_gpu_span_visible(
                gpu_mapping, gpu_mapping_bytes, texture_table,
                REF_AGC_GPU_WORLD_TEXTURE_DWORDS * sizeof(uint32_t)))
            return -2;
        ++selected;
    }
    if (ref_agc_gpu_world_required_dwords(selected, &required) != 0 ||
        (size_t)(end - *cursor) < required)
        return -3;
    uint32_t *const start = *cursor;
    RefAgcGpuWorldComposeResult result = {0};
    for (uint32_t i = 0u; i < cache->stats.draw_count; ++i) {
        const RefAgcGpuWorldDraw *draw = &cache->draws[i];
        if (draw->surface_id < first_surface ||
            draw->surface_id - first_surface >= surface_count)
            continue;
        if ((draw->draw_flags & flag_mask) != flag_value)
            continue;
        const uint32_t gs_values[2] = {
            (uint32_t)(uintptr_t)constant_table,
            (uint32_t)(uintptr_t)
                ref_agc_gpu_world_cache_vertex_table(cache, draw),
        };
        const uint32_t ps_value = (uint32_t)(uintptr_t)
            ref_agc_gpu_world_cache_texture_table(cache, draw);
        if (set_sh_direct(cursor, (uint32_t)(end - *cursor),
                          BSP_RESOURCE_GS_SH_OFFSET, gs_values, 2u) != 0 ||
            set_sh_direct(cursor, (uint32_t)(end - *cursor),
                          BSP_RESOURCE_PS_SH_OFFSET, &ps_value, 1u) != 0 ||
            draw_indexed(
                cursor, (uint32_t)(end - *cursor), draw->index_count,
                ref_agc_gpu_world_cache_indices(cache, draw), gpu_mapping,
                gpu_mapping_bytes, modifier) != 0)
            return -4;
        ++result.draws;
        result.indices += draw->index_count;
    }
    result.command_dwords = (uint32_t)(*cursor - start);
    if (result.draws != selected || result.command_dwords != required)
        return -5;
    *out = result;
    return 0;
}
