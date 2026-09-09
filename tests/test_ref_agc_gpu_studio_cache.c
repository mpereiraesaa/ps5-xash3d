#include "../src/ref_agc_gpu_studio_cache.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t flushes;

static void flush(const void *memory, size_t bytes, void *user)
{
    (void)user;
    assert(memory && bytes != 0u);
    ++flushes;
}

int main(void)
{
    _Alignas(256) uint8_t arena[1024] = {0};
    const uint8_t first[300] = {1u};
    const uint8_t update[500] = {2u};
    const uint8_t second[128] = {3u};
    RefAgcGpuStudioCache cache;
    RefAgcGpuStudioEntry entry;
    RefAgcGpuStudioStats stats;
    const uint8_t *data;

    assert(ref_agc_gpu_studio_cache_init(
        &cache, arena, sizeof(arena), flush, NULL) ==
        REF_AGC_GPU_STUDIO_OK);
    assert(ref_agc_gpu_studio_cache_apply(
        &cache, &(RefAgcStudioView){1u, 1u, 11u, "models/a.mdl",
                                   first, sizeof(first), 1}, 1) ==
        REF_AGC_GPU_STUDIO_OK);
    assert(ref_agc_gpu_studio_cache_apply(
        &cache, &(RefAgcStudioView){2u, 2u, 22u, "models/b.mdl",
                                   second, sizeof(second), 1}, 1) ==
        REF_AGC_GPU_STUDIO_OK);
    assert(ref_agc_gpu_studio_cache_apply(
        &cache, &(RefAgcStudioView){1u, 3u, 33u, "models/a.mdl",
                                   update, sizeof(update), 1}, 0) ==
        REF_AGC_GPU_STUDIO_RETIREMENT_REQUIRED);
    assert(ref_agc_gpu_studio_cache_apply(
        &cache, &(RefAgcStudioView){1u, 3u, 33u, "models/a.mdl",
                                   update, sizeof(update), 1}, 1) ==
        REF_AGC_GPU_STUDIO_OK);
    assert(ref_agc_gpu_studio_cache_get(&cache, 1u, &entry, &data) ==
           REF_AGC_GPU_STUDIO_OK);
    assert(entry.source_bytes == sizeof(update) && data[0] == 2u &&
           strcmp(entry.model_name, "models/a.mdl") == 0);
    assert(ref_agc_gpu_studio_cache_apply(
        &cache, &(RefAgcStudioView){2u, 4u, 0u, "models/b.mdl",
                                   NULL, 0u, 0}, 1) ==
        REF_AGC_GPU_STUDIO_OK);
    assert(ref_agc_gpu_studio_cache_validate(&cache) ==
           REF_AGC_GPU_STUDIO_OK);
    assert(ref_agc_gpu_studio_cache_stats(&cache, &stats) ==
           REF_AGC_GPU_STUDIO_OK);
    assert(stats.revision == 4u && stats.creates == 2u &&
           stats.updates == 1u && stats.deletes == 1u &&
           stats.active == 1u && stats.peak_active == 2u && flushes == 3u);
    ref_agc_gpu_studio_cache_destroy(&cache);
    puts("ref_agc GPU studio cache tests passed");
    return 0;
}
