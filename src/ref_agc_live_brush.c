#include "ref_agc_live_brush.h"

#include "bsp_flat_scene.h"
#include "ps5_gfx1013_descriptor.h"
#include "ps5_gpu_span.h"
#include "ps5_transient_table.h"

#include <math.h>
#include <string.h>

typedef struct Matrix4 { float v[16]; } Matrix4;

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

static void goldsrc_rotation(const float angles[3], float scale,
                             float out[3][3])
{
    const float radians = 3.14159265358979323846f / 180.0f;
    const float sy = sinf(angles[1] * radians);
    const float cy = cosf(angles[1] * radians);
    const float sp = sinf(angles[0] * radians);
    const float cp = cosf(angles[0] * radians);
    const float sr = sinf(angles[2] * radians);
    const float cr = cosf(angles[2] * radians);
    out[0][0] = cp * cy * scale;
    out[0][1] = (sr * sp * cy - cr * sy) * scale;
    out[0][2] = (cr * sp * cy + sr * sy) * scale;
    out[1][0] = cp * sy * scale;
    out[1][1] = (sr * sp * sy + cr * cy) * scale;
    out[1][2] = (cr * sp * sy - sr * cy) * scale;
    out[2][0] = -sp * scale;
    out[2][1] = sr * cp * scale;
    out[2][2] = cr * cp * scale;
}

static void agc_to_goldsrc(const float in[3], float out[3])
{
    out[0] = in[0]; out[1] = -in[2]; out[2] = in[1];
}

static void goldsrc_to_agc(const float in[3], float out[3])
{
    out[0] = in[0]; out[1] = in[2]; out[2] = -in[1];
}

static Matrix4 entity_model_matrix(const RefAgcLiveEntity *entity)
{
    float source_rotation[3][3];
    const float scale = entity->model_type == REF_AGC_LIVE_MODEL_BRUSH ||
            !(entity->scale > 0.0f) ? 1.0f : entity->scale;
    goldsrc_rotation(entity->angles, scale, source_rotation);
    Matrix4 result = {{0}};
    for (uint32_t column = 0u; column < 3u; ++column) {
        float agc_basis[3] = {0};
        float source_basis[3];
        float source_rotated[3] = {0};
        float agc_rotated[3];
        agc_basis[column] = 1.0f;
        agc_to_goldsrc(agc_basis, source_basis);
        for (uint32_t row = 0u; row < 3u; ++row)
            for (uint32_t k = 0u; k < 3u; ++k)
                source_rotated[row] +=
                    source_rotation[row][k] * source_basis[k];
        goldsrc_to_agc(source_rotated, agc_rotated);
        for (uint32_t row = 0u; row < 3u; ++row)
            result.v[column * 4u + row] = agc_rotated[row];
    }
    float agc_origin[3];
    goldsrc_to_agc(entity->origin, agc_origin);
    result.v[12] = agc_origin[0];
    result.v[13] = agc_origin[1];
    result.v[14] = agc_origin[2];
    result.v[15] = 1.0f;
    return result;
}

static RefAgcLiveBrushClass render_class(int mode)
{
    if (mode == REF_AGC_LIVE_RENDER_TRANS_ADD ||
        mode == REF_AGC_LIVE_RENDER_GLOW)
        return REF_AGC_LIVE_BRUSH_ADDITIVE;
    if (mode != REF_AGC_LIVE_RENDER_NORMAL)
        return REF_AGC_LIVE_BRUSH_ALPHA;
    return REF_AGC_LIVE_BRUSH_OPAQUE;
}

