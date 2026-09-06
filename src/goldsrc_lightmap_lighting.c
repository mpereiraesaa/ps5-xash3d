#include "goldsrc_lightmap_lighting.h"

#include <float.h>
#include <math.h>
#include <string.h>

typedef struct LightingComposeContext {
    const GoldSrcLightmapLightingPlan *plan;
    uint64_t frame_index;
    uint32_t mode;
    GoldSrcLightmapLightingUpdate *update;
} LightingComposeContext;

static uint64_t hash_bytes(uint64_t hash, const uint8_t *data, size_t bytes)
{
    for (size_t index = 0u; index < bytes; ++index) {
        hash ^= data[index];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static uint32_t style_count(const uint8_t styles[4])
{
    uint32_t count = 0u;
    while (count < 4u && styles[count] != 255u)
        ++count;
    return count;
}

static const BspBundleDraw *find_draw(const BspBundleView *bundle,
                                      uint32_t face_id,
                                      uint32_t *draw_index)
{
    for (uint32_t index = 0u; index < bundle->draw_count; ++index)
        if (bundle->draws[index].face_id == face_id) {
            if (draw_index)
                *draw_index = index;
            return &bundle->draws[index];
        }
    return 0;
}

static int draw_center_normal(const BspBundleView *bundle,
                              const BspBundleDraw *draw,
                              float center[3], float normal[3])
{
    if (!bundle || !draw || !center || !normal || draw->index_count < 3u ||
        draw->first_index > bundle->index_count ||
        draw->index_count > bundle->index_count - draw->first_index)
        return -1;
    memset(center, 0, 3u * sizeof(float));
    for (uint32_t item = 0u; item < draw->index_count; ++item) {
        const uint32_t vertex = bundle->indices[draw->first_index + item];
        if (vertex >= bundle->vertex_count)
            return -1;
        for (uint32_t axis = 0u; axis < 3u; ++axis)
            center[axis] += bundle->vertices[vertex].position[axis];
    }
    for (uint32_t axis = 0u; axis < 3u; ++axis)
        center[axis] /= (float)draw->index_count;
    const BspBundleVertex *const a = &bundle->vertices[
        bundle->indices[draw->first_index]];
    const BspBundleVertex *const b = &bundle->vertices[
        bundle->indices[draw->first_index + 1u]];
    const BspBundleVertex *const c = &bundle->vertices[
        bundle->indices[draw->first_index + 2u]];
    const float ab[3] = {
        b->position[0] - a->position[0],
        b->position[1] - a->position[1],
        b->position[2] - a->position[2],
    };
    const float ac[3] = {
        c->position[0] - a->position[0],
        c->position[1] - a->position[1],
        c->position[2] - a->position[2],
    };
    normal[0] = ab[1] * ac[2] - ab[2] * ac[1];
    normal[1] = ab[2] * ac[0] - ab[0] * ac[2];
    normal[2] = ab[0] * ac[1] - ab[1] * ac[0];
    const float length_squared = normal[0] * normal[0] +
                                 normal[1] * normal[1] +
                                 normal[2] * normal[2];
    if (!(length_squared > 0.000001f) || !__builtin_isfinite(length_squared))
        return -1;
    const float inverse = 1.0f / sqrtf(length_squared);
    for (uint32_t axis = 0u; axis < 3u; ++axis)
        normal[axis] *= inverse;
    return 0;
}

const char *goldsrc_lighting_mode_name(uint32_t mode)
{
    static const char *const names[GOLDSRC_LIGHTING_MODE_COUNT] = {
        "base", "lightstyle", "dlight", "combined",
    };
    return mode < GOLDSRC_LIGHTING_MODE_COUNT ? names[mode] : "invalid";
}

uint32_t goldsrc_lighting_mode(uint64_t frame_index)
{
    return (uint32_t)((frame_index / GOLDSRC_LIGHTING_HOLD_FRAMES) %
                      GOLDSRC_LIGHTING_MODE_COUNT);
}

uint16_t goldsrc_lightstyle_scale(const char *pattern, uint64_t tick)
{
    if (!pattern || !pattern[0])
        return 256u;
    size_t length = 0u;
    while (pattern[length]) {
        if (pattern[length] < 'a' || pattern[length] > 'z')
            return 0u;
        ++length;
    }
    const uint32_t level = (uint32_t)(pattern[tick % length] - 'a');
    return (uint16_t)((level * 256u + 6u) / 12u);
}

static const char *pattern_for_style(uint8_t style)
{
    static const char *const patterns[5] = {
        "abcdefghijklmnopqrstuvwxyzyxwvutsrqponmlkjihgfedcba",
        "mmnmmommommnonmmonqnmmo",
        "abcdefghijklmnopqrrqponmlkjihgfedcba",
        "aaaazzzz",
        "mmmmmaaaaammmmmaaaaaabcdefgabcdefg",
    };
    return patterns[(uint32_t)style % 5u];
}

int goldsrc_lightmap_lighting_plan(
    const BspBundleView *bundle, size_t maximum_patch_bytes,
    GoldSrcLightmapLightingPlan *out)
{
    if (!bundle || !out || !bundle->lightmap_image ||
        !bundle->lightmap_faces || bundle->lightmap_face_count == 0u ||
        !bundle->lightmap_samples || bundle->lightmap_sample_bytes == 0u ||
        !bundle->draws || !bundle->indices || !bundle->vertices ||
        !bundle->textures || maximum_patch_bytes < 4u)
        return -1;
    memset(out, 0, sizeof(*out));
    size_t best_area = 0u;
    float best_distance = FLT_MAX;
    float best_center[3] = {0.0f, 0.0f, 0.0f};
    float best_normal[3] = {0.0f, 0.0f, 0.0f};
    uint32_t best_draw = UINT32_MAX;
    const BspBundleLightmapFace *best = 0;
    for (uint32_t index = 0u; index < bundle->lightmap_face_count; ++index) {
        const BspBundleLightmapFace *const face =
            &bundle->lightmap_faces[index];
        const uint32_t count = style_count(face->styles);
        const size_t area = (size_t)face->width * face->height;
        if (count < 2u || face->width < 4u || face->height < 4u ||
            area > maximum_patch_bytes / 4u)
            continue;
        uint32_t draw_index = UINT32_MAX;
        const BspBundleDraw *const draw =
            find_draw(bundle, face->face_id, &draw_index);
        if (!draw || draw->base_texture >= bundle->texture_count ||
            (bundle->textures[draw->base_texture].flags &
             (BSP_BUNDLE_TEXTURE_TRANSPARENT | BSP_BUNDLE_TEXTURE_NODRAW |
              BSP_BUNDLE_TEXTURE_SKY)) != 0u)
            continue;
        float center[3];
        float normal[3];
        if (draw_center_normal(bundle, draw, center, normal) != 0)
            continue;
        /* A wall-facing proof camera makes atlas changes legible and avoids
         * placing the camera above a ceiling or below a floor. */
        if (fabsf(normal[1]) > 0.75f)
            continue;
        const float delta[3] = {
            center[0] - bundle->camera_position[0],
            center[1] - bundle->camera_position[1],
            center[2] - bundle->camera_position[2],
        };
        const float distance = delta[0] * delta[0] +
                               delta[1] * delta[1] +
                               delta[2] * delta[2];
        if (area < best_area || (area == best_area && distance >= best_distance))
            continue;
        best = face;
        best_area = area;
        best_distance = distance;
        best_draw = draw_index;
        memcpy(best_center, center, sizeof(best_center));
        memcpy(best_normal, normal, sizeof(best_normal));
    }
    if (!best || best_draw == UINT32_MAX)
        return -2;
    const float toward_spawn[3] = {
        bundle->camera_position[0] - best_center[0],
        bundle->camera_position[1] - best_center[1],
        bundle->camera_position[2] - best_center[2],
    };
    if (toward_spawn[0] * best_normal[0] +
            toward_spawn[1] * best_normal[1] +
            toward_spawn[2] * best_normal[2] < 0.0f)
        for (uint32_t axis = 0u; axis < 3u; ++axis)
            best_normal[axis] = -best_normal[axis];
    const BspBundleImage *const image = bundle->lightmap_image;
    out->layout = (BspDynamicLightmapLayout){
        image->width, image->height, image->row_pitch,
        best->atlas_x, best->atlas_y, best->width, best->height,
        best->face_id,
        (size_t)image->height * image->row_pitch,
        best_area * 4u,
        (size_t)best->atlas_y * image->row_pitch +
            (size_t)best->atlas_x * 4u,
        (size_t)(best->height - 1u) * image->row_pitch +
            (size_t)best->width * 4u,
    };
    out->face = best;
    out->samples = bundle->lightmap_samples + best->sample_offset;
    out->draw_index = best_draw;
    out->style_count = style_count(best->styles);
    for (uint32_t index = 0u; index < bundle->lightmap_face_count; ++index) {
        const uint32_t count = style_count(bundle->lightmap_faces[index].styles);
        if (count > 1u) {
            ++out->styled_face_count;
            out->styled_layer_count += count - 1u;
        }
    }
    for (uint32_t axis = 0u; axis < 3u; ++axis) {
        out->target_position[axis] = best_center[axis] +
                                     best_normal[axis] * 96.0f;
        out->target_forward[axis] = -best_normal[axis];
    }
    out->source_hash = hash_bytes(UINT64_C(14695981039346656037),
                                  out->samples, best->sample_bytes);
    return out->source_hash != 0u ? 0 : -3;
}

static int compose_lighting(uint8_t *rgba, size_t rgba_bytes, void *opaque)
{
    LightingComposeContext *const context = opaque;
    if (!rgba || !context || !context->plan || !context->update ||
        context->mode >= GOLDSRC_LIGHTING_MODE_COUNT ||
        rgba_bytes != context->plan->layout.patch_bytes)
        return -1;
    const GoldSrcLightmapLightingPlan *const plan = context->plan;
    const size_t texels = (size_t)plan->layout.patch_width *
                          plan->layout.patch_height;
    const size_t plane_bytes = texels * 3u;
    const int styles_enabled =
        context->mode == GOLDSRC_LIGHTING_MODE_LIGHTSTYLE ||
        context->mode == GOLDSRC_LIGHTING_MODE_COMBINED;
    const int dlight_enabled =
        context->mode == GOLDSRC_LIGHTING_MODE_DLIGHT ||
        context->mode == GOLDSRC_LIGHTING_MODE_COMBINED;
    const uint32_t style_tick = (uint32_t)(context->frame_index / 6u);
    uint16_t scales[4] = {256u, 0u, 0u, 0u};
    uint64_t style_hash = UINT64_C(14695981039346656037);
    uint32_t animated_scale = 0u;
    for (uint32_t style = 0u; style < plan->style_count; ++style) {
        if (style > 0u && styles_enabled)
            scales[style] = goldsrc_lightstyle_scale(
                pattern_for_style(plan->face->styles[style]), style_tick);
        if (style > 0u)
            animated_scale += scales[style];
        style_hash = hash_bytes(style_hash,
                                (const uint8_t *)&scales[style],
                                sizeof(scales[style]));
    }
    const uint32_t width = plan->layout.patch_width;
    const uint32_t height = plan->layout.patch_height;
    const uint32_t span = width > 1u ? 2u * (width - 1u) : 1u;
    uint32_t center_x = (uint32_t)((context->frame_index / 6u) % span);
    if (center_x >= width)
        center_x = span - center_x;
    const uint32_t center_y = height / 2u;
    uint32_t radius = (width < height ? width : height) / 2u;
    if (radius < 4u)
        radius = 4u;
    const uint32_t radius_squared = radius * radius;
    uint32_t dynamic_luxels = 0u;
    for (uint32_t y = 0u; y < height; ++y) {
        for (uint32_t x = 0u; x < width; ++x) {
            const size_t texel = (size_t)y * width + x;
            uint32_t color[3] = {0u, 0u, 0u};
            for (uint32_t style = 0u; style < plan->style_count; ++style) {
                const uint8_t *const sample = plan->samples +
                    (size_t)style * plane_bytes + texel * 3u;
                for (uint32_t channel = 0u; channel < 3u; ++channel)
                    color[channel] +=
                        ((uint32_t)sample[channel] * scales[style] + 128u) >> 8;
            }
            if (dlight_enabled) {
                const int32_t dx = (int32_t)x - (int32_t)center_x;
                const int32_t dy = (int32_t)y - (int32_t)center_y;
                const uint32_t distance_squared =
                    (uint32_t)(dx * dx + dy * dy);
                if (distance_squared < radius_squared) {
                    const uint32_t intensity =
                        ((radius_squared - distance_squared) * 256u) /
                        radius_squared;
                    color[0] += (255u * intensity + 128u) >> 8;
                    color[1] += (96u * intensity + 128u) >> 8;
                    color[2] += (32u * intensity + 128u) >> 8;
                    ++dynamic_luxels;
                }
            }
            const size_t target = texel * 4u;
            for (uint32_t channel = 0u; channel < 3u; ++channel)
                rgba[target + channel] =
                    (uint8_t)(color[channel] > 255u ? 255u : color[channel]);
            rgba[target + 3u] = 255u;
        }
    }
    *context->update = (GoldSrcLightmapLightingUpdate){
        context->mode, style_tick, animated_scale, dynamic_luxels,
        center_x, center_y, style_hash,
    };
    return 0;
}

int goldsrc_lightmap_lighting_update(
    const GoldSrcLightmapLightingPlan *plan,
    BspDynamicLightmapSlot *slot, Ps5TransientRing *ring,
    uint32_t slot_index, uint64_t frame_index, uint32_t mode,
    BspDynamicLightmapUpdate *upload,
    GoldSrcLightmapLightingUpdate *lighting)
{
    if (!plan || !plan->face || !plan->samples || !slot || !ring ||
        !upload || !lighting || mode >= GOLDSRC_LIGHTING_MODE_COUNT)
        return -1;
    LightingComposeContext context = {plan, frame_index, mode, lighting};
    return bsp_dynamic_lightmap_update_composed(
        slot, &plan->layout, ring, slot_index, frame_index, mode,
        compose_lighting, &context, upload);
}
