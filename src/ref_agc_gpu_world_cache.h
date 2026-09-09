#ifndef PS5_XASH3D_REF_AGC_GPU_WORLD_CACHE_H
#define PS5_XASH3D_REF_AGC_GPU_WORLD_CACHE_H

#include "ref_agc_gpu_texture_cache.h"
#include "ref_agc_world_store.h"

#include <stddef.h>
#include <stdint.h>

enum {
    REF_AGC_GPU_WORLD_MAX_DRAWS = 8192,
    REF_AGC_GPU_WORLD_TEXTURE_DWORDS = 24,
};

typedef void (*RefAgcGpuWorldFlushFn)(const void *memory, size_t bytes,
                                      void *user);

typedef struct RefAgcGpuWorldDraw {
    size_t vertex_offset;
    size_t index_offset;
    size_t vertex_table_offset;
    size_t texture_table_offset;
    uint32_t vertex_count;
    uint32_t index_count;
    uint32_t texture_handle;
    uint32_t surface_id;
    uint32_t surface_flags;
    uint32_t draw_flags;
} RefAgcGpuWorldDraw;

typedef struct RefAgcGpuWorldStats {
    uint64_t revision;
    uint64_t publishes;
    uint64_t clears;
    uint64_t flushes;
    uint64_t source_hash;
    uint64_t upload_hash;
    uint32_t vertex_count;
    uint32_t index_count;
    uint32_t draw_count;
    uint32_t texture_tables;
    uint32_t lightmap_width;
    uint32_t lightmap_height;
    uint32_t lightmap_row_pitch;
    uint32_t lightmapped_draw_count;
    size_t lightmap_bytes;
    uint64_t lightmap_rgb_sum;
    uint32_t lightmap_nonzero_texels;
    uint8_t lightmap_rgb_min;
    uint8_t lightmap_rgb_max;
    size_t resident_bytes;
    size_t peak_resident_bytes;
    int active;
} RefAgcGpuWorldStats;

typedef struct RefAgcGpuWorldCache {
    uint8_t *base;
    uint64_t gpu_base;
    size_t bytes;
    RefAgcGpuWorldFlushFn flush;
    void *flush_user;
    RefAgcGpuWorldDraw draws[REF_AGC_GPU_WORLD_MAX_DRAWS];
    size_t lightmap_offset;
    uint32_t lightmap_descriptor[REF_AGC_GPU_TEXTURE_DESCRIPTOR_DWORDS];
    RefAgcGpuWorldStats stats;
    int initialized;
} RefAgcGpuWorldCache;

enum RefAgcGpuWorldResult {
    REF_AGC_GPU_WORLD_OK = 0,
    REF_AGC_GPU_WORLD_INVALID = -1,
    REF_AGC_GPU_WORLD_EXHAUSTED = -2,
    REF_AGC_GPU_WORLD_STALE = -3,
    REF_AGC_GPU_WORLD_RETIREMENT_REQUIRED = -4,
    REF_AGC_GPU_WORLD_TEXTURE_MISSING = -5,
    REF_AGC_GPU_WORLD_DESCRIPTOR_FAILED = -6,
};

int ref_agc_gpu_world_cache_init(RefAgcGpuWorldCache *cache, void *base,
                                 uint64_t gpu_base, size_t bytes,
                                 RefAgcGpuWorldFlushFn flush,
                                 void *flush_user);
int ref_agc_gpu_world_cache_apply(RefAgcGpuWorldCache *cache,
                                  const RefAgcWorldView *view,
                                  const RefAgcGpuTextureCache *textures,
                                  int prior_use_retired);
int ref_agc_gpu_world_cache_refresh_textures(
    RefAgcGpuWorldCache *cache, const RefAgcGpuTextureCache *textures,
    int prior_use_retired);
int ref_agc_gpu_world_cache_stats(const RefAgcGpuWorldCache *cache,
                                  RefAgcGpuWorldStats *out);
int ref_agc_gpu_world_cache_validate(const RefAgcGpuWorldCache *cache);
const RefAgcGpuWorldDraw *ref_agc_gpu_world_cache_draw(
    const RefAgcGpuWorldCache *cache, uint32_t draw);
const uint16_t *ref_agc_gpu_world_cache_indices(
    const RefAgcGpuWorldCache *cache, const RefAgcGpuWorldDraw *draw);
const uint32_t *ref_agc_gpu_world_cache_vertex_table(
    const RefAgcGpuWorldCache *cache, const RefAgcGpuWorldDraw *draw);
const uint32_t *ref_agc_gpu_world_cache_texture_table(
    const RefAgcGpuWorldCache *cache, const RefAgcGpuWorldDraw *draw);
void ref_agc_gpu_world_cache_destroy(RefAgcGpuWorldCache *cache);

#endif