int ref_agc_live_brush_frame_build(
    RefAgcLiveBrushFrame *out, const RefAgcLiveFrame *live,
    Ps5TransientRing *ring, uint32_t slot_index,
    const void *gpu_mapping, size_t gpu_mapping_bytes,
    const float camera_position[3], const float camera_forward[3],
    float aspect_ratio)
{
    if (!out || !live || !ring || slot_index >= ring->slot_count ||
        !gpu_mapping || !gpu_mapping_bytes || !camera_position ||
        !camera_forward || !(aspect_ratio > 0.0f) ||
        !__builtin_isfinite(aspect_ratio))
        return -1;
    const size_t checkpoint = ring->slots[slot_index].used;
    memset(out, 0, sizeof(*out));
    float camera_values[16];
    if (bsp_flat_camera_matrix(camera_values, camera_position,
                               camera_forward, aspect_ratio) != 0)
        return -2;
    Matrix4 camera;
    memcpy(camera.v, camera_values, sizeof(camera.v));
    uint64_t transform_hash = UINT64_C(14695981039346656037);
    for (uint32_t index = 0u; index < live->entity_count; ++index) {
        const RefAgcLiveEntity *entity = &live->entities[index];
        if (entity->model_type != REF_AGC_LIVE_MODEL_BRUSH)
            continue;
        if (entity->surface_count == 0u ||
            entity->first_surface >= live->world.surfaces ||
            entity->surface_count >
                live->world.surfaces - entity->first_surface) {
            ++out->rejected;
            continue;
        }
        if (out->count >= REF_AGC_LIVE_MAX_BRUSH_ENTITIES)
            goto exhausted;
        Ps5TransientSlice constant_slice;
        Ps5TransientTable constant_table;
        if (ps5_transient_ring_allocate(
                ring, slot_index, sizeof(BspResourceConstants), 256u,
                &constant_slice) != PS5_TRANSIENT_OK ||
            !ps5_gpu_span_visible(gpu_mapping, gpu_mapping_bytes,
                                  constant_slice.cpu,
                                  constant_slice.bytes) ||
            ps5_transient_table_allocate(
                ring, slot_index, PS5_GFX1013_VSHARP_DWORDS,
                gpu_mapping, gpu_mapping_bytes, &constant_table) != 0)
            goto exhausted;
        const Matrix4 model = entity_model_matrix(entity);
        const Matrix4 mvp = multiply(camera, model);
        BspResourceConstants *constants = constant_slice.cpu;
        memset(constants, 0, sizeof(*constants));
        memcpy(constants->mvp, mvp.v, sizeof(mvp.v));
        const RefAgcLiveBrushClass classification =
            render_class(entity->render_mode);
        constants->control[0] = entity->render_color[0] / 255.0f;
        constants->control[1] = entity->render_color[1] / 255.0f;
        constants->control[2] = entity->render_color[2] / 255.0f;
        if (entity->render_color[0] == 0u &&
            entity->render_color[1] == 0u &&
            entity->render_color[2] == 0u)
            constants->control[0] = constants->control[1] =
                constants->control[2] = 1.0f;
        constants->control[3] = classification == REF_AGC_LIVE_BRUSH_OPAQUE
            ? 1.0f : entity->render_amount / 255.0f;
        if (ps5_gfx1013_build_constant_vsharp(
                constant_table.words, (uintptr_t)constants,
                sizeof(*constants)) != 0)
            goto exhausted;
        RefAgcLiveBrushEntry *entry = &out->entries[out->count++];
        entry->constant_table = constant_table.words;
        entry->live_entity = index;
        entry->first_surface = entity->first_surface;
        entry->surface_count = entity->surface_count;
        entry->render_mode = (uint32_t)entity->render_mode;
        entry->render_class = classification;
        if (classification == REF_AGC_LIVE_BRUSH_OPAQUE)
            ++out->opaque_count;
        else if (classification == REF_AGC_LIVE_BRUSH_ALPHA)
            ++out->alpha_count;
        else
            ++out->additive_count;
        transform_hash = hash_bytes(transform_hash, mvp.v, sizeof(mvp.v));
    }
    out->transform_hash = transform_hash;
    out->transient_bytes = ring->slots[slot_index].used - checkpoint;
    return 0;

exhausted:
    ring->slots[slot_index].used = checkpoint;
    memset(out, 0, sizeof(*out));
    return -3;
}

int ref_agc_live_brush_compose_entry(
    uint32_t **cursor, uint32_t *end, const RefAgcLiveBrushFrame *frame,
    uint32_t entry_index, const RefAgcGpuWorldCache *cache,
    uint32_t flag_mask, uint32_t flag_value,
    const void *gpu_mapping, size_t gpu_mapping_bytes, uint64_t modifier,
    BspSetShDirectFn set_sh_direct, BspDrawIndexedFn draw_indexed,
    RefAgcGpuWorldComposeResult *out)
{
    if (!frame || entry_index >= frame->count)
        return -1;
    const RefAgcLiveBrushEntry *entry = &frame->entries[entry_index];
    return ref_agc_gpu_world_compose_surface_range(
        cursor, end, cache, entry->first_surface, entry->surface_count,
        flag_mask, flag_value, entry->constant_table, gpu_mapping,
        gpu_mapping_bytes, modifier, set_sh_direct, draw_indexed, out);
}
