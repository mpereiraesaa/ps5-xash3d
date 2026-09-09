#ifndef PS5_XASH3D_REF_AGC_GPU_STUDIO_CACHE_H
#define PS5_XASH3D_REF_AGC_GPU_STUDIO_CACHE_H

#include "ref_agc_studio_store.h"

#include <stddef.h>
#include <stdint.h>

enum { REF_AGC_GPU_STUDIO_ALIGNMENT = 256 };

typedef void (*RefAgcGpuStudioFlushFn)(const void *memory, size_t bytes,
                                       void *user);

typedef struct RefAgcGpuStudioEntry {
    size_t offset;
    size_t allocation_bytes;
    size_t source_bytes;
    uint64_t revision;
    uint64_t content_hash;
    char model_name[REF_AGC_STUDIO_NAME_MAX];
    int active;
} RefAgcGpuStudioEntry;

typedef struct RefAgcGpuStudioStats {
    uint64_t revision;
    uint64_t creates;
    uint64_t updates;
    uint64_t deletes;
    uint64_t flushes;
    uint64_t source_bytes_copied;
    uint32_t active;
    uint32_t peak_active;
    size_t resident_bytes;
    size_t peak_resident_bytes;
} RefAgcGpuStudioStats;

typedef struct RefAgcGpuStudioCache {
    uint8_t *base;
    size_t bytes;
    RefAgcGpuStudioFlushFn flush;
    void *flush_user;
    RefAgcGpuStudioEntry entries[REF_AGC_STUDIO_MAX];
    RefAgcGpuStudioStats stats;
    int initialized;
} RefAgcGpuStudioCache;

enum RefAgcGpuStudioResult {
    REF_AGC_GPU_STUDIO_OK = 0,
    REF_AGC_GPU_STUDIO_INVALID = -1,
    REF_AGC_GPU_STUDIO_EXHAUSTED = -2,
    REF_AGC_GPU_STUDIO_STALE = -3,
    REF_AGC_GPU_STUDIO_RETIREMENT_REQUIRED = -4,
};

int ref_agc_gpu_studio_cache_init(RefAgcGpuStudioCache *cache, void *base,
                                  size_t bytes,
                                  RefAgcGpuStudioFlushFn flush, void *user);
int ref_agc_gpu_studio_cache_apply(RefAgcGpuStudioCache *cache,
                                   const RefAgcStudioView *view,
                                   int prior_use_retired);
int ref_agc_gpu_studio_cache_get(const RefAgcGpuStudioCache *cache,
                                 uint32_t handle,
                                 RefAgcGpuStudioEntry *entry,
                                 const uint8_t **data);
int ref_agc_gpu_studio_cache_stats(const RefAgcGpuStudioCache *cache,
                                   RefAgcGpuStudioStats *stats);
int ref_agc_gpu_studio_cache_validate(const RefAgcGpuStudioCache *cache);
void ref_agc_gpu_studio_cache_destroy(RefAgcGpuStudioCache *cache);

#endif
