#ifndef REF_AGC_GPU_TEXTURE_CACHE_H
#define REF_AGC_GPU_TEXTURE_CACHE_H

#include "ref_agc_texture_store.h"

#include <stddef.h>
#include <stdint.h>

enum {
    REF_AGC_GPU_TEXTURE_ALIGNMENT = 256,
    REF_AGC_GPU_TEXTURE_DESCRIPTOR_DWORDS = 12,
};

typedef void (*RefAgcGpuTextureFlushFn)(const void *memory, size_t bytes,
                                        void *user);

typedef struct RefAgcGpuTextureEntry {
    size_t offset;
    size_t allocation_bytes;
    size_t source_bytes;
    uint64_t revision;
    uint64_t content_hash;
    uint32_t descriptor[REF_AGC_GPU_TEXTURE_DESCRIPTOR_DWORDS];
    uint32_t width;
    uint32_t height;
    uint32_t row_pitch;
    int active;
} RefAgcGpuTextureEntry;

typedef struct RefAgcGpuTextureStats {
    uint64_t revision;
    uint64_t creates;
    uint64_t updates;
    uint64_t deletes;
    uint64_t flushes;
    uint64_t source_bytes_copied;
    uint64_t descriptor_hash;
    uint32_t active;
    uint32_t peak_active;
    size_t resident_bytes;
    size_t peak_resident_bytes;
} RefAgcGpuTextureStats;

typedef struct RefAgcGpuTextureCache {
    uint8_t *base;
    uint64_t gpu_base;
    size_t bytes;
    RefAgcGpuTextureFlushFn flush;
    void *flush_user;
    RefAgcGpuTextureEntry entries[REF_AGC_TEXTURE_MAX];
    RefAgcGpuTextureStats stats;
    int initialized;
} RefAgcGpuTextureCache;

enum RefAgcGpuTextureResult {
    REF_AGC_GPU_TEXTURE_OK = 0,
    REF_AGC_GPU_TEXTURE_INVALID = -1,
    REF_AGC_GPU_TEXTURE_EXHAUSTED = -2,
    REF_AGC_GPU_TEXTURE_STALE = -3,
    REF_AGC_GPU_TEXTURE_RETIREMENT_REQUIRED = -4,
    REF_AGC_GPU_TEXTURE_DESCRIPTOR_FAILED = -5,
};

int ref_agc_gpu_texture_cache_init(RefAgcGpuTextureCache *cache, void *base,
                                   uint64_t gpu_base, size_t bytes,
                                   RefAgcGpuTextureFlushFn flush,
                                   void *flush_user);
int ref_agc_gpu_texture_cache_apply(RefAgcGpuTextureCache *cache,
                                    const RefAgcTextureView *view,
                                    int prior_use_retired);
int ref_agc_gpu_texture_cache_get(const RefAgcGpuTextureCache *cache,
                                  uint32_t handle,
                                  RefAgcGpuTextureEntry *out);
int ref_agc_gpu_texture_cache_stats(const RefAgcGpuTextureCache *cache,
                                    RefAgcGpuTextureStats *out);
int ref_agc_gpu_texture_cache_validate(const RefAgcGpuTextureCache *cache);
void ref_agc_gpu_texture_cache_destroy(RefAgcGpuTextureCache *cache);

#endif
