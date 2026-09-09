#include "ref_agc_skybox.h"

#include "bsp_resource_draw.h"
#include "ps5_gfx1013_descriptor.h"
#include "ps5_gpu_span.h"
#include "ps5_transient_table.h"

#include <string.h>

_Static_assert(REF_AGC_SKYBOX_SIDES == 6, "GoldSrc skybox ABI");
_Static_assert(sizeof(BspBundleVertex) == 32u, "skybox vertex ABI");

static const int sky_texture_order[REF_AGC_SKYBOX_SIDES] = {
    0, 2, 1, 3, 4, 5,
};

/* Exact GoldSrc gl_warp.c axis projection. Values index (s, t, far) with
 * sign; the resulting Z-up point is converted to the AGC Y-up convention. */
static const int st_to_vec[REF_AGC_SKYBOX_SIDES][3] = {
    { 3, -1,  2}, {-3,  1,  2}, { 1,  3,  2},
    {-1, -3,  2}, {-2, -1,  3}, { 2, -1, -3},
};

static uint64_t hash_bytes(uint64_t hash, const void *data, size_t bytes)
{
    const uint8_t *cursor = data;
    while (bytes-- != 0u) {
        hash ^= *cursor++;
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static int allocate(Ps5TransientRing *ring, uint32_t slot,
                    size_t bytes, size_t alignment,
                    const void *mapping, size_t mapping_bytes,
                    Ps5TransientSlice *out)
{
    return ps5_transient_ring_allocate(ring, slot, bytes, alignment, out) ==
                   PS5_TRANSIENT_OK &&
               ps5_gpu_span_visible(mapping, mapping_bytes,
                                    out->cpu, out->bytes)
           ? 0 : -1;
}

static void build_vertex(BspBundleVertex *out, float s, float t, int axis)
{
    const float source[3] = {s * 4096.0f, t * 4096.0f, 4096.0f};
    float engine[3];
    for (int component = 0; component < 3; ++component) {
        const int selector = st_to_vec[axis][component];
        engine[component] = selector < 0
            ? -source[-selector - 1] : source[selector - 1];
    }
    memset(out, 0, sizeof(*out));
    out->position[0] = engine[0];
    out->position[1] = engine[2];
    out->position[2] = -engine[1];
    out->base_uv[0] = (s + 1.0f) * 0.5f;
    out->base_uv[1] = 1.0f - (t + 1.0f) * 0.5f;
    out->light_uv[0] = out->light_uv[1] = 0.5f;
    out->face_id = (uint32_t)axis;
}

int ref_agc_skybox_frame_build(
    RefAgcSkyboxFrame *out, Ps5TransientRing *ring,
    uint32_t slot_index, const void *gpu_mapping,
    size_t gpu_mapping_bytes, const RefAgcLiveSky *sky,
    const RefAgcGpuTextureCache *textures)
{
    if (!out || !ring || slot_index >= ring->slot_count || !gpu_mapping ||
        gpu_mapping_bytes == 0u || !sky || !textures ||
        !textures->initialized)
        return REF_AGC_SKYBOX_INVALID;
    const size_t checkpoint = ring->slots[slot_index].used;
    memset(out, 0, sizeof(*out));
    if (!sky->active)
        return REF_AGC_SKYBOX_INACTIVE;
    if (sky->revision == 0u)
        return REF_AGC_SKYBOX_INVALID;
    Ps5TransientSlice vertex_slice, index_slice;
    Ps5TransientTable vertex_table;
    if (allocate(ring, slot_index,
                 REF_AGC_SKYBOX_VERTICES * sizeof(BspBundleVertex), 16u,
                 gpu_mapping, gpu_mapping_bytes, &vertex_slice) != 0 ||
        allocate(ring, slot_index,
                 REF_AGC_SKYBOX_INDICES * sizeof(uint16_t), 2u,
                 gpu_mapping, gpu_mapping_bytes, &index_slice) != 0 ||
        ps5_transient_table_allocate(
            ring, slot_index, PS5_GFX1013_VSHARP_DWORDS,
            gpu_mapping, gpu_mapping_bytes, &vertex_table) != 0)
        goto transient_failed;

    BspBundleVertex *vertices = vertex_slice.cpu;
    uint16_t *indices = index_slice.cpu;
    static const float corners[4][2] = {
        {-1.0f, -1.0f}, {-1.0f, 1.0f},
        {1.0f, 1.0f}, {1.0f, -1.0f},
    };
    uint64_t texture_hash = UINT64_C(14695981039346656037);
    for (uint32_t axis = 0u; axis < REF_AGC_SKYBOX_SIDES; ++axis) {
        const uint32_t vertex = axis * 4u;
        const uint32_t index = axis * 6u;
        for (uint32_t corner = 0u; corner < 4u; ++corner)
            build_vertex(&vertices[vertex + corner],
                         corners[corner][0], corners[corner][1],
                         (int)axis);
        const uint16_t face_indices[6] = {
            (uint16_t)vertex, (uint16_t)(vertex + 1u),
            (uint16_t)(vertex + 2u), (uint16_t)vertex,
            (uint16_t)(vertex + 2u), (uint16_t)(vertex + 3u),
        };
        memcpy(indices + index, face_indices, sizeof(face_indices));

        const uint32_t handle =
            sky->texture_handles[sky_texture_order[axis]];
        RefAgcGpuTextureEntry texture;
        Ps5TransientTable texture_table;
        if (handle == 0u ||
            ref_agc_gpu_texture_cache_get(textures, handle, &texture) !=
                REF_AGC_GPU_TEXTURE_OK)
            goto texture_unresolved;
        if (ps5_transient_table_allocate(
                ring, slot_index,
                REF_AGC_GPU_TEXTURE_DESCRIPTOR_DWORDS * 2u,
                gpu_mapping, gpu_mapping_bytes, &texture_table) != 0)
            goto texture_table_failed;
        memcpy(texture_table.words, texture.descriptor,
               sizeof(texture.descriptor));
        memcpy(texture_table.words + REF_AGC_GPU_TEXTURE_DESCRIPTOR_DWORDS,
               texture.descriptor, sizeof(texture.descriptor));
        out->texture_tables[axis] = texture_table.words;
        out->texture_handles[axis] = handle;
        texture_hash = hash_bytes(texture_hash, &handle, sizeof(handle));
        texture_hash = hash_bytes(texture_hash, &texture.content_hash,
                                  sizeof(texture.content_hash));
    }
    if (ps5_gfx1013_build_vsharp(
            vertex_table.words, (uintptr_t)vertices,
            sizeof(BspBundleVertex), REF_AGC_SKYBOX_VERTICES) != 0)
        goto vertex_descriptor_failed;
    out->vertex_table = vertex_table.words;
    out->vertices = vertices;
    out->indices = indices;
    out->sky_revision = sky->revision;
    out->geometry_hash = hash_bytes(
        UINT64_C(14695981039346656037), vertices,
        REF_AGC_SKYBOX_VERTICES * sizeof(*vertices));
    out->geometry_hash = hash_bytes(
        out->geometry_hash, indices,
        REF_AGC_SKYBOX_INDICES * sizeof(*indices));
    out->texture_hash = texture_hash;
    out->transient_bytes = ring->slots[slot_index].used - checkpoint;
    out->active = 1u;
    return REF_AGC_SKYBOX_OK;

transient_failed:
    ring->slots[slot_index].used = checkpoint;
    memset(out, 0, sizeof(*out));
    return REF_AGC_SKYBOX_TRANSIENT_EXHAUSTED;
texture_unresolved:
    ring->slots[slot_index].used = checkpoint;
    memset(out, 0, sizeof(*out));
    return REF_AGC_SKYBOX_TEXTURE_UNRESOLVED;
texture_table_failed:
    ring->slots[slot_index].used = checkpoint;
    memset(out, 0, sizeof(*out));
    return REF_AGC_SKYBOX_TEXTURE_TABLE_FAILED;
vertex_descriptor_failed:
    ring->slots[slot_index].used = checkpoint;
    memset(out, 0, sizeof(*out));
    return REF_AGC_SKYBOX_VERTEX_DESCRIPTOR_FAILED;
}

int ref_agc_skybox_compose(
    uint32_t **cursor, uint32_t *end, const RefAgcSkyboxFrame *frame,
    const uint32_t *constant_table, const void *gpu_mapping,
    size_t gpu_mapping_bytes, uint64_t modifier,
    BspSetShDirectFn set_sh_direct, BspDrawIndexedFn draw_indexed,
    RefAgcSkyboxComposeResult *result)
{
    enum { DWORDS_PER_DRAW = 13 };
    if (!cursor || !*cursor || !end || *cursor > end || !frame ||
        !frame->active || !constant_table || !gpu_mapping ||
        gpu_mapping_bytes == 0u || modifier == 0u || !set_sh_direct ||
        !draw_indexed || !result ||
        (size_t)(end - *cursor) <
            REF_AGC_SKYBOX_SIDES * DWORDS_PER_DRAW ||
        !ps5_gpu_span_visible(
            gpu_mapping, gpu_mapping_bytes, constant_table,
            PS5_GFX1013_VSHARP_DWORDS * sizeof(uint32_t)) ||
        !ps5_gpu_span_visible(
            gpu_mapping, gpu_mapping_bytes, frame->vertex_table,
            PS5_GFX1013_VSHARP_DWORDS * sizeof(uint32_t)) ||
        !ps5_gpu_span_visible(
            gpu_mapping, gpu_mapping_bytes, frame->indices,
            REF_AGC_SKYBOX_INDICES * sizeof(uint16_t)))
        return -1;
    uint32_t *const start = *cursor;
    memset(result, 0, sizeof(*result));
    for (uint32_t side = 0u; side < REF_AGC_SKYBOX_SIDES; ++side) {
        if (!ps5_gpu_span_visible(
                gpu_mapping, gpu_mapping_bytes,
                frame->texture_tables[side],
                REF_AGC_GPU_TEXTURE_DESCRIPTOR_DWORDS * 2u *
                    sizeof(uint32_t)))
            return -1;
        const uint32_t gs[2] = {
            (uint32_t)(uintptr_t)constant_table,
            (uint32_t)(uintptr_t)frame->vertex_table,
        };
        const uint32_t ps =
            (uint32_t)(uintptr_t)frame->texture_tables[side];
        if (set_sh_direct(cursor, (uint32_t)(end - *cursor),
                          BSP_RESOURCE_GS_SH_OFFSET, gs, 2u) != 0 ||
            set_sh_direct(cursor, (uint32_t)(end - *cursor),
                          BSP_RESOURCE_PS_SH_OFFSET, &ps, 1u) != 0 ||
            draw_indexed(cursor, (uint32_t)(end - *cursor), 6u,
                         frame->indices + side * 6u,
                         gpu_mapping, gpu_mapping_bytes, modifier) != 0)
            return -2;
        ++result->draws;
        result->indices += 6u;
    }
    result->command_dwords = (uint32_t)(*cursor - start);
    return result->draws == REF_AGC_SKYBOX_SIDES &&
                   result->indices == REF_AGC_SKYBOX_INDICES &&
                   result->command_dwords ==
                       REF_AGC_SKYBOX_SIDES * DWORDS_PER_DRAW
               ? 0 : -3;
}
