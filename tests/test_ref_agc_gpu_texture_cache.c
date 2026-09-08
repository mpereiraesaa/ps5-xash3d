#include "ref_agc_gpu_texture_cache.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct FlushLog {
    const void *memory;
    size_t bytes;
    unsigned calls;
} FlushLog;

static void flush_memory(const void *memory, size_t bytes, void *user)
{
    FlushLog *log = (FlushLog *)user;
    log->memory = memory;
    log->bytes = bytes;
    ++log->calls;
}

int main(void)
{
    uint8_t arena[4096];
    uint8_t pixels_a[3u * 2u * 4u];
    uint8_t pixels_b[4u * 1u * 4u];
    memset(pixels_a, 0x35, sizeof(pixels_a));
    memset(pixels_b, 0xa7, sizeof(pixels_b));
    FlushLog flush = {0};
    RefAgcGpuTextureCache cache;
    RefAgcGpuTextureEntry entry;
    RefAgcGpuTextureStats stats;
    RefAgcTextureView view = {
        .handle = 1,
        .width = 3,
        .height = 2,
        .depth = 1,
        .mip_count = 1,
        .revision = 1,
        .content_hash = 0x1234,
        .pixel_bytes = sizeof(pixels_a),
        .pixels = pixels_a,
        .active = 1,
    };
    assert(ref_agc_gpu_texture_cache_init(
        &cache, arena, UINT64_C(0x200000000), sizeof(arena),
        flush_memory, &flush) == 0);
    assert(ref_agc_gpu_texture_cache_apply(&cache, &view, 0) == 0);
    assert(flush.calls == 1u && flush.memory == arena && flush.bytes == 512u);
    assert(ref_agc_gpu_texture_cache_get(&cache, 1, &entry) == 0);
    assert(entry.offset == 0u && entry.row_pitch == 256u &&
           entry.allocation_bytes == 512u && entry.source_bytes == 24u);
    assert(memcmp(arena, pixels_a, 12u) == 0);
    assert(memcmp(arena + 256u, pixels_a + 12u, 12u) == 0);
    assert(entry.descriptor[0] == UINT32_C(0x02000000));

    view.handle = 2;
    view.width = 4;
    view.height = 1;
    view.revision = 2;
    view.content_hash = 0x5678;
    view.pixel_bytes = sizeof(pixels_b);
    view.pixels = pixels_b;
    view.sampler_clamp = 1;
    assert(ref_agc_gpu_texture_cache_apply(&cache, &view, 0) == 0);
    assert(ref_agc_gpu_texture_cache_get(&cache, 2, &entry) == 0);
    assert(entry.offset == 512u && entry.allocation_bytes == 256u);
    assert((entry.descriptor[8] & 0x1ffu) != 0u);

    view.width = 2;
    view.revision = 3;
    view.pixel_bytes = 8u;
    assert(ref_agc_gpu_texture_cache_apply(&cache, &view, 0) ==
           REF_AGC_GPU_TEXTURE_RETIREMENT_REQUIRED);
    assert(ref_agc_gpu_texture_cache_apply(&cache, &view, 1) == 0);
    assert(ref_agc_gpu_texture_cache_get(&cache, 2, &entry) == 0);
    assert(entry.offset == 512u && entry.source_bytes == 8u);

    view.handle = 1;
    view.revision = 4;
    view.active = 0;
    view.pixels = NULL;
    assert(ref_agc_gpu_texture_cache_apply(&cache, &view, 0) ==
           REF_AGC_GPU_TEXTURE_RETIREMENT_REQUIRED);
    assert(ref_agc_gpu_texture_cache_apply(&cache, &view, 1) == 0);
    assert(ref_agc_gpu_texture_cache_get(&cache, 1, &entry) != 0);
    assert(ref_agc_gpu_texture_cache_apply(&cache, &view, 1) == 0);

    assert(ref_agc_gpu_texture_cache_stats(&cache, &stats) == 0);
    assert(stats.revision == 4u && stats.creates == 2u &&
           stats.updates == 1u && stats.deletes == 1u &&
           stats.flushes == 3u && stats.active == 1u &&
           stats.peak_active == 2u && stats.resident_bytes == 256u &&
           stats.peak_resident_bytes == 768u &&
           stats.source_bytes_copied == 48u && stats.descriptor_hash != 0u);
    assert(ref_agc_gpu_texture_cache_validate(&cache) == 0);

    view.revision = 2;
    assert(ref_agc_gpu_texture_cache_apply(&cache, &view, 1) ==
           REF_AGC_GPU_TEXTURE_STALE);
    view.handle = 3;
    view.active = 1;
    assert(ref_agc_gpu_texture_cache_apply(&cache, &view, 1) ==
           REF_AGC_GPU_TEXTURE_STALE);
    ref_agc_gpu_texture_cache_destroy(&cache);
    puts("ref_agc GPU texture cache tests passed");
    return 0;
}
