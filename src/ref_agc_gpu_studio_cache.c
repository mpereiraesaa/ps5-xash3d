#include "ref_agc_gpu_studio_cache.h"

#include <string.h>

static int align_size(size_t value, size_t *out)
{
    if (!out || value > SIZE_MAX - (REF_AGC_GPU_STUDIO_ALIGNMENT - 1u))
        return -1;
    *out = (value + REF_AGC_GPU_STUDIO_ALIGNMENT - 1u) &
           ~(size_t)(REF_AGC_GPU_STUDIO_ALIGNMENT - 1u);
    return 0;
}

static int find_space(const RefAgcGpuStudioCache *cache, size_t bytes,
                      uint32_t excluded, size_t *out)
{
    size_t cursor = 0u;
    for (;;) {
        size_t candidate;
        if (align_size(cursor, &candidate) != 0 || candidate > cache->bytes ||
            bytes > cache->bytes - candidate)
            return REF_AGC_GPU_STUDIO_EXHAUSTED;
        int collision = 0;
        size_t next = SIZE_MAX;
        for (uint32_t index = 0u; index < REF_AGC_STUDIO_MAX; ++index) {
            const RefAgcGpuStudioEntry *entry = &cache->entries[index];
            if (index == excluded || !entry->active)
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
            return REF_AGC_GPU_STUDIO_OK;
        }
        if (next == SIZE_MAX || next <= cursor)
            return REF_AGC_GPU_STUDIO_EXHAUSTED;
        cursor = next;
    }
}

int ref_agc_gpu_studio_cache_init(RefAgcGpuStudioCache *cache, void *base,
                                  size_t bytes,
                                  RefAgcGpuStudioFlushFn flush, void *user)
{
    if (!cache || !base || !bytes || !flush ||
        ((uintptr_t)base & (REF_AGC_GPU_STUDIO_ALIGNMENT - 1u)) != 0u)
        return REF_AGC_GPU_STUDIO_INVALID;
    memset(cache, 0, sizeof(*cache));
    cache->base = base;
    cache->bytes = bytes;
    cache->flush = flush;
    cache->flush_user = user;
    cache->initialized = 1;
    return REF_AGC_GPU_STUDIO_OK;
}

int ref_agc_gpu_studio_cache_apply(RefAgcGpuStudioCache *cache,
                                   const RefAgcStudioView *view,
                                   int prior_use_retired)
{
    RefAgcGpuStudioEntry *entry;
    size_t allocation_bytes, offset;
    int replacing;
    if (!cache || !cache->initialized || !view || view->handle == 0u ||
        view->handle > REF_AGC_STUDIO_MAX || view->revision == 0u)
        return REF_AGC_GPU_STUDIO_INVALID;
    entry = &cache->entries[view->handle - 1u];
    if (cache->stats.revision > view->revision ||
        entry->revision > view->revision)
        return REF_AGC_GPU_STUDIO_STALE;
    if (entry->revision == view->revision &&
        cache->stats.revision == view->revision)
        return REF_AGC_GPU_STUDIO_OK;
    if (cache->stats.revision == view->revision)
        return REF_AGC_GPU_STUDIO_STALE;
    replacing = entry->active;
    if (!view->active) {
        if (replacing && !prior_use_retired)
            return REF_AGC_GPU_STUDIO_RETIREMENT_REQUIRED;
        if (replacing) {
            cache->stats.resident_bytes -= entry->allocation_bytes;
            --cache->stats.active;
            ++cache->stats.deletes;
        }
        memset(entry, 0, sizeof(*entry));
        entry->revision = view->revision;
        cache->stats.revision = view->revision;
        return REF_AGC_GPU_STUDIO_OK;
    }
    if (!view->data || !view->bytes ||
        align_size(view->bytes, &allocation_bytes) != 0)
        return REF_AGC_GPU_STUDIO_INVALID;
    if (replacing && !prior_use_retired)
        return REF_AGC_GPU_STUDIO_RETIREMENT_REQUIRED;
    if (replacing && entry->allocation_bytes >= allocation_bytes)
        offset = entry->offset;
    else {
        const int result = find_space(
            cache, allocation_bytes,
            replacing ? view->handle - 1u : REF_AGC_STUDIO_MAX, &offset);
        if (result != REF_AGC_GPU_STUDIO_OK)
            return result;
    }
    uint8_t *destination = cache->base + offset;
    memcpy(destination, view->data, view->bytes);
    if (allocation_bytes > view->bytes)
        memset(destination + view->bytes, 0, allocation_bytes - view->bytes);
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
    memset(entry, 0, sizeof(*entry));
    entry->offset = offset;
    entry->allocation_bytes = allocation_bytes;
    entry->source_bytes = view->bytes;
    entry->revision = view->revision;
    entry->content_hash = view->content_hash;
    size_t name_length = 0u;
    while (name_length + 1u < sizeof(entry->model_name) &&
           view->model_name[name_length])
        ++name_length;
    memcpy(entry->model_name, view->model_name, name_length);
    entry->active = 1;
    cache->stats.resident_bytes += allocation_bytes;
    if (cache->stats.resident_bytes > cache->stats.peak_resident_bytes)
        cache->stats.peak_resident_bytes = cache->stats.resident_bytes;
    ++cache->stats.flushes;
    cache->stats.source_bytes_copied += view->bytes;
    cache->stats.revision = view->revision;
    return REF_AGC_GPU_STUDIO_OK;
}

