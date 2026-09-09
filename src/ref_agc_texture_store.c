#include "ref_agc_texture_store.h"

#include <stdlib.h>
#include <string.h>

static void *default_alloc(size_t bytes, void *user)
{
    (void)user;
    return malloc(bytes);
}

static void default_free(void *memory, void *user)
{
    (void)user;
    free(memory);
}

static size_t bounded_name_length(const char *name)
{
    size_t length = 0u;
    if (!name)
        return 0u;
    while (length < REF_AGC_TEXTURE_NAME_MAX && name[length] != '\0')
        ++length;
    return length;
}

static uint64_t hash_bytes(const void *data, size_t bytes)
{
    const uint8_t *cursor = (const uint8_t *)data;
    uint64_t hash = UINT64_C(14695981039346656037);
    while (bytes-- != 0u) {
        hash ^= *cursor++;
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static int validate_input(const RefAgcTextureInput *input)
{
    const size_t name_length = input ? bounded_name_length(input->name) : 0u;
    if (!input || name_length == 0u ||
        name_length >= REF_AGC_TEXTURE_NAME_MAX || input->width == 0u ||
        input->height == 0u || input->depth == 0u ||
        input->pixel_bytes == 0u)
        return REF_AGC_TEXTURE_INVALID;
    return REF_AGC_TEXTURE_OK;
}

static int find_name_locked(const RefAgcTextureStore *store, const char *name)
{
    for (uint32_t i = 0u; i < store->stats.handles_issued; ++i) {
        if (store->entries[i].active &&
            strcmp(store->entries[i].name, name) == 0)
            return (int)i;
    }
    return -1;
}

static int handle_index_locked(const RefAgcTextureStore *store,
                               uint32_t handle, int require_active)
{
    if (handle == 0u || handle > store->stats.handles_issued)
        return -1;
    const uint32_t index = handle - 1u;
    if (require_active && !store->entries[index].active)
        return -1;
    return (int)index;
}

static void make_view(const RefAgcTextureEntry *entry, uint32_t handle,
                      RefAgcTextureView *out)
{
    memset(out, 0, sizeof(*out));
    out->handle = handle;
    out->width = entry->width;
    out->height = entry->height;
    out->depth = entry->depth;
    out->format = entry->format;
    out->flags = entry->flags;
    out->mip_count = entry->mip_count;
    out->generate_mips = entry->generate_mips;
    out->sampler_clamp = entry->sampler_clamp;
    out->revision = entry->revision;
    out->content_hash = entry->content_hash;
    out->pixel_bytes = entry->pixel_bytes;
    out->pixels = entry->pixels;
    out->name = entry->name;
    out->active = entry->active;
}

int ref_agc_texture_store_init(RefAgcTextureStore *store,
                               const RefAgcTextureAllocator *allocator)
{
    if (!store || (allocator && (!allocator->alloc || !allocator->free)))
        return REF_AGC_TEXTURE_INVALID;
    memset(store, 0, sizeof(*store));
    if (pthread_mutex_init(&store->lock, NULL) != 0)
        return REF_AGC_TEXTURE_INVALID;
    if (allocator)
        store->allocator = *allocator;
    else {
        store->allocator.alloc = default_alloc;
        store->allocator.free = default_free;
    }
    store->initialized = 1;
    return REF_AGC_TEXTURE_OK;
}

void ref_agc_texture_store_destroy(RefAgcTextureStore *store)
{
    if (!store || !store->initialized)
        return;
    if (pthread_mutex_lock(&store->lock) == 0) {
        for (uint32_t i = 0u; i < store->stats.handles_issued; ++i) {
            if (store->entries[i].pixels)
                store->allocator.free(store->entries[i].pixels,
                                      store->allocator.user);
        }
        (void)pthread_mutex_unlock(&store->lock);
    }
    (void)pthread_mutex_destroy(&store->lock);
    memset(store, 0, sizeof(*store));
}

int ref_agc_texture_store_upsert(RefAgcTextureStore *store,
                                 const RefAgcTextureInput *input,
                                 int update, uint32_t *out_handle)
{
    uint8_t *copy;
    int index;
    if (!store || !store->initialized || !out_handle ||
        validate_input(input) != REF_AGC_TEXTURE_OK)
        return REF_AGC_TEXTURE_INVALID;
    if (pthread_mutex_lock(&store->lock) != 0)
        return REF_AGC_TEXTURE_INVALID;
    index = find_name_locked(store, input->name);
    if (index >= 0 && !update) {
        *out_handle = (uint32_t)index + 1u;
        (void)pthread_mutex_unlock(&store->lock);
        return REF_AGC_TEXTURE_OK;
    }
    if (index < 0 && update) {
        (void)pthread_mutex_unlock(&store->lock);
        return REF_AGC_TEXTURE_NOT_FOUND;
    }
    if (index < 0 && store->stats.handles_issued >= REF_AGC_TEXTURE_MAX) {
        (void)pthread_mutex_unlock(&store->lock);
        return REF_AGC_TEXTURE_FULL;
    }
    copy = (uint8_t *)store->allocator.alloc(input->pixel_bytes,
                                             store->allocator.user);
    if (!copy) {
        (void)pthread_mutex_unlock(&store->lock);
        return REF_AGC_TEXTURE_NO_MEMORY;
    }
    if (input->pixels)
        memcpy(copy, input->pixels, input->pixel_bytes);
    else
        memset(copy, 0, input->pixel_bytes);

    if (index < 0) {
        index = (int)store->stats.handles_issued++;
        ++store->stats.creates;
    } else {
        store->stats.resident_bytes -= store->entries[index].pixel_bytes;
        store->allocator.free(store->entries[index].pixels,
                              store->allocator.user);
        ++store->stats.updates;
    }
    RefAgcTextureEntry *entry = &store->entries[index];
    memset(entry, 0, sizeof(*entry));
    memcpy(entry->name, input->name, bounded_name_length(input->name));
    entry->pixels = copy;
    entry->pixel_bytes = input->pixel_bytes;
    entry->width = input->width;
    entry->height = input->height;
    entry->depth = input->depth;
    entry->format = input->format;
    entry->flags = input->flags;
    entry->mip_count = input->mip_count;
    entry->generate_mips = input->generate_mips != 0;
    entry->sampler_clamp = input->sampler_clamp != 0;
    entry->content_hash = hash_bytes(copy, input->pixel_bytes);
    entry->revision = ++store->stats.revision;
    entry->active = 1;
    store->stats.resident_bytes += input->pixel_bytes;
    if (store->stats.resident_bytes > store->stats.peak_resident_bytes)
        store->stats.peak_resident_bytes = store->stats.resident_bytes;
    if (!update)
        ++store->stats.active;
    if (store->stats.active > store->stats.peak_active)
        store->stats.peak_active = store->stats.active;
    *out_handle = (uint32_t)index + 1u;
    (void)pthread_mutex_unlock(&store->lock);
    return REF_AGC_TEXTURE_OK;
}

int ref_agc_texture_store_free(RefAgcTextureStore *store, uint32_t handle)
{
    int index;
    if (!store || !store->initialized)
        return REF_AGC_TEXTURE_INVALID;
    if (pthread_mutex_lock(&store->lock) != 0)
        return REF_AGC_TEXTURE_INVALID;
    index = handle_index_locked(store, handle, 1);
    if (index < 0) {
        (void)pthread_mutex_unlock(&store->lock);
        return REF_AGC_TEXTURE_NOT_FOUND;
    }
    RefAgcTextureEntry *entry = &store->entries[index];
    store->stats.resident_bytes -= entry->pixel_bytes;
    store->allocator.free(entry->pixels, store->allocator.user);
    entry->pixels = NULL;
    entry->pixel_bytes = 0u;
    entry->content_hash = 0u;
    entry->active = 0;
    entry->revision = ++store->stats.revision;
    ++store->stats.frees;
    --store->stats.active;
    (void)pthread_mutex_unlock(&store->lock);
    return REF_AGC_TEXTURE_OK;
}

int ref_agc_texture_store_find(RefAgcTextureStore *store, const char *name,
                               uint32_t *out_handle)
{
    int index;
    if (!store || !store->initialized || !name || !out_handle)
        return REF_AGC_TEXTURE_INVALID;
    if (pthread_mutex_lock(&store->lock) != 0)
        return REF_AGC_TEXTURE_INVALID;
    index = find_name_locked(store, name);
    if (index >= 0)
        *out_handle = (uint32_t)index + 1u;
    (void)pthread_mutex_unlock(&store->lock);
    return index >= 0 ? REF_AGC_TEXTURE_OK : REF_AGC_TEXTURE_NOT_FOUND;
}

int ref_agc_texture_store_get(RefAgcTextureStore *store, uint32_t handle,
                              RefAgcTextureView *out)
{
    int index;
    if (!store || !store->initialized || !out)
        return REF_AGC_TEXTURE_INVALID;
    if (pthread_mutex_lock(&store->lock) != 0)
        return REF_AGC_TEXTURE_INVALID;
    index = handle_index_locked(store, handle, 1);
    if (index >= 0)
        make_view(&store->entries[index], handle, out);
    (void)pthread_mutex_unlock(&store->lock);
    return index >= 0 ? REF_AGC_TEXTURE_OK : REF_AGC_TEXTURE_NOT_FOUND;
}

int ref_agc_texture_store_copy_pixels(RefAgcTextureStore *store,
                                      uint32_t handle, void *destination,
                                      size_t capacity, size_t *out_bytes)
{
    int index;
    if (!store || !store->initialized || !out_bytes)
        return REF_AGC_TEXTURE_INVALID;
    if (pthread_mutex_lock(&store->lock) != 0)
        return REF_AGC_TEXTURE_INVALID;
    index = handle_index_locked(store, handle, 1);
    if (index < 0) {
        (void)pthread_mutex_unlock(&store->lock);
        return REF_AGC_TEXTURE_NOT_FOUND;
    }
    const RefAgcTextureEntry *entry = &store->entries[index];
    *out_bytes = entry->pixel_bytes;
    if (!destination || capacity < entry->pixel_bytes) {
        (void)pthread_mutex_unlock(&store->lock);
        return REF_AGC_TEXTURE_TOO_SMALL;
    }
    memcpy(destination, entry->pixels, entry->pixel_bytes);
    (void)pthread_mutex_unlock(&store->lock);
    return REF_AGC_TEXTURE_OK;
}

int ref_agc_texture_store_visit_changed(RefAgcTextureStore *store,
                                        uint64_t after_revision,
                                        RefAgcTextureVisitor visitor,
                                        void *user,
                                        uint64_t *out_revision)
{
    uint64_t cursor = after_revision;
    if (!store || !store->initialized || !visitor || !out_revision)
        return REF_AGC_TEXTURE_INVALID;
    if (pthread_mutex_lock(&store->lock) != 0)
        return REF_AGC_TEXTURE_INVALID;
    if (after_revision > store->stats.revision) {
        (void)pthread_mutex_unlock(&store->lock);
        return REF_AGC_TEXTURE_INVALID;
    }
    while (cursor < store->stats.revision) {
        uint32_t selected = REF_AGC_TEXTURE_MAX;
        uint64_t selected_revision = UINT64_MAX;
        for (uint32_t i = 0u; i < store->stats.handles_issued; ++i) {
            const uint64_t revision = store->entries[i].revision;
            if (revision > cursor && revision < selected_revision) {
                selected = i;
                selected_revision = revision;
            }
        }
        if (selected == REF_AGC_TEXTURE_MAX)
            break;
        RefAgcTextureView view;
        make_view(&store->entries[selected], selected + 1u, &view);
        if (visitor(&view, user) != 0) {
            *out_revision = cursor;
            (void)pthread_mutex_unlock(&store->lock);
            return REF_AGC_TEXTURE_VISITOR_FAILED;
        }
        cursor = selected_revision;
    }
    *out_revision = store->stats.revision;
    (void)pthread_mutex_unlock(&store->lock);
    return REF_AGC_TEXTURE_OK;
}

int ref_agc_texture_store_stats(RefAgcTextureStore *store,
                                RefAgcTextureStats *out)
{
    if (!store || !store->initialized || !out)
        return REF_AGC_TEXTURE_INVALID;
    if (pthread_mutex_lock(&store->lock) != 0)
        return REF_AGC_TEXTURE_INVALID;
    *out = store->stats;
    (void)pthread_mutex_unlock(&store->lock);
    return REF_AGC_TEXTURE_OK;
}
