#ifndef PS5_XASH3D_REF_AGC_GPU_WORLD_DRAW_H
#define PS5_XASH3D_REF_AGC_GPU_WORLD_DRAW_H

#include "bsp_flat_draw.h"
#include "ref_agc_gpu_world_cache.h"

typedef struct RefAgcGpuWorldComposeResult {
    uint32_t draws;
    uint32_t indices;
    uint32_t command_dwords;
} RefAgcGpuWorldComposeResult;

int ref_agc_gpu_world_required_dwords(uint32_t draws, uint32_t *out);
int ref_agc_gpu_world_compose(
    uint32_t **cursor, uint32_t *end, const RefAgcGpuWorldCache *cache,
    uint32_t flag_mask, uint32_t flag_value,
    const uint32_t *constant_table, const void *gpu_mapping,
    size_t gpu_mapping_bytes, uint64_t modifier,
    BspSetShDirectFn set_sh_direct, BspDrawIndexedFn draw_indexed,
    RefAgcGpuWorldComposeResult *out);

#endif
