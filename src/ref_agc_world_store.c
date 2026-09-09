#include "ref_agc_world_store.h"

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

static int array_bytes(uint32_t count, size_t width, size_t *out)
{
    if (!out || count == 0u || width == 0u ||
        (size_t)count > SIZE_MAX / width)
        return -1;
    *out = (size_t)count * width;
    return 0;
}

static uint64_t hash_bytes(uint64_t hash, const void *data, size_t bytes)
{
    const uint8_t *cursor = (const uint8_t *)data;
    while (bytes--) {
        hash ^= *cursor++;
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static void copy_name(char out[REF_AGC_WORLD_NAME_MAX], const char *name)
{
    size_t length = 0u;
    while (length + 1u < REF_AGC_WORLD_NAME_MAX && name[length])
        ++length;
    memcpy(out, name, length);
    out[length] = '\0';
}

static void release_arrays(RefAgcWorldStore *store)
{
    if (store->vertices)
        store->allocator.free(store->vertices, store->allocator.user);
    if (store->indices)
        store->allocator.free(store->indices, store->allocator.user);
    if (store->draws)
        store->allocator.free(store->draws, store->allocator.user);
    if (store->lightmap_pixels)
        store->allocator.free(store->lightmap_pixels, store->allocator.user);
    store->vertices = NULL;
    store->indices = NULL;
    store->draws = NULL;
    store->lightmap_pixels = NULL;
}

int ref_agc_world_store_init(RefAgcWorldStore *store,
                             const RefAgcWorldAllocator *allocator)
{
    if (!store)
        return REF_AGC_WORLD_INVALID;
    memset(store, 0, sizeof(*store));
    if (allocator) {
        if (!allocator->alloc || !allocator->free)
            return REF_AGC_WORLD_INVALID;
        store->allocator = *allocator;
    } else {
        store->allocator.alloc = default_alloc;
        store->allocator.free = default_free;
    }
    if (pthread_mutex_init(&store->lock, NULL) != 0)
        return REF_AGC_WORLD_INVALID;
    store->initialized = 1;
    return REF_AGC_WORLD_OK;
}

void ref_agc_world_store_destroy(RefAgcWorldStore *store)
{
    if (!store || !store->initialized)
        return;
    release_arrays(store);
    (void)pthread_mutex_destroy(&store->lock);
    memset(store, 0, sizeof(*store));
}

int ref_agc_world_store_publish(RefAgcWorldStore *store,
                                const RefAgcWorldInput *input)
{
    RefAgcWorldVertex *vertices = NULL;
    uint32_t *indices = NULL;
    RefAgcWorldDraw *draws = NULL;
    uint8_t *lightmap_pixels = NULL;
    size_t vertex_bytes, index_bytes, draw_bytes, resident_bytes;
    size_t lightmap_bytes = 0u;
    uint32_t lightmapped_draws = 0u;
    uint32_t sky_draws = 0u;
    uint32_t turbulent_draws = 0u;
    uint64_t hash = UINT64_C(14695981039346656037);
    char model_name[REF_AGC_WORLD_NAME_MAX];
    if (!store || !store->initialized || !input || !input->model_name ||
        !input->model_name[0] || !input->vertices || !input->indices ||
        !input->draws || array_bytes(input->vertex_count,
            sizeof(*vertices), &vertex_bytes) != 0 ||
        array_bytes(input->index_count, sizeof(*indices), &index_bytes) != 0 ||
        array_bytes(input->draw_count, sizeof(*draws), &draw_bytes) != 0 ||
        vertex_bytes > SIZE_MAX - index_bytes ||
        vertex_bytes + index_bytes > SIZE_MAX - draw_bytes)
        return REF_AGC_WORLD_INVALID;
    if (input->lightmap_width || input->lightmap_height ||
        input->lightmap_row_pitch || input->lightmap_pixel_bytes ||
        input->lightmap_pixels) {
        if (!input->lightmap_pixels || !input->lightmap_width ||
            !input->lightmap_height || input->lightmap_width > UINT32_MAX / 4u ||
            input->lightmap_row_pitch < input->lightmap_width * 4u ||
            input->lightmap_height > SIZE_MAX / input->lightmap_row_pitch)
            return REF_AGC_WORLD_INVALID;
        lightmap_bytes = (size_t)input->lightmap_row_pitch *
                         input->lightmap_height;
        if (input->lightmap_pixel_bytes != lightmap_bytes)
            return REF_AGC_WORLD_INVALID;
    }
    if (vertex_bytes + index_bytes + draw_bytes >
        SIZE_MAX - lightmap_bytes)
        return REF_AGC_WORLD_INVALID;
    resident_bytes = vertex_bytes + index_bytes + draw_bytes + lightmap_bytes;
    for (uint32_t draw = 0u; draw < input->draw_count; ++draw) {
        const RefAgcWorldDraw *item = &input->draws[draw];
        if (item->texture_handle == 0u || item->index_count < 3u ||
            item->index_count % 3u != 0u ||
            item->first_index > input->index_count ||
            item->index_count > input->index_count - item->first_index)
            return REF_AGC_WORLD_INVALID;
        if (item->draw_flags & REF_AGC_WORLD_DRAW_LIGHTMAP)
            ++lightmapped_draws;
        if ((item->draw_flags & (REF_AGC_WORLD_DRAW_SKY |
                                 REF_AGC_WORLD_DRAW_TURB)) ==
            (REF_AGC_WORLD_DRAW_SKY | REF_AGC_WORLD_DRAW_TURB))
            return REF_AGC_WORLD_INVALID;
        if (item->draw_flags & REF_AGC_WORLD_DRAW_SKY)
            ++sky_draws;
        if (item->draw_flags & REF_AGC_WORLD_DRAW_TURB)
            ++turbulent_draws;
    }
    if ((lightmapped_draws != 0u) != (lightmap_bytes != 0u))
        return REF_AGC_WORLD_INVALID;
    for (uint32_t index = 0u; index < input->index_count; ++index)
        if (input->indices[index] >= input->vertex_count)
            return REF_AGC_WORLD_INVALID;
    vertices = store->allocator.alloc(vertex_bytes, store->allocator.user);
    if (!vertices)
        goto no_memory;
    indices = store->allocator.alloc(index_bytes, store->allocator.user);
    if (!indices)
        goto no_memory;
    draws = store->allocator.alloc(draw_bytes, store->allocator.user);
    if (!draws)
        goto no_memory;
    if (lightmap_bytes) {
        lightmap_pixels = store->allocator.alloc(
            lightmap_bytes, store->allocator.user);
        if (!lightmap_pixels)
            goto no_memory;
    }
    memcpy(vertices, input->vertices, vertex_bytes);
    memcpy(indices, input->indices, index_bytes);
    memcpy(draws, input->draws, draw_bytes);
    if (lightmap_bytes)
        memcpy(lightmap_pixels, input->lightmap_pixels, lightmap_bytes);
    copy_name(model_name, input->model_name);
    hash = hash_bytes(hash, model_name, sizeof(model_name));
    hash = hash_bytes(hash, &input->model_flags, sizeof(input->model_flags));
    hash = hash_bytes(hash, vertices, vertex_bytes);
    hash = hash_bytes(hash, indices, index_bytes);
    hash = hash_bytes(hash, draws, draw_bytes);
    hash = hash_bytes(hash, &input->lightmap_width,
                      sizeof(input->lightmap_width));
    hash = hash_bytes(hash, &input->lightmap_height,
                      sizeof(input->lightmap_height));
    hash = hash_bytes(hash, &input->lightmap_row_pitch,
                      sizeof(input->lightmap_row_pitch));
    if (lightmap_bytes)
        hash = hash_bytes(hash, lightmap_pixels, lightmap_bytes);

    if (pthread_mutex_lock(&store->lock) != 0)
        goto invalid;
    release_arrays(store);
    store->vertices = vertices;
    store->indices = indices;
    store->draws = draws;
    store->lightmap_pixels = lightmap_pixels;
    copy_name(store->model_name, model_name);
    store->model_flags = input->model_flags;
    ++store->stats.revision;
    ++store->stats.publishes;
    store->stats.content_hash = hash;
    store->stats.vertex_count = input->vertex_count;
    store->stats.index_count = input->index_count;
    store->stats.draw_count = input->draw_count;
    store->stats.lightmap_width = input->lightmap_width;
    store->stats.lightmap_height = input->lightmap_height;
    store->stats.lightmap_row_pitch = input->lightmap_row_pitch;
    store->stats.lightmap_pixel_bytes = lightmap_bytes;
    store->stats.lightmapped_draw_count = lightmapped_draws;
    store->stats.sky_draw_count = sky_draws;
    store->stats.turbulent_draw_count = turbulent_draws;
    store->stats.resident_bytes = resident_bytes;
    if (resident_bytes > store->stats.peak_resident_bytes)
        store->stats.peak_resident_bytes = resident_bytes;
    store->stats.active = 1;
    (void)pthread_mutex_unlock(&store->lock);
    return REF_AGC_WORLD_OK;

no_memory:
    if (vertices) store->allocator.free(vertices, store->allocator.user);
    if (indices) store->allocator.free(indices, store->allocator.user);
    if (draws) store->allocator.free(draws, store->allocator.user);
    if (lightmap_pixels)
        store->allocator.free(lightmap_pixels, store->allocator.user);
    return REF_AGC_WORLD_NO_MEMORY;
invalid:
    store->allocator.free(vertices, store->allocator.user);
    store->allocator.free(indices, store->allocator.user);
    store->allocator.free(draws, store->allocator.user);
    if (lightmap_pixels)
        store->allocator.free(lightmap_pixels, store->allocator.user);
    return REF_AGC_WORLD_INVALID;
}

int ref_agc_world_store_clear(RefAgcWorldStore *store,
                              const char *model_name)
{
    if (!store || !store->initialized || !model_name || !model_name[0])
        return REF_AGC_WORLD_INVALID;
    if (pthread_mutex_lock(&store->lock) != 0)
        return REF_AGC_WORLD_INVALID;
    if (!store->stats.active || strcmp(store->model_name, model_name) != 0) {
        (void)pthread_mutex_unlock(&store->lock);
        return REF_AGC_WORLD_NOT_FOUND;
    }
    release_arrays(store);
    ++store->stats.revision;
    ++store->stats.clears;
    store->stats.content_hash = 0u;
    store->stats.vertex_count = 0u;
    store->stats.index_count = 0u;
    store->stats.draw_count = 0u;
    store->stats.lightmap_width = 0u;
    store->stats.lightmap_height = 0u;
    store->stats.lightmap_row_pitch = 0u;
    store->stats.lightmap_pixel_bytes = 0u;
    store->stats.lightmapped_draw_count = 0u;
    store->stats.sky_draw_count = 0u;
    store->stats.turbulent_draw_count = 0u;
    store->stats.resident_bytes = 0u;
    store->stats.active = 0;
    (void)pthread_mutex_unlock(&store->lock);
    return REF_AGC_WORLD_OK;
}

int ref_agc_world_store_visit_changed(RefAgcWorldStore *store,
                                      uint64_t after_revision,
                                      RefAgcWorldVisitor visitor,
                                      void *user,
                                      uint64_t *out_revision)
{
    RefAgcWorldView view;
    int result = REF_AGC_WORLD_OK;
    if (!store || !store->initialized || !visitor || !out_revision)
        return REF_AGC_WORLD_INVALID;
    if (pthread_mutex_lock(&store->lock) != 0)
        return REF_AGC_WORLD_INVALID;
    if (after_revision > store->stats.revision) {
        result = REF_AGC_WORLD_INVALID;
    } else if (after_revision < store->stats.revision) {
        memset(&view, 0, sizeof(view));
        view.revision = store->stats.revision;
        view.content_hash = store->stats.content_hash;
        copy_name(view.model_name, store->model_name);
        view.model_flags = store->model_flags;
        view.vertices = store->vertices;
        view.vertex_count = store->stats.vertex_count;
        view.indices = store->indices;
        view.index_count = store->stats.index_count;
        view.draws = store->draws;
        view.draw_count = store->stats.draw_count;
        view.lightmap_pixels = store->lightmap_pixels;
        view.lightmap_width = store->stats.lightmap_width;
        view.lightmap_height = store->stats.lightmap_height;
        view.lightmap_row_pitch = store->stats.lightmap_row_pitch;
        view.lightmap_pixel_bytes = store->stats.lightmap_pixel_bytes;
        view.lightmapped_draw_count = store->stats.lightmapped_draw_count;
        view.sky_draw_count = store->stats.sky_draw_count;
        view.turbulent_draw_count = store->stats.turbulent_draw_count;
        view.active = store->stats.active;
        if (visitor(&view, user) != 0)
            result = REF_AGC_WORLD_VISITOR_FAILED;
        else
            *out_revision = store->stats.revision;
    } else {
        *out_revision = store->stats.revision;
    }
    (void)pthread_mutex_unlock(&store->lock);
    return result;
}

int ref_agc_world_store_stats(RefAgcWorldStore *store,
                              RefAgcWorldStats *out)
{
    if (!store || !store->initialized || !out)
        return REF_AGC_WORLD_INVALID;
    if (pthread_mutex_lock(&store->lock) != 0)
        return REF_AGC_WORLD_INVALID;
    *out = store->stats;
    (void)pthread_mutex_unlock(&store->lock);
    return REF_AGC_WORLD_OK;
}
