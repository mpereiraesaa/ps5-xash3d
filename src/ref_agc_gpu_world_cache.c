#include "ref_agc_gpu_world_cache.h"

#include "ps5_gfx1013_descriptor.h"

#include <limits.h>
#include <string.h>

enum { WORLD_VERTEX_STRIDE = sizeof(RefAgcWorldVertex) };

static int align_cursor(size_t *cursor, size_t alignment)
{
    if (!cursor || alignment == 0u || (alignment & (alignment - 1u)) != 0u ||
        *cursor > SIZE_MAX - (alignment - 1u))
        return -1;
    *cursor = (*cursor + alignment - 1u) & ~(alignment - 1u);
    return 0;
}

static int reserve(size_t *cursor, size_t bytes, size_t capacity,
                   size_t alignment, size_t *offset)
{
    if (!offset || align_cursor(cursor, alignment) != 0 ||
        *cursor > capacity || bytes > capacity - *cursor)
        return -1;
    *offset = *cursor;
    *cursor += bytes;
    return 0;
}

static uint64_t hash_bytes(uint64_t hash, const void *data, size_t bytes)
{
    const uint8_t *cursor = data;
    while (bytes--) {
        hash ^= *cursor++;
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static int copy_texture_table(RefAgcGpuWorldCache *cache,
                              const RefAgcGpuTextureCache *textures,
                              RefAgcGpuWorldDraw *draw)
{
    RefAgcGpuTextureEntry texture;
    uint32_t *table;
    if (ref_agc_gpu_texture_cache_get(textures, draw->texture_handle,
                                      &texture) != REF_AGC_GPU_TEXTURE_OK)
        return REF_AGC_GPU_WORLD_TEXTURE_MISSING;
    table = (uint32_t *)(cache->base + draw->texture_table_offset);
    memcpy(table, texture.descriptor, sizeof(texture.descriptor));
    memcpy(table + REF_AGC_GPU_TEXTURE_DESCRIPTOR_DWORDS,
           (draw->draw_flags & REF_AGC_WORLD_DRAW_LIGHTMAP) ?
               cache->lightmap_descriptor : texture.descriptor,
           sizeof(texture.descriptor));
    return REF_AGC_GPU_WORLD_OK;
}

int ref_agc_gpu_world_cache_init(RefAgcGpuWorldCache *cache, void *base,
                                 uint64_t gpu_base, size_t bytes,
                                 RefAgcGpuWorldFlushFn flush,
                                 void *flush_user)
{
    if (!cache || !base || !flush || bytes == 0u || gpu_base == 0u ||
        (gpu_base & 255u) != 0u || gpu_base >= (UINT64_C(1) << 48) ||
        bytes > (UINT64_C(1) << 48) - gpu_base)
        return REF_AGC_GPU_WORLD_INVALID;
    memset(cache, 0, sizeof(*cache));
    cache->base = base;
    cache->gpu_base = gpu_base;
    cache->bytes = bytes;
    cache->flush = flush;
    cache->flush_user = flush_user;
    cache->initialized = 1;
    return REF_AGC_GPU_WORLD_OK;
}

int ref_agc_gpu_world_cache_apply(RefAgcGpuWorldCache *cache,
                                  const RefAgcWorldView *view,
                                  const RefAgcGpuTextureCache *textures,
                                  int prior_use_retired)
{
    size_t cursor = 0u;
    uint32_t total_vertices = 0u;
    uint32_t lightmapped_draws = 0u;
    uint32_t sky_draws = 0u;
    uint32_t turbulent_draws = 0u;
    uint32_t sky_indices = 0u;
    uint32_t turbulent_indices = 0u;
    uint32_t lightmap_gpu_row_pitch = 0u;
    uint64_t lightmap_rgb_sum = 0u;
    uint32_t lightmap_nonzero_texels = 0u;
    uint8_t lightmap_rgb_min = UINT8_MAX;
    uint8_t lightmap_rgb_max = 0u;
    uint64_t upload_hash = UINT64_C(14695981039346656037);
    if (!cache || !cache->initialized || !view || !textures ||
        !textures->initialized || view->revision == 0u ||
        view->revision < cache->stats.revision)
        return REF_AGC_GPU_WORLD_INVALID;
    if (view->revision == cache->stats.revision)
        return REF_AGC_GPU_WORLD_OK;
    if (cache->stats.active && !prior_use_retired)
        return REF_AGC_GPU_WORLD_RETIREMENT_REQUIRED;
    if (!view->active) {
        if (!cache->stats.active)
            return REF_AGC_GPU_WORLD_STALE;
        memset(cache->draws, 0, sizeof(cache->draws));
        cache->stats.revision = view->revision;
        cache->stats.resident_bytes = 0u;
        cache->stats.vertex_count = 0u;
        cache->stats.index_count = 0u;
        cache->stats.draw_count = 0u;
        cache->stats.texture_tables = 0u;
        cache->stats.lightmap_width = 0u;
        cache->stats.lightmap_height = 0u;
        cache->stats.lightmap_row_pitch = 0u;
        cache->stats.lightmapped_draw_count = 0u;
        cache->stats.sky_draw_count = 0u;
        cache->stats.turbulent_draw_count = 0u;
        cache->stats.sky_index_count = 0u;
        cache->stats.turbulent_index_count = 0u;
        cache->stats.lightmap_bytes = 0u;
        cache->stats.lightmap_rgb_sum = 0u;
        cache->stats.lightmap_nonzero_texels = 0u;
        cache->stats.lightmap_rgb_min = 0u;
        cache->stats.lightmap_rgb_max = 0u;
        cache->lightmap_offset = 0u;
        memset(cache->lightmap_descriptor, 0,
               sizeof(cache->lightmap_descriptor));
        cache->stats.active = 0;
        ++cache->stats.clears;
        return REF_AGC_GPU_WORLD_OK;
    }
    if (!view->vertices || !view->indices || !view->draws ||
        view->vertex_count == 0u || view->index_count == 0u ||
        view->draw_count == 0u ||
        view->draw_count > REF_AGC_GPU_WORLD_MAX_DRAWS)
        return REF_AGC_GPU_WORLD_INVALID;
    for (uint32_t i = 0u; i < view->draw_count; ++i) {
        if ((view->draws[i].draw_flags & (REF_AGC_WORLD_DRAW_SKY |
                                          REF_AGC_WORLD_DRAW_TURB)) ==
            (REF_AGC_WORLD_DRAW_SKY | REF_AGC_WORLD_DRAW_TURB))
            return REF_AGC_GPU_WORLD_INVALID;
        if (view->draws[i].draw_flags & REF_AGC_WORLD_DRAW_LIGHTMAP)
            ++lightmapped_draws;
        if (view->draws[i].draw_flags & REF_AGC_WORLD_DRAW_SKY) {
            ++sky_draws;
            if (sky_indices > UINT32_MAX - view->draws[i].index_count)
                return REF_AGC_GPU_WORLD_INVALID;
            sky_indices += view->draws[i].index_count;
        }
        if (view->draws[i].draw_flags & REF_AGC_WORLD_DRAW_TURB) {
            ++turbulent_draws;
            if (turbulent_indices > UINT32_MAX - view->draws[i].index_count)
                return REF_AGC_GPU_WORLD_INVALID;
            turbulent_indices += view->draws[i].index_count;
        }
    }
    if (lightmapped_draws != view->lightmapped_draw_count ||
        sky_draws != view->sky_draw_count ||
        turbulent_draws != view->turbulent_draw_count)
        return REF_AGC_GPU_WORLD_INVALID;
    if (view->lightmapped_draw_count) {
        size_t source_bytes, gpu_row_pitch, lightmap_bytes;
        if (!view->lightmap_pixels || !view->lightmap_width ||
            !view->lightmap_height ||
            view->lightmap_width > UINT32_MAX / 4u ||
            view->lightmap_row_pitch < view->lightmap_width * 4u ||
            view->lightmap_height > SIZE_MAX / view->lightmap_row_pitch)
            return REF_AGC_GPU_WORLD_INVALID;
        source_bytes = (size_t)view->lightmap_row_pitch *
                       view->lightmap_height;
        gpu_row_pitch = (size_t)view->lightmap_width * 4u;
        if (align_cursor(&gpu_row_pitch, 256u) != 0 ||
            gpu_row_pitch > UINT32_MAX ||
            view->lightmap_height > SIZE_MAX / gpu_row_pitch)
            return REF_AGC_GPU_WORLD_INVALID;
        lightmap_bytes = gpu_row_pitch * view->lightmap_height;
        lightmap_gpu_row_pitch = (uint32_t)gpu_row_pitch;
        if (view->lightmap_pixel_bytes != source_bytes ||
            reserve(&cursor, lightmap_bytes, cache->bytes, 256u,
                    &cache->lightmap_offset) != 0)
            return view->lightmap_pixel_bytes != source_bytes ?
                REF_AGC_GPU_WORLD_INVALID : REF_AGC_GPU_WORLD_EXHAUSTED;
        memset(cache->base + cache->lightmap_offset, 0, lightmap_bytes);
        for (uint32_t row = 0u; row < view->lightmap_height; ++row) {
            memcpy(cache->base + cache->lightmap_offset +
                       (size_t)row * gpu_row_pitch,
                   view->lightmap_pixels +
                       (size_t)row * view->lightmap_row_pitch,
                   (size_t)view->lightmap_width * 4u);
            const uint8_t *source = view->lightmap_pixels +
                (size_t)row * view->lightmap_row_pitch;
            for (uint32_t column = 0u; column < view->lightmap_width;
                 ++column) {
                const uint8_t *pixel = source + (size_t)column * 4u;
                const uint32_t rgb = (uint32_t)pixel[0] + pixel[1] + pixel[2];
                lightmap_rgb_sum += rgb;
                if (rgb != 0u) ++lightmap_nonzero_texels;
                for (unsigned channel = 0u; channel < 3u; ++channel) {
                    if (pixel[channel] < lightmap_rgb_min)
                        lightmap_rgb_min = pixel[channel];
                    if (pixel[channel] > lightmap_rgb_max)
                        lightmap_rgb_max = pixel[channel];
                }
            }
        }
        if (ps5_gfx1013_build_tsharp_rgba8(
                cache->lightmap_descriptor,
                cache->gpu_base + cache->lightmap_offset,
                view->lightmap_width, view->lightmap_height,
                (uint32_t)gpu_row_pitch) != 0 ||
            ps5_gfx1013_build_ssharp(
                cache->lightmap_descriptor + PS5_GFX1013_TSHARP_DWORDS,
                PS5_GFX1013_CLAMP_LAST_TEXEL,
                PS5_GFX1013_FILTER_BILINEAR) != 0)
            return REF_AGC_GPU_WORLD_DESCRIPTOR_FAILED;
    } else if (view->lightmap_pixels || view->lightmap_width ||
               view->lightmap_height || view->lightmap_row_pitch ||
               view->lightmap_pixel_bytes) {
        return REF_AGC_GPU_WORLD_INVALID;
    } else {
        cache->lightmap_offset = 0u;
        memset(cache->lightmap_descriptor, 0,
               sizeof(cache->lightmap_descriptor));
    }
    memset(cache->draws, 0, sizeof(cache->draws));
    for (uint32_t i = 0u; i < view->draw_count; ++i) {
        const RefAgcWorldDraw *source = &view->draws[i];
        RefAgcGpuWorldDraw *target = &cache->draws[i];
        uint32_t minimum = UINT32_MAX, maximum = 0u;
        if (source->first_index > view->index_count ||
            source->index_count < 3u || source->index_count % 3u != 0u ||
            source->index_count > view->index_count - source->first_index ||
            source->texture_handle == 0u)
            return REF_AGC_GPU_WORLD_INVALID;
        for (uint32_t j = 0u; j < source->index_count; ++j) {
            const uint32_t value = view->indices[source->first_index + j];
            if (value >= view->vertex_count)
                return REF_AGC_GPU_WORLD_INVALID;
            if (value < minimum) minimum = value;
            if (value > maximum) maximum = value;
        }
        const uint32_t vertex_count = maximum - minimum + 1u;
        if (vertex_count > UINT16_MAX ||
            total_vertices > UINT32_MAX - vertex_count)
            return REF_AGC_GPU_WORLD_INVALID;
        size_t vertex_bytes = (size_t)vertex_count * sizeof(RefAgcWorldVertex);
        size_t index_bytes = (size_t)source->index_count * sizeof(uint16_t);
        if (reserve(&cursor, vertex_bytes, cache->bytes, 16u,
                    &target->vertex_offset) != 0 ||
            reserve(&cursor, index_bytes, cache->bytes, 2u,
                    &target->index_offset) != 0 ||
            reserve(&cursor, 4u * sizeof(uint32_t), cache->bytes, 16u,
                    &target->vertex_table_offset) != 0 ||
            reserve(&cursor, REF_AGC_GPU_WORLD_TEXTURE_DWORDS *
                        sizeof(uint32_t), cache->bytes, 16u,
                    &target->texture_table_offset) != 0)
            return REF_AGC_GPU_WORLD_EXHAUSTED;
        memcpy(cache->base + target->vertex_offset,
               view->vertices + minimum, vertex_bytes);
        uint16_t *destination_indices =
            (uint16_t *)(cache->base + target->index_offset);
        for (uint32_t j = 0u; j < source->index_count; ++j)
            destination_indices[j] = (uint16_t)(
                view->indices[source->first_index + j] - minimum);
        uint32_t *vertex_table =
            (uint32_t *)(cache->base + target->vertex_table_offset);
        if (ps5_gfx1013_build_vsharp(
                vertex_table, cache->gpu_base + target->vertex_offset,
                WORLD_VERTEX_STRIDE, vertex_count) != 0)
            return REF_AGC_GPU_WORLD_DESCRIPTOR_FAILED;
        target->vertex_count = vertex_count;
        target->index_count = source->index_count;
        target->texture_handle = source->texture_handle;
        target->surface_id = source->surface_id;
        target->surface_flags = source->surface_flags;
        target->draw_flags = source->draw_flags;
        if (copy_texture_table(cache, textures, target) != 0)
            return REF_AGC_GPU_WORLD_TEXTURE_MISSING;
        total_vertices += vertex_count;
    }
    cache->flush(cache->base, cursor, cache->flush_user);
    upload_hash = hash_bytes(upload_hash, cache->base, cursor);
    cache->stats.revision = view->revision;
    ++cache->stats.publishes;
    ++cache->stats.flushes;
    cache->stats.source_hash = view->content_hash;
    cache->stats.upload_hash = upload_hash;
    cache->stats.vertex_count = total_vertices;
    cache->stats.index_count = view->index_count;
    cache->stats.draw_count = view->draw_count;
    cache->stats.texture_tables = view->draw_count;
    cache->stats.lightmap_width = view->lightmap_width;
    cache->stats.lightmap_height = view->lightmap_height;
    cache->stats.lightmap_row_pitch = lightmap_gpu_row_pitch;
    cache->stats.lightmapped_draw_count = lightmapped_draws;
    cache->stats.sky_draw_count = sky_draws;
    cache->stats.turbulent_draw_count = turbulent_draws;
    cache->stats.sky_index_count = sky_indices;
    cache->stats.turbulent_index_count = turbulent_indices;
    cache->stats.lightmap_bytes = (size_t)lightmap_gpu_row_pitch *
                                  view->lightmap_height;
    cache->stats.lightmap_rgb_sum = lightmap_rgb_sum;
    cache->stats.lightmap_nonzero_texels = lightmap_nonzero_texels;
    cache->stats.lightmap_rgb_min = view->lightmapped_draw_count ?
        lightmap_rgb_min : 0u;
    cache->stats.lightmap_rgb_max = lightmap_rgb_max;
    cache->stats.resident_bytes = cursor;
    if (cursor > cache->stats.peak_resident_bytes)
        cache->stats.peak_resident_bytes = cursor;
    cache->stats.active = 1;
    return REF_AGC_GPU_WORLD_OK;
}

int ref_agc_gpu_world_cache_refresh_textures(
    RefAgcGpuWorldCache *cache, const RefAgcGpuTextureCache *textures,
    int prior_use_retired)
{
    if (!cache || !cache->initialized || !textures ||
        !textures->initialized)
        return REF_AGC_GPU_WORLD_INVALID;
    if (!cache->stats.active)
        return REF_AGC_GPU_WORLD_OK;
    if (!prior_use_retired)
        return REF_AGC_GPU_WORLD_RETIREMENT_REQUIRED;
    for (uint32_t i = 0u; i < cache->stats.draw_count; ++i)
        if (copy_texture_table(cache, textures, &cache->draws[i]) != 0)
            return REF_AGC_GPU_WORLD_TEXTURE_MISSING;
    cache->flush(cache->base, cache->stats.resident_bytes,
                 cache->flush_user);
    ++cache->stats.flushes;
    cache->stats.upload_hash = hash_bytes(
        UINT64_C(14695981039346656037), cache->base,
        cache->stats.resident_bytes);
    return REF_AGC_GPU_WORLD_OK;
}

int ref_agc_gpu_world_cache_stats(const RefAgcGpuWorldCache *cache,
                                  RefAgcGpuWorldStats *out)
{
    if (!cache || !cache->initialized || !out)
        return REF_AGC_GPU_WORLD_INVALID;
    *out = cache->stats;
    return REF_AGC_GPU_WORLD_OK;
}

int ref_agc_gpu_world_cache_validate(const RefAgcGpuWorldCache *cache)
{
    if (!cache || !cache->initialized ||
        cache->stats.draw_count > REF_AGC_GPU_WORLD_MAX_DRAWS ||
        cache->stats.resident_bytes > cache->bytes ||
        (cache->stats.lightmapped_draw_count != 0u &&
         (cache->stats.lightmap_bytes == 0u ||
          cache->stats.lightmap_nonzero_texels == 0u ||
          cache->stats.lightmap_rgb_sum == 0u ||
          cache->lightmap_offset > cache->stats.resident_bytes ||
          cache->stats.lightmap_bytes >
              cache->stats.resident_bytes - cache->lightmap_offset)) ||
        (!!cache->stats.active != (cache->stats.draw_count != 0u)))
        return REF_AGC_GPU_WORLD_INVALID;
    for (uint32_t i = 0u; i < cache->stats.draw_count; ++i) {
        const RefAgcGpuWorldDraw *draw = &cache->draws[i];
        if (!draw->vertex_count || !draw->index_count ||
            draw->vertex_count > UINT16_MAX ||
            draw->vertex_offset > cache->stats.resident_bytes ||
            (size_t)draw->vertex_count * WORLD_VERTEX_STRIDE >
                cache->stats.resident_bytes - draw->vertex_offset ||
            draw->index_offset > cache->stats.resident_bytes ||
            (size_t)draw->index_count * sizeof(uint16_t) >
                cache->stats.resident_bytes - draw->index_offset ||
            draw->vertex_table_offset > cache->stats.resident_bytes ||
            4u * sizeof(uint32_t) >
                cache->stats.resident_bytes - draw->vertex_table_offset ||
            draw->texture_table_offset > cache->stats.resident_bytes ||
            REF_AGC_GPU_WORLD_TEXTURE_DWORDS * sizeof(uint32_t) >
                cache->stats.resident_bytes - draw->texture_table_offset)
            return REF_AGC_GPU_WORLD_INVALID;
    }
    return REF_AGC_GPU_WORLD_OK;
}

const RefAgcGpuWorldDraw *ref_agc_gpu_world_cache_draw(
    const RefAgcGpuWorldCache *cache, uint32_t draw)
{
    return cache && cache->initialized && cache->stats.active &&
        draw < cache->stats.draw_count ? &cache->draws[draw] : NULL;
}

const uint16_t *ref_agc_gpu_world_cache_indices(
    const RefAgcGpuWorldCache *cache, const RefAgcGpuWorldDraw *draw)
{
    return cache && draw ? (const uint16_t *)(cache->base +
        draw->index_offset) : NULL;
}

const uint32_t *ref_agc_gpu_world_cache_vertex_table(
    const RefAgcGpuWorldCache *cache, const RefAgcGpuWorldDraw *draw)
{
    return cache && draw ? (const uint32_t *)(cache->base +
        draw->vertex_table_offset) : NULL;
}

const uint32_t *ref_agc_gpu_world_cache_texture_table(
    const RefAgcGpuWorldCache *cache, const RefAgcGpuWorldDraw *draw)
{
    return cache && draw ? (const uint32_t *)(cache->base +
        draw->texture_table_offset) : NULL;
}

void ref_agc_gpu_world_cache_destroy(RefAgcGpuWorldCache *cache)
{
    if (cache)
        memset(cache, 0, sizeof(*cache));
}
