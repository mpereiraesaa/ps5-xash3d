#include "goldsrc_brush_entities.h"

#include "bsp_flat_scene.h"
#include "bsp_texture_descriptor.h"
#include "ps5_gfx1013_descriptor.h"
#include "ps5_gpu_span.h"
#include "ps5_transient_table.h"

#include <math.h>
#include <string.h>

typedef struct Matrix4 { float v[16]; } Matrix4;
typedef struct Vec3 { float x, y, z; } Vec3;

static float dot(Vec3 a, Vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static Vec3 cross(Vec3 a, Vec3 b)
{
    const Vec3 result = {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
    return result;
}

static int normalize(Vec3 *value)
{
    const float length = sqrtf(dot(*value, *value));
    if (!(length > 0.000001f) || !__builtin_isfinite(length))
        return -1;
    value->x /= length;
    value->y /= length;
    value->z /= length;
    return 0;
}

static Matrix4 multiply(Matrix4 a, Matrix4 b)
{
    Matrix4 result = {{0}};
    for (uint32_t column = 0u; column < 4u; ++column)
        for (uint32_t row = 0u; row < 4u; ++row)
            for (uint32_t k = 0u; k < 4u; ++k)
                result.v[column * 4u + row] +=
                    a.v[k * 4u + row] * b.v[column * 4u + k];
    return result;
}

static uint64_t hash_bytes(uint64_t hash, const void *opaque, size_t bytes)
{
    const uint8_t *data = opaque;
    for (size_t index = 0u; index < bytes; ++index) {
        hash ^= data[index];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

GoldSrcBrushMode goldsrc_brush_mode(uint64_t frame_index)
{
    return (GoldSrcBrushMode)((frame_index / GOLDSRC_BRUSH_HOLD_FRAMES) %
                              GOLDSRC_BRUSH_MODE_COUNT);
}

const char *goldsrc_brush_mode_name(GoldSrcBrushMode mode)
{
    static const char *const names[GOLDSRC_BRUSH_MODE_COUNT] = {
        "control", "opaque", "alpha", "additive", "combined",
    };
    return mode < GOLDSRC_BRUSH_MODE_COUNT ? names[mode] : "invalid";
}

static void entity_counts(const BspBundleView *bundle,
                          const BspBundleBrushEntity *entity,
                          uint32_t *draws, uint32_t *indices)
{
    *draws = *indices = 0u;
    const uint32_t end = entity->first_face + entity->face_count;
    for (uint32_t draw = 0u; draw < bundle->draw_count; ++draw) {
        const BspBundleDraw *const source = &bundle->draws[draw];
        if (source->face_id >= entity->first_face && source->face_id < end &&
            !(bundle->textures[source->base_texture].flags &
              (BSP_BUNDLE_TEXTURE_NODRAW | BSP_BUNDLE_TEXTURE_SKY))) {
            ++*draws;
            *indices += source->index_count;
        }
    }
}

static int find_candidate(const BspBundleView *bundle, uint32_t desired_mode,
                          const uint32_t *selected, uint32_t selected_count,
                          uint32_t *entity_out, uint32_t *draws_out,
                          uint32_t *indices_out)
{
    for (uint32_t index = 0u; index < bundle->brush_entity_count; ++index) {
        const BspBundleBrushEntity *const entity =
            &bundle->brush_entities[index];
        int duplicate = 0;
        for (uint32_t prior = 0u; prior < selected_count; ++prior)
            duplicate |= selected[prior] == index;
        if (duplicate || (desired_mode != UINT32_MAX &&
                          entity->render_mode != desired_mode))
            continue;
        uint32_t draws;
        uint32_t indices;
        entity_counts(bundle, entity, &draws, &indices);
        if (draws != 0u && indices != 0u) {
            *entity_out = index;
            *draws_out = draws;
            *indices_out = indices;
            return 0;
        }
    }
    return -1;
}

static int entity_uses_texture(const BspBundleView *bundle,
                               const BspBundleBrushEntity *entity,
                               uint32_t texture_name_hash)
{
    const uint32_t end = entity->first_face + entity->face_count;
    for (uint32_t draw = 0u; draw < bundle->draw_count; ++draw) {
        const BspBundleDraw *const source = &bundle->draws[draw];
        if (source->face_id >= entity->first_face &&
            source->face_id < end &&
            source->base_texture < bundle->texture_count &&
            bundle->textures[source->base_texture].name_hash ==
                texture_name_hash)
            return 1;
    }
    return 0;
}

static int find_scene_candidate(
    const BspBundleView *bundle, uint32_t classname_hash,
    uint32_t texture_name_hash, const uint32_t *selected,
    uint32_t selected_count, uint32_t *entity_out, uint32_t *draws_out,
    uint32_t *indices_out)
{
    for (uint32_t index = 0u; index < bundle->brush_entity_count; ++index) {
        const BspBundleBrushEntity *const entity =
            &bundle->brush_entities[index];
        int duplicate = 0;
        for (uint32_t prior = 0u; prior < selected_count; ++prior)
            duplicate |= selected[prior] == index;
        if (duplicate ||
            (classname_hash != 0u &&
             entity->classname_hash != classname_hash) ||
            (texture_name_hash != 0u &&
             !entity_uses_texture(bundle, entity, texture_name_hash)))
            continue;
        uint32_t draws;
        uint32_t indices;
        entity_counts(bundle, entity, &draws, &indices);
        if (draws != 0u && indices != 0u) {
            *entity_out = index;
            *draws_out = draws;
            *indices_out = indices;
            return 0;
        }
    }
    return -1;
}

int goldsrc_brush_plan_build(GoldSrcBrushPlan *out,
                             const BspBundleView *bundle)
{
    if (!out || !bundle || !bundle->brush_entities ||
        !bundle->brush_entity_count || !bundle->draws || !bundle->textures)
        return -1;
    memset(out, 0, sizeof(*out));
    static const uint32_t preferred[3] = {0u, 2u, 5u};
    for (uint32_t instance = 0u; instance < 3u; ++instance) {
        if (find_candidate(bundle, preferred[instance], out->entity_indices,
                           instance, &out->entity_indices[instance],
                           &out->draw_counts[instance],
                           &out->index_counts[instance]) != 0 &&
            find_candidate(bundle, UINT32_MAX, out->entity_indices, instance,
                           &out->entity_indices[instance],
                           &out->draw_counts[instance],
                           &out->index_counts[instance]) != 0)
            return -2;
        const BspBundleBrushEntity *const entity =
            &bundle->brush_entities[out->entity_indices[instance]];
        out->source_render_modes[instance] = entity->render_mode;
        out->classname_hashes[instance] = entity->classname_hash;
    }
    out->instance_count = GOLDSRC_BRUSH_INSTANCE_COUNT;
    return 0;
}

int goldsrc_brush_phase4_scene_plan_extend(GoldSrcBrushPlan *plan,
                                           const BspBundleView *bundle)
{
    /* FNV-1a32("func_water") and FNV-1a32("glass_med"). */
    static const uint32_t water_class = UINT32_C(0xd5807c07);
    static const uint32_t glass_texture = UINT32_C(0x10944ba2);
    if (!plan || !bundle || plan->instance_count !=
            GOLDSRC_BRUSH_INSTANCE_COUNT)
        return -1;
    if (find_scene_candidate(
            bundle, water_class, 0u, plan->entity_indices,
            plan->instance_count,
            &plan->entity_indices[GOLDSRC_BRUSH_INSTANCE_WATER],
            &plan->draw_counts[GOLDSRC_BRUSH_INSTANCE_WATER],
            &plan->index_counts[GOLDSRC_BRUSH_INSTANCE_WATER]) != 0)
        return -2;
    plan->instance_count += 1u;
    if (find_scene_candidate(
            bundle, 0u, glass_texture, plan->entity_indices,
            plan->instance_count,
            &plan->entity_indices[GOLDSRC_BRUSH_INSTANCE_GLASS],
            &plan->draw_counts[GOLDSRC_BRUSH_INSTANCE_GLASS],
            &plan->index_counts[GOLDSRC_BRUSH_INSTANCE_GLASS]) != 0)
        return -3;
    plan->instance_count += 1u;
    for (uint32_t instance = GOLDSRC_BRUSH_INSTANCE_WATER;
         instance < plan->instance_count; ++instance) {
        const BspBundleBrushEntity *const entity =
            &bundle->brush_entities[plan->entity_indices[instance]];
        plan->source_render_modes[instance] = entity->render_mode;
        plan->classname_hashes[instance] = entity->classname_hash;
    }
    return plan->instance_count ==
        GOLDSRC_BRUSH_PHASE4_SCENE_INSTANCE_COUNT ? 0 : -4;
}

static Matrix4 model_matrix(const BspBundleBrushEntity *entity,
                            Vec3 target, float angle)
{
    const float center[3] = {
        (entity->mins[0] + entity->maxs[0]) * 0.5f,
        (entity->mins[1] + entity->maxs[1]) * 0.5f,
        (entity->mins[2] + entity->maxs[2]) * 0.5f,
    };
    const float ex = entity->maxs[0] - entity->mins[0];
    const float ey = entity->maxs[1] - entity->mins[1];
    const float ez = entity->maxs[2] - entity->mins[2];
    const float extent = fmaxf(ex, fmaxf(ey, ez));
    const float scale = extent > 0.001f ? 28.0f / extent : 1.0f;
    const float c = cosf(angle);
    const float s = sinf(angle);
    Matrix4 result = {{
        c * scale, 0.0f, -s * scale, 0.0f,
        0.0f, scale, 0.0f, 0.0f,
        s * scale, 0.0f, c * scale, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    }};
    result.v[12] = target.x - (result.v[0] * center[0] +
                                result.v[4] * center[1] +
                                result.v[8] * center[2]);
    result.v[13] = target.y - (result.v[1] * center[0] +
                                result.v[5] * center[1] +
                                result.v[9] * center[2]);
    result.v[14] = target.z - (result.v[2] * center[0] +
                                result.v[6] * center[1] +
                                result.v[10] * center[2]);
    return result;
}

int goldsrc_brush_frame_build(
    GoldSrcBrushFrame *out, const GoldSrcBrushPlan *plan,
    const BspBundleView *bundle, Ps5TransientRing *ring,
    uint32_t slot_index, const void *gpu_mapping, size_t gpu_mapping_bytes,
    const float camera_position[3], const float camera_forward[3],
    float aspect_ratio, uint64_t frame_index)
{
    if (!out || !plan || !bundle || !ring || slot_index >= ring->slot_count ||
        !gpu_mapping || !gpu_mapping_bytes || !camera_position ||
        !camera_forward || plan->instance_count < GOLDSRC_BRUSH_INSTANCE_COUNT ||
        plan->instance_count > GOLDSRC_BRUSH_PHASE4_SCENE_INSTANCE_COUNT)
        return -1;
    memset(out, 0, sizeof(*out));
    out->mode = goldsrc_brush_mode(frame_index);
    const size_t transient_start = ring->slots[slot_index].used;
    float camera_mvp_values[16];
    if (bsp_flat_camera_matrix(camera_mvp_values, camera_position,
                               camera_forward, aspect_ratio) != 0)
        return -2;
    Matrix4 camera_mvp;
    memcpy(camera_mvp.v, camera_mvp_values, sizeof(camera_mvp.v));
    Vec3 forward = {camera_forward[0], camera_forward[1], camera_forward[2]};
    Vec3 up = {0.0f, 1.0f, 0.0f};
    if (normalize(&forward) != 0)
        return -2;
    if (fabsf(dot(forward, up)) > 0.999f)
        up = (Vec3){0.0f, 0.0f, 1.0f};
    Vec3 right = cross(forward, up);
    if (normalize(&right) != 0)
        return -2;
    const Vec3 camera_up = cross(right, forward);
    uint64_t transform_hash = UINT64_C(14695981039346656037);
    for (uint32_t instance = 0u; instance < plan->instance_count; ++instance) {
        const BspBundleBrushEntity *const entity =
            &bundle->brush_entities[plan->entity_indices[instance]];
        const float phase = (float)(frame_index % 3600u) *
                            (0.0035f + 0.0007f * (float)instance);
        static const float lateral_offsets[
            GOLDSRC_BRUSH_PHASE4_SCENE_INSTANCE_COUNT] = {
                -38.0f, 0.0f, 38.0f, -22.0f, 22.0f,
            };
        const float lateral = lateral_offsets[instance];
        const float vertical = 3.0f + 4.0f * sinf(phase * 1.7f + instance);
        const float distance = instance < GOLDSRC_BRUSH_INSTANCE_COUNT
            ? 100.0f : 68.0f;
        const Vec3 target = {
            camera_position[0] + forward.x * distance + right.x * lateral +
                camera_up.x * vertical,
            camera_position[1] + forward.y * distance + right.y * lateral +
                camera_up.y * vertical,
            camera_position[2] + forward.z * distance + right.z * lateral +
                camera_up.z * vertical,
        };
        const Matrix4 model = model_matrix(entity, target, phase);
        const Matrix4 mvp = multiply(camera_mvp, model);
        Ps5TransientSlice constants_slice;
        Ps5TransientTable constants_table;
        if (ps5_transient_ring_allocate(
                ring, slot_index, sizeof(BspResourceConstants), 256u,
                &constants_slice) != PS5_TRANSIENT_OK ||
            !ps5_gpu_span_visible(gpu_mapping, gpu_mapping_bytes,
                                  constants_slice.cpu,
                                  constants_slice.bytes) ||
            ps5_transient_table_allocate(
                ring, slot_index, PS5_GFX1013_VSHARP_DWORDS,
                gpu_mapping, gpu_mapping_bytes,
                &constants_table) != 0)
            return -3;
        BspResourceConstants *const constants = constants_slice.cpu;
        memset(constants, 0, sizeof(*constants));
        memcpy(constants->mvp, mvp.v, sizeof(mvp.v));
        static const float colors[
            GOLDSRC_BRUSH_PHASE4_SCENE_INSTANCE_COUNT][4] = {
            {1.0f, 0.85f, 0.55f, 1.0f},
            {0.25f, 0.85f, 1.0f, 0.48f},
            {1.0f, 0.28f, 0.08f, 0.72f},
            {0.22f, 0.58f, 1.0f, 0.42f},
            {0.72f, 0.92f, 1.0f, 0.30f},
        };
        memcpy(constants->control, colors[instance], sizeof(colors[instance]));
        constants->debug_values[4] = (float)(frame_index & 0xffffu);
        constants->debug_values[8] = (float)slot_index;
        if (ps5_gfx1013_build_constant_vsharp(
                constants_table.words, (uintptr_t)constants,
                sizeof(*constants)) != 0)
            return -3;
        out->constant_tables[instance] = constants_table.words;
        transform_hash = hash_bytes(transform_hash, mvp.v, sizeof(mvp.v));
    }
    out->transform_hash = transform_hash;
    out->transient_bytes =
        ring->slots[slot_index].used - transient_start;
    return 0;
}

int goldsrc_brush_compose_instance(
    uint32_t **cursor, uint32_t *end, const GoldSrcBrushFrame *frame,
    const GoldSrcBrushPlan *plan, const BspResourceFrame *resource_frame,
    const BspBundleView *bundle, GoldSrcBrushInstance instance,
    const void *gpu_mapping, size_t gpu_mapping_bytes, uint64_t modifier,
    BspSetShDirectFn set_sh_direct, BspDrawIndexedFn draw_indexed,
    GoldSrcBrushComposeResult *result)
{
    if (!cursor || !*cursor || !end || *cursor > end || !frame || !plan ||
        !resource_frame || !bundle ||
        (uint32_t)instance >= GOLDSRC_BRUSH_PHASE4_SCENE_INSTANCE_COUNT ||
        (uint32_t)instance >= plan->instance_count || !gpu_mapping ||
        !gpu_mapping_bytes || !modifier || !set_sh_direct || !draw_indexed ||
        !result || !frame->constant_tables[instance] ||
        !resource_frame->map_vertex_table || !resource_frame->texture_tables)
        return -1;
    const BspBundleBrushEntity *const entity =
        &bundle->brush_entities[plan->entity_indices[instance]];
    const uint32_t required = 4u + plan->draw_counts[instance] * 9u;
    if ((size_t)(end - *cursor) < required)
        return -2;
    uint32_t *const start = *cursor;
    const uint32_t gs_values[2] = {
        (uint32_t)(uintptr_t)frame->constant_tables[instance],
        (uint32_t)(uintptr_t)resource_frame->map_vertex_table,
    };
    if (set_sh_direct(cursor, (uint32_t)(end - *cursor),
                      BSP_RESOURCE_GS_SH_OFFSET, gs_values, 2u) != 0)
        return -3;
    const uint32_t face_end = entity->first_face + entity->face_count;
    uint32_t draws = 0u;
    uint32_t indices = 0u;
    for (uint32_t draw = 0u; draw < bundle->draw_count; ++draw) {
        const BspBundleDraw *const source = &bundle->draws[draw];
        if (source->face_id < entity->first_face ||
            source->face_id >= face_end ||
            (bundle->textures[source->base_texture].flags &
             (BSP_BUNDLE_TEXTURE_NODRAW | BSP_BUNDLE_TEXTURE_SKY)))
            continue;
        const uint32_t texture = (uint32_t)(uintptr_t)(
            resource_frame->texture_tables +
            source->base_texture * BSP_TEXTURE_TABLE_DWORDS);
        if (set_sh_direct(cursor, (uint32_t)(end - *cursor),
                          BSP_RESOURCE_PS_SH_OFFSET, &texture, 1u) != 0 ||
            draw_indexed(cursor, (uint32_t)(end - *cursor),
                         source->index_count,
                         bundle->indices + source->first_index,
                         gpu_mapping, gpu_mapping_bytes, modifier) != 0)
            return -3;
        ++draws;
        indices += source->index_count;
    }
    if (draws != plan->draw_counts[instance] ||
        indices != plan->index_counts[instance])
        return -4;
    result->instances += 1u;
    result->draws += draws;
    result->indices += indices;
    result->texture_binds += draws;
    result->command_dwords += (uint32_t)(*cursor - start);
    return (uint32_t)(*cursor - start) == required ? 0 : -5;
}
