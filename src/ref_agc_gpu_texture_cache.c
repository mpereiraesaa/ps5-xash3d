#include "ref_agc_gpu_texture_cache.h"

#include "ps5_gfx1013_descriptor.h"

#include <limits.h>
#include <string.h>

static int align_size(size_t value, size_t alignment, size_t *out)
{
    if (!out || alignment == 0u || (alignment & (alignment - 1u)) != 0u ||
        value > SIZE_MAX - (alignment - 1u))
        return -1;
    *out = (value + alignment - 1u) & ~(alignment - 1u);
    return 0;
}

static uint64_t hash_words(uint64_t hash, const uint32_t *words, size_t count)
{
    const uint8_t *bytes = (const uint8_t *)words;
    size_t length = count * sizeof(*words);
    while (length-- != 0u) {
        hash ^= *bytes++;
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static int find_space(const RefAgcGpuTextureCache *cache, size_t bytes,
                      uint32_t excluded, size_t *out)
{
    size_t cursor = 0u;
    for (;;) {
        size_t candidate;
        if (align_size(cursor, REF_AGC_GPU_TEXTURE_ALIGNMENT, &candidate) != 0 ||
            candidate > cache->bytes || bytes > cache->bytes - candidate)
            return REF_AGC_GPU_TEXTURE_EXHAUSTED;
        size_t next = SIZE_MAX;
        int collision = 0;
        for (uint32_t i = 0u; i < REF_AGC_TEXTURE_MAX; ++i) {
            const RefAgcGpuTextureEntry *entry = &cache->entries[i];
            if (i == excluded || !entry->active)
                continue;
            const size_t candidate_end = candidate + bytes;
            const size_t entry_end = entry->offset + entry->allocation_bytes;
            if (candidate < entry_end && candidate_end > entry->offset) {
                collision = 1;
                if (entry_end < next)
                    next = entry_end;
            }
        }
        if (!collision) {
            *out = candidate;
            return REF_AGC_GPU_TEXTURE_OK;
        }
        if (next == SIZE_MAX || next <= cursor)
            return REF_AGC_GPU_TEXTURE_EXHAUSTED;
        cursor = next;
    }
}

int ref_agc_gpu_texture_cache_init(RefAgcGpuTextureCache *cache, void *base,
                                   uint64_t gpu_base, size_t bytes,
                                   RefAgcGpuTextureFlushFn flush,
                                   void *flush_user)
{
    if (!cache || !base || !flush || bytes == 0u ||
        (gpu_base & (REF_AGC_GPU_TEXTURE_ALIGNMENT - 1u)) != 0u ||
        gpu_base == 0u || gpu_base >= (UINT64_C(1) << 48) ||
        bytes > (UINT64_C(1) << 48) - gpu_base)
        return REF_AGC_GPU_TEXTURE_INVALID;
    memset(cache, 0, sizeof(*cache));
    cache->base = (uint8_t *)base;
    cache->gpu_base = gpu_base;
    cache->bytes = bytes;
    cache->flush = flush;
    cache->flush_user = flush_user;
    cache->stats.descriptor_hash = UINT64_C(14695981039346656037);
    cache->initialized = 1;
    return REF_AGC_GPU_TEXTURE_OK;
}

int ref_agc_gpu_texture_cache_apply(RefAgcGpuTextureCache *cache,
                                    const RefAgcTextureView *view,
                                    int prior_use_retired)
{
    size_t source_row, source_bytes, row_pitch, allocation_bytes, offset;
    uint32_t descriptor[REF_AGC_GPU_TEXTURE_DESCRIPTOR_DWORDS];
    RefAgcGpuTextureEntry *entry;
    int replacing;
    uint32_t mip_count = 1u;
    size_t mip_offsets[15] = {0}, mip_pitches[15] = {0};
    uint32_t mip_widths[15] = {0}, mip_heights[15] = {0};
    uint32_t storage_widths[15] = {0}, storage_heights[15] = {0};
    if (!cache || !cache->initialized || !view || view->handle == 0u ||
        view->handle > REF_AGC_TEXTURE_MAX || view->revision == 0u)
        return REF_AGC_GPU_TEXTURE_INVALID;
    entry = &cache->entries[view->handle - 1u];
    if (cache->stats.revision > view->revision ||
        entry->revision > view->revision)
        return REF_AGC_GPU_TEXTURE_STALE;
    if (entry->revision == view->revision &&
        cache->stats.revision == view->revision)
        return REF_AGC_GPU_TEXTURE_OK;
    if (cache->stats.revision == view->revision)
        return REF_AGC_GPU_TEXTURE_STALE;
    replacing = entry->active;
    if (!view->active) {
        if (replacing && !prior_use_retired)
            return REF_AGC_GPU_TEXTURE_RETIREMENT_REQUIRED;
        if (replacing) {
            cache->stats.resident_bytes -= entry->allocation_bytes;
            --cache->stats.active;
            ++cache->stats.deletes;
        }
        memset(entry, 0, sizeof(*entry));
        entry->revision = view->revision;
        cache->stats.revision = view->revision;
        return REF_AGC_GPU_TEXTURE_OK;
    }
    if (!view->pixels || view->width == 0u || view->height == 0u ||
        view->depth != 1u || view->width > 16384u || view->height > 16384u)
        return REF_AGC_GPU_TEXTURE_INVALID;
    source_row = (size_t)view->width * 4u;
    if (view->height > SIZE_MAX / source_row)
        return REF_AGC_GPU_TEXTURE_INVALID;
    source_bytes = source_row * view->height;
    if (view->pixel_bytes < source_bytes ||
        align_size(source_row, REF_AGC_GPU_TEXTURE_ALIGNMENT, &row_pitch) != 0 ||
        view->height > SIZE_MAX / row_pitch)
        return REF_AGC_GPU_TEXTURE_INVALID;
    if (view->generate_mips)
        for (uint32_t d = view->width > view->height ? view->width : view->height;
             d > 1u; d >>= 1u)
            ++mip_count;
    /* Same reverse level order and 256-byte pitch as BSP's validated linear
     * AddrLib layout: smallest level at allocation start, level zero last. */
    allocation_bytes = 0u;
    for (uint32_t level = mip_count; level-- > 0u;) {
        mip_widths[level] = view->width >> level;
        mip_heights[level] = view->height >> level;
        if (!mip_widths[level]) mip_widths[level] = 1u;
        if (!mip_heights[level]) mip_heights[level] = 1u;
        /* AddrLib GFX10 linear storage uses ShiftCeil, whereas sampled mip
         * dimensions use floor shifts. NPOT images require both extents. */
        storage_widths[level] = (view->width + (1u << level) - 1u) >> level;
        storage_heights[level] = (view->height + (1u << level) - 1u) >> level;
        if (align_size((size_t)storage_widths[level] * 4u, 256u,
                       &mip_pitches[level]) != 0 ||
            storage_heights[level] > (SIZE_MAX - allocation_bytes) / mip_pitches[level])
            return REF_AGC_GPU_TEXTURE_INVALID;
        mip_offsets[level] = allocation_bytes;
        allocation_bytes += mip_pitches[level] * storage_heights[level];
    }
    if (replacing && !prior_use_retired)
        return REF_AGC_GPU_TEXTURE_RETIREMENT_REQUIRED;
    if (replacing && entry->allocation_bytes >= allocation_bytes)
        offset = entry->offset;
    else {
        const int space_result = find_space(
            cache, allocation_bytes,
            replacing ? view->handle - 1u : REF_AGC_TEXTURE_MAX, &offset);
        if (space_result != REF_AGC_GPU_TEXTURE_OK)
            return space_result;
    }
    if (row_pitch > UINT32_MAX ||
        ps5_gfx1013_build_tsharp_rgba8_mip(
            descriptor, cache->gpu_base + offset, view->width, view->height,
            (uint32_t)row_pitch, mip_count) != 0 ||
        ps5_gfx1013_build_ssharp_mip(
            descriptor + 8,
            view->sampler_clamp ? PS5_GFX1013_CLAMP_LAST_TEXEL
                                : PS5_GFX1013_REPEAT,
            mip_count > 1u ? PS5_GFX1013_FILTER_TRILINEAR : PS5_GFX1013_FILTER_BILINEAR,
            mip_count) != 0)
        return REF_AGC_GPU_TEXTURE_DESCRIPTOR_FAILED;

    uint8_t *destination = cache->base + offset;
    memset(destination, 0, allocation_bytes);
    for (uint32_t row = 0u; row < view->height; ++row)
        memcpy(destination + mip_offsets[0] + (size_t)row * row_pitch,
               view->pixels + (size_t)row * source_row, source_row);
    for (uint32_t level = 1u; level < mip_count; ++level) {
        const uint8_t *previous = destination + mip_offsets[level - 1u];
        uint8_t *current = destination + mip_offsets[level];
        const uint32_t width = mip_widths[level], height = mip_heights[level];
        const uint32_t pw = mip_widths[level - 1u], ph = mip_heights[level - 1u];
        for (uint32_t y = 0u; y < height; ++y)
            for (uint32_t x = 0u; x < width; ++x) {
                const uint32_t x0 = x * pw / width, x1 = (x + 1u) * pw / width;
                const uint32_t y0 = y * ph / height, y1 = (y + 1u) * ph / height;
                const uint32_t samples = (x1 - x0) * (y1 - y0);
                for (uint32_t c = 0u; c < 4u; ++c) {
                    uint32_t sum = 0u;
                    for (uint32_t sy = y0; sy < y1; ++sy)
                        for (uint32_t sx = x0; sx < x1; ++sx)
                            sum += previous[(size_t)sy * mip_pitches[level - 1u] + sx * 4u + c];
                    current[(size_t)y * mip_pitches[level] + x * 4u + c] =
                        (uint8_t)((sum + samples / 2u) / samples);
                }
            }
    }
    /* Initialize the NPOT storage-only fringe by edge extension. Never feed
     * padding back into downsampling of logical texels. */
    for (uint32_t level = 0; level < mip_count; ++level) {
        uint8_t *current = destination + mip_offsets[level];
        for (uint32_t y = 0; y < storage_heights[level]; ++y) {
            uint32_t sy = y < mip_heights[level] ? y : mip_heights[level] - 1u;
            for (uint32_t x = 0; x < storage_widths[level]; ++x) {
                if (y < mip_heights[level] && x < mip_widths[level]) continue;
                uint32_t sx = x < mip_widths[level] ? x : mip_widths[level] - 1u;
                memcpy(current + y * mip_pitches[level] + x * 4u,
                       current + sy * mip_pitches[level] + sx * 4u, 4u);
            }
        }
    }
    cache->flush(destination, allocation_bytes, cache->flush_user);

    if (replacing) {
        cache->stats.resident_bytes -= entry->allocation_bytes;
        ++cache->stats.updates;
    } else {
        ++cache->stats.creates;
        ++cache->stats.active;
        if (cache->stats.active > cache->stats.peak_active)
            cache->stats.peak_active = cache->stats.active;
    }
    *entry = (RefAgcGpuTextureEntry){
        .offset = offset,
        .allocation_bytes = allocation_bytes,
        .source_bytes = source_bytes,
        .revision = view->revision,
        .content_hash = view->content_hash,
        .width = view->width,
        .height = view->height,
        .row_pitch = (uint32_t)row_pitch,
        .mip_count = mip_count,
        .active = 1,
    };
    memcpy(entry->descriptor, descriptor, sizeof(descriptor));
    cache->stats.resident_bytes += allocation_bytes;
    if (cache->stats.resident_bytes > cache->stats.peak_resident_bytes)
        cache->stats.peak_resident_bytes = cache->stats.resident_bytes;
    ++cache->stats.flushes;
    cache->stats.source_bytes_copied += source_bytes;
    cache->stats.descriptor_hash = hash_words(
        cache->stats.descriptor_hash, descriptor,
        REF_AGC_GPU_TEXTURE_DESCRIPTOR_DWORDS);
    cache->stats.revision = view->revision;
    return REF_AGC_GPU_TEXTURE_OK;
}

int ref_agc_gpu_texture_cache_get(const RefAgcGpuTextureCache *cache,
                                  uint32_t handle,
                                  RefAgcGpuTextureEntry *out)
{
    if (!cache || !cache->initialized || !out || handle == 0u ||
        handle > REF_AGC_TEXTURE_MAX || !cache->entries[handle - 1u].active)
        return REF_AGC_GPU_TEXTURE_INVALID;
    *out = cache->entries[handle - 1u];
    return REF_AGC_GPU_TEXTURE_OK;
}

int ref_agc_gpu_texture_cache_stats(const RefAgcGpuTextureCache *cache,
                                    RefAgcGpuTextureStats *out)
{
    if (!cache || !cache->initialized || !out)
        return REF_AGC_GPU_TEXTURE_INVALID;
    *out = cache->stats;
    return REF_AGC_GPU_TEXTURE_OK;
}

int ref_agc_gpu_texture_cache_validate(const RefAgcGpuTextureCache *cache)
{
    uint32_t active = 0u;
    size_t resident = 0u;
    if (!cache || !cache->initialized)
        return REF_AGC_GPU_TEXTURE_INVALID;
    for (uint32_t i = 0u; i < REF_AGC_TEXTURE_MAX; ++i) {
        const RefAgcGpuTextureEntry *left = &cache->entries[i];
        if (!left->active)
            continue;
        if (left->offset > cache->bytes ||
            left->allocation_bytes > cache->bytes - left->offset ||
            (left->offset & (REF_AGC_GPU_TEXTURE_ALIGNMENT - 1u)) != 0u)
            return REF_AGC_GPU_TEXTURE_INVALID;
        ++active;
        resident += left->allocation_bytes;
        for (uint32_t j = i + 1u; j < REF_AGC_TEXTURE_MAX; ++j) {
            const RefAgcGpuTextureEntry *right = &cache->entries[j];
            if (!right->active)
                continue;
            if (right->offset > cache->bytes ||
                right->allocation_bytes > cache->bytes - right->offset ||
                (right->offset &
                 (REF_AGC_GPU_TEXTURE_ALIGNMENT - 1u)) != 0u)
                return REF_AGC_GPU_TEXTURE_INVALID;
            if (left->offset < right->offset + right->allocation_bytes &&
                left->offset + left->allocation_bytes > right->offset)
                return REF_AGC_GPU_TEXTURE_INVALID;
        }
    }
    return active == cache->stats.active &&
                   resident == cache->stats.resident_bytes
               ? REF_AGC_GPU_TEXTURE_OK
               : REF_AGC_GPU_TEXTURE_INVALID;
}

void ref_agc_gpu_texture_cache_destroy(RefAgcGpuTextureCache *cache)
{
    if (cache)
        memset(cache, 0, sizeof(*cache));
}
