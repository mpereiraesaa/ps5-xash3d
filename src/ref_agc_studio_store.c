#include "ref_agc_studio_store.h"

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
    while (length < REF_AGC_STUDIO_NAME_MAX && name[length])
        ++length;
    return length;
}

static uint64_t hash_bytes(const void *data, size_t bytes)
{
    const uint8_t *cursor = data;
    uint64_t hash = UINT64_C(14695981039346656037);
    while (bytes--) {
        hash ^= *cursor++;
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static int find_name_locked(const RefAgcStudioStore *store,
                            const char *model_name)
{
    for (uint32_t index = 0u; index < store->stats.handles_issued; ++index)
        if (store->entries[index].active &&
            strcmp(store->entries[index].model_name, model_name) == 0)
            return (int)index;
    return -1;
}

static int handle_index_locked(const RefAgcStudioStore *store,
                               uint32_t handle)
{
    if (handle == 0u || handle > store->stats.handles_issued ||
        !store->entries[handle - 1u].active)
        return -1;
    return (int)(handle - 1u);
}

static void make_view(const RefAgcStudioEntry *entry, uint32_t handle,
                      RefAgcStudioView *view)
{
    memset(view, 0, sizeof(*view));
    view->handle = handle;
    view->revision = entry->revision;
    view->content_hash = entry->content_hash;
    view->model_name = entry->model_name;
    view->data = entry->data;
    view->bytes = entry->bytes;
    view->active = entry->active;
}

int ref_agc_studio_store_init(RefAgcStudioStore *store,
                              const RefAgcStudioAllocator *allocator)
{
    if (!store || (allocator && (!allocator->alloc || !allocator->free)))
        return REF_AGC_STUDIO_INVALID;
    memset(store, 0, sizeof(*store));
    if (pthread_mutex_init(&store->lock, NULL) != 0)
        return REF_AGC_STUDIO_INVALID;
    if (allocator)
        store->allocator = *allocator;
    else {
        store->allocator.alloc = default_alloc;
        store->allocator.free = default_free;
    }
    store->initialized = 1;
    return REF_AGC_STUDIO_OK;
}

void ref_agc_studio_store_destroy(RefAgcStudioStore *store)
{
    if (!store || !store->initialized)
        return;
    if (pthread_mutex_lock(&store->lock) == 0) {
        for (uint32_t index = 0u; index < store->stats.handles_issued;
             ++index)
            if (store->entries[index].data)
                store->allocator.free(store->entries[index].data,
                                      store->allocator.user);
        (void)pthread_mutex_unlock(&store->lock);
    }
    (void)pthread_mutex_destroy(&store->lock);
    memset(store, 0, sizeof(*store));
}

int ref_agc_studio_store_upsert(RefAgcStudioStore *store,
                                const RefAgcStudioInput *input,
                                uint32_t *out_handle)
{
    uint8_t *copy;
    int index;
    const size_t name_length = input ?
        bounded_name_length(input->model_name) : 0u;
    if (!store || !store->initialized || !input || !out_handle ||
        name_length == 0u || name_length >= REF_AGC_STUDIO_NAME_MAX ||
        !input->data || input->bytes == 0u)
        return REF_AGC_STUDIO_INVALID;
    copy = store->allocator.alloc(input->bytes, store->allocator.user);
    if (!copy)
        return REF_AGC_STUDIO_NO_MEMORY;
    memcpy(copy, input->data, input->bytes);
    if (pthread_mutex_lock(&store->lock) != 0) {
        store->allocator.free(copy, store->allocator.user);
        return REF_AGC_STUDIO_INVALID;
    }
    index = find_name_locked(store, input->model_name);
    if (index < 0) {
        for (uint32_t candidate = 0u;
             candidate < store->stats.handles_issued; ++candidate)
            if (!store->entries[candidate].active) {
                index = (int)candidate;
                break;
            }
    }
    if (index < 0 && store->stats.handles_issued < REF_AGC_STUDIO_MAX)
        index = (int)store->stats.handles_issued++;
    if (index < 0) {
        (void)pthread_mutex_unlock(&store->lock);
        store->allocator.free(copy, store->allocator.user);
        return REF_AGC_STUDIO_FULL;
    }
    RefAgcStudioEntry *entry = &store->entries[index];
    if (entry->active) {
        store->stats.resident_bytes -= entry->bytes;
        store->allocator.free(entry->data, store->allocator.user);
        ++store->stats.updates;
    } else {
        ++store->stats.creates;
        ++store->stats.active;
    }
    memset(entry, 0, sizeof(*entry));
    memcpy(entry->model_name, input->model_name, name_length);
    entry->data = copy;
    entry->bytes = input->bytes;
    entry->content_hash = hash_bytes(copy, input->bytes);
    entry->revision = ++store->stats.revision;
    entry->active = 1;
    store->stats.resident_bytes += input->bytes;
    if (store->stats.resident_bytes > store->stats.peak_resident_bytes)
        store->stats.peak_resident_bytes = store->stats.resident_bytes;
    if (store->stats.active > store->stats.peak_active)
        store->stats.peak_active = store->stats.active;
    *out_handle = (uint32_t)index + 1u;
    (void)pthread_mutex_unlock(&store->lock);
    return REF_AGC_STUDIO_OK;
}

int ref_agc_studio_store_free(RefAgcStudioStore *store, uint32_t handle)
{
    int index;
    if (!store || !store->initialized)
        return REF_AGC_STUDIO_INVALID;
    if (pthread_mutex_lock(&store->lock) != 0)
        return REF_AGC_STUDIO_INVALID;
    index = handle_index_locked(store, handle);
    if (index < 0) {
        (void)pthread_mutex_unlock(&store->lock);
        return REF_AGC_STUDIO_NOT_FOUND;
    }
    RefAgcStudioEntry *entry = &store->entries[index];
    store->stats.resident_bytes -= entry->bytes;
    store->allocator.free(entry->data, store->allocator.user);
    entry->data = NULL;
    entry->bytes = 0u;
    entry->content_hash = 0u;
    entry->revision = ++store->stats.revision;
    entry->active = 0;
    ++store->stats.frees;
    --store->stats.active;
    (void)pthread_mutex_unlock(&store->lock);
    return REF_AGC_STUDIO_OK;
}

int ref_agc_studio_store_free_name(RefAgcStudioStore *store,
                                   const char *model_name)
{
    uint32_t handle;
    const int result = ref_agc_studio_store_find(store, model_name, &handle);
    return result == REF_AGC_STUDIO_OK ?
        ref_agc_studio_store_free(store, handle) : result;
}

int ref_agc_studio_store_find(RefAgcStudioStore *store,
                              const char *model_name,
                              uint32_t *out_handle)
{
    int index;
    if (!store || !store->initialized || !model_name || !out_handle)
        return REF_AGC_STUDIO_INVALID;
    if (pthread_mutex_lock(&store->lock) != 0)
        return REF_AGC_STUDIO_INVALID;
    index = find_name_locked(store, model_name);
    if (index >= 0)
        *out_handle = (uint32_t)index + 1u;
    (void)pthread_mutex_unlock(&store->lock);
    return index >= 0 ? REF_AGC_STUDIO_OK : REF_AGC_STUDIO_NOT_FOUND;
}

int ref_agc_studio_store_visit_changed(RefAgcStudioStore *store,
                                       uint64_t after_revision,
                                       RefAgcStudioVisitor visitor,
                                       void *user,
                                       uint64_t *out_revision)
{
    uint64_t cursor = after_revision;
    if (!store || !store->initialized || !visitor || !out_revision)
        return REF_AGC_STUDIO_INVALID;
    if (pthread_mutex_lock(&store->lock) != 0)
        return REF_AGC_STUDIO_INVALID;
    if (after_revision > store->stats.revision) {
        (void)pthread_mutex_unlock(&store->lock);
        return REF_AGC_STUDIO_INVALID;
    }
    while (cursor < store->stats.revision) {
        uint32_t selected = REF_AGC_STUDIO_MAX;
        uint64_t selected_revision = UINT64_MAX;
        for (uint32_t index = 0u; index < store->stats.handles_issued;
             ++index) {
            const uint64_t revision = store->entries[index].revision;
            if (revision > cursor && revision < selected_revision) {
                selected = index;
                selected_revision = revision;
            }
        }
        if (selected == REF_AGC_STUDIO_MAX)
            break;
        RefAgcStudioView view;
        make_view(&store->entries[selected], selected + 1u, &view);
        if (visitor(&view, user) != 0) {
            *out_revision = cursor;
            (void)pthread_mutex_unlock(&store->lock);
            return REF_AGC_STUDIO_VISITOR_FAILED;
        }
        cursor = selected_revision;
    }
    *out_revision = store->stats.revision;
    (void)pthread_mutex_unlock(&store->lock);
    return REF_AGC_STUDIO_OK;
}

int ref_agc_studio_store_stats(RefAgcStudioStore *store,
                               RefAgcStudioStats *out)
{
    if (!store || !store->initialized || !out)
        return REF_AGC_STUDIO_INVALID;
    if (pthread_mutex_lock(&store->lock) != 0)
        return REF_AGC_STUDIO_INVALID;
    *out = store->stats;
    (void)pthread_mutex_unlock(&store->lock);
    return REF_AGC_STUDIO_OK;
}