int ref_agc_gpu_studio_cache_get(const RefAgcGpuStudioCache *cache,
                                 uint32_t handle,
                                 RefAgcGpuStudioEntry *entry,
                                 const uint8_t **data)
{
    if (!cache || !cache->initialized || !entry || !data || handle == 0u ||
        handle > REF_AGC_STUDIO_MAX || !cache->entries[handle - 1u].active)
        return REF_AGC_GPU_STUDIO_INVALID;
    *entry = cache->entries[handle - 1u];
    *data = cache->base + entry->offset;
    return REF_AGC_GPU_STUDIO_OK;
}

int ref_agc_gpu_studio_cache_stats(const RefAgcGpuStudioCache *cache,
                                   RefAgcGpuStudioStats *stats)
{
    if (!cache || !cache->initialized || !stats)
        return REF_AGC_GPU_STUDIO_INVALID;
    *stats = cache->stats;
    return REF_AGC_GPU_STUDIO_OK;
}

int ref_agc_gpu_studio_cache_validate(const RefAgcGpuStudioCache *cache)
{
    uint32_t active = 0u;
    size_t resident = 0u;
    if (!cache || !cache->initialized)
        return REF_AGC_GPU_STUDIO_INVALID;
    for (uint32_t left_index = 0u; left_index < REF_AGC_STUDIO_MAX;
         ++left_index) {
        const RefAgcGpuStudioEntry *left = &cache->entries[left_index];
        if (!left->active)
            continue;
        if (!left->source_bytes || left->source_bytes > left->allocation_bytes ||
            left->offset > cache->bytes ||
            left->allocation_bytes > cache->bytes - left->offset ||
            (left->offset & (REF_AGC_GPU_STUDIO_ALIGNMENT - 1u)) != 0u)
            return REF_AGC_GPU_STUDIO_INVALID;
        ++active;
        resident += left->allocation_bytes;
        for (uint32_t right_index = left_index + 1u;
             right_index < REF_AGC_STUDIO_MAX; ++right_index) {
            const RefAgcGpuStudioEntry *right = &cache->entries[right_index];
            if (!right->active)
                continue;
            if (left->offset < right->offset + right->allocation_bytes &&
                left->offset + left->allocation_bytes > right->offset)
                return REF_AGC_GPU_STUDIO_INVALID;
        }
    }
    return active == cache->stats.active && resident == cache->stats.resident_bytes
        ? REF_AGC_GPU_STUDIO_OK : REF_AGC_GPU_STUDIO_INVALID;
}

void ref_agc_gpu_studio_cache_destroy(RefAgcGpuStudioCache *cache)
{
    if (cache)
        memset(cache, 0, sizeof(*cache));
}
