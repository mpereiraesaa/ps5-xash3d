#include "ref_agc_live_2d.h"

#include "bsp_resource_draw.h"
#include "bsp_texture_descriptor.h"
#include "ps5_gfx1013_descriptor.h"
#include "ps5_gpu_span.h"
#include "ps5_transient_table.h"
#include "ref_agc_2d_state.h"

#include <math.h>
#include <string.h>

_Static_assert(REF_AGC_LIVE_2D_MAX_QUADS * 4u <= UINT16_MAX,
               "live 2D indices fit u16");

static uint64_t hash_bytes(uint64_t hash, const void *data, size_t bytes)
{
    const uint8_t *cursor = data;
    while (bytes-- != 0u) {
        hash ^= *cursor++;
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static int allocate_slice(Ps5TransientRing *ring, uint32_t slot,
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

static int finite_quad(const RefAgcLive2DCommand *command)
{
    return isfinite(command->x) && isfinite(command->y) &&
           isfinite(command->width) && isfinite(command->height) &&
           isfinite(command->s1) && isfinite(command->t1) &&
           isfinite(command->s2) && isfinite(command->t2) &&
           command->width > 0.0f && command->height > 0.0f;
}

static int blend_from_render_mode(int32_t render_mode,
                                  GoldSrcBlendMode *out)
{
    if (!out || render_mode < GOLDSRC_RENDER_NORMAL ||
        (render_mode >= GOLDSRC_RENDER_MODE_COUNT &&
         render_mode != REF_AGC_2D_SCREEN_FADE_MODULATE))
        return -1;
    if (render_mode == GOLDSRC_RENDER_NORMAL)
        *out = GOLDSRC_BLEND_OPAQUE;
    else if (render_mode == GOLDSRC_RENDER_GLOW ||
             render_mode == GOLDSRC_RENDER_TRANS_ADD)
        *out = GOLDSRC_BLEND_ADDITIVE;
    else if (render_mode == GOLDSRC_RENDER_TRANS_ALPHA)
        *out = GOLDSRC_BLEND_ALPHA_TEST;
    else if (render_mode == REF_AGC_2D_SCREEN_FADE_MODULATE)
        *out = GOLDSRC_BLEND_SCREEN_MODULATE;
    else
        *out = GOLDSRC_BLEND_ALPHA;
    return 0;
}

static void write_quad(GoldSrc2DVertex *vertices, uint16_t *indices,
                       uint32_t quad, const RefAgcLive2DCommand *command)
{
    const uint32_t first_vertex = quad * 4u;
    const uint32_t first_index = quad * 6u;
    const float positions[4][2] = {
        {command->x, command->y},
        {command->x + command->width, command->y},
        {command->x + command->width, command->y + command->height},
        {command->x, command->y + command->height},
    };
    const float uv[4][2] = {
        {command->s1, command->t1}, {command->s2, command->t1},
        {command->s2, command->t2}, {command->s1, command->t2},
    };
    const float inverse_byte = 1.0f / 255.0f;
    for (uint32_t i = 0u; i < 4u; ++i) {
        memcpy(vertices[first_vertex + i].position, positions[i],
               sizeof(positions[i]));
        memcpy(vertices[first_vertex + i].uv, uv[i], sizeof(uv[i]));
        for (uint32_t channel = 0u; channel < 4u; ++channel)
            vertices[first_vertex + i].color[channel] =
                (float)command->color[channel] * inverse_byte;
    }
    const uint16_t quad_indices[6] = {
        (uint16_t)first_vertex, (uint16_t)(first_vertex + 1u),
        (uint16_t)(first_vertex + 2u), (uint16_t)first_vertex,
        (uint16_t)(first_vertex + 2u), (uint16_t)(first_vertex + 3u),
    };
    memcpy(indices + first_index, quad_indices, sizeof(quad_indices));
}

int ref_agc_live_2d_frame_build(
    RefAgcLive2DFrame *out, Ps5TransientRing *ring, uint32_t slot_index,
    const void *gpu_mapping, size_t gpu_mapping_bytes,
    uint32_t framebuffer_width, uint32_t framebuffer_height,
    const RefAgcLiveFrame *live,
    const RefAgcGpuTextureCache *textures)
{
    if (!out || !ring || slot_index >= ring->slot_count || !gpu_mapping ||
        gpu_mapping_bytes == 0u || framebuffer_width == 0u ||
        framebuffer_height == 0u || !live || !textures ||
        live->command_2d_count > REF_AGC_LIVE_MAX_2D_COMMANDS ||
        live->dropped_2d_commands != 0u)
        return REF_AGC_LIVE_2D_INVALID;

    const size_t checkpoint = ring->slots[slot_index].used;
    memset(out, 0, sizeof(*out));
    out->command_hash = UINT64_C(14695981039346656037);
    out->layout_hash = UINT64_C(14695981039346656037);
    if (live->command_2d_count == 0u)
        return REF_AGC_LIVE_2D_OK;

    uint32_t drawable_count = 0u;
    uint32_t fill_count = 0u;
    int mode_enabled = 0;
    for (uint32_t i = 0u; i < live->command_2d_count; ++i) {
        const RefAgcLive2DCommand *command = &live->commands_2d[i];
        out->failed_command = i;
        out->failed_texture = command->texture;
        out->failed_type = command->type;
        out->command_hash = hash_bytes(out->command_hash, command,
                                       sizeof(*command));
        if (command->type == REF_AGC_LIVE_2D_MODE) {
            if (command->enabled > 1u)
                return REF_AGC_LIVE_2D_SEQUENCE_INVALID;
            mode_enabled = command->enabled != 0u;
            ++out->mode_commands;
            continue;
        }
        if (!mode_enabled || !finite_quad(command))
            return REF_AGC_LIVE_2D_SEQUENCE_INVALID;
        if (command->type == REF_AGC_LIVE_2D_STRETCH_PIC) {
            GoldSrcBlendMode blend;
            if (blend_from_render_mode(command->render_mode, &blend) != 0)
                return REF_AGC_LIVE_2D_SEQUENCE_INVALID;
            RefAgcGpuTextureEntry texture;
            if (command->texture <= 0 ||
                ref_agc_gpu_texture_cache_get(
                    textures, (uint32_t)command->texture, &texture) !=
                    REF_AGC_GPU_TEXTURE_OK) {
                ++out->unresolved_textures;
                return REF_AGC_LIVE_2D_TEXTURE_UNRESOLVED;
            }
            ++out->stretch_quads;
        } else if (command->type == REF_AGC_LIVE_2D_FILL_RGBA) {
            ++out->fill_quads;
            ++fill_count;
        } else {
            return REF_AGC_LIVE_2D_SEQUENCE_INVALID;
        }
        ++drawable_count;
    }
    if (drawable_count == 0u)
        return REF_AGC_LIVE_2D_OK;
    out->failed_command = 0u;
    out->failed_texture = 0;
    out->failed_type = 0u;

    Ps5TransientSlice constants_slice, vertices_slice, indices_slice;
    Ps5TransientTable constant_table, vertex_table;
    if (allocate_slice(ring, slot_index, sizeof(GoldSrc2DConstants), 256u,
                       gpu_mapping, gpu_mapping_bytes,
                       &constants_slice) != 0 ||
        allocate_slice(ring, slot_index,
                       (size_t)drawable_count * 4u *
                           sizeof(GoldSrc2DVertex), 16u,
                       gpu_mapping, gpu_mapping_bytes, &vertices_slice) != 0 ||
        allocate_slice(ring, slot_index,
                       (size_t)drawable_count * 6u * sizeof(uint16_t), 2u,
                       gpu_mapping, gpu_mapping_bytes, &indices_slice) != 0 ||
        ps5_transient_table_allocate(
            ring, slot_index, PS5_GFX1013_VSHARP_DWORDS,
            gpu_mapping, gpu_mapping_bytes, &constant_table) != 0 ||
        ps5_transient_table_allocate(
            ring, slot_index, PS5_GFX1013_VSHARP_DWORDS,
            gpu_mapping, gpu_mapping_bytes, &vertex_table) != 0) {
        ring->slots[slot_index].used = checkpoint;
        return REF_AGC_LIVE_2D_TRANSIENT_EXHAUSTED;
    }

    Ps5TransientSlice white_slice = {0};
    uint32_t white_descriptor[BSP_GFX1013_COMBINED_DWORDS] = {0};
    if (fill_count != 0u) {
        if (allocate_slice(ring, slot_index, 256u, 256u,
                           gpu_mapping, gpu_mapping_bytes,
                           &white_slice) != 0) {
            ring->slots[slot_index].used = checkpoint;
            return REF_AGC_LIVE_2D_TRANSIENT_EXHAUSTED;
        }
        memset(white_slice.cpu, 255, white_slice.bytes);
        if (bsp_gfx1013_combined_descriptor(
                white_descriptor, (uintptr_t)white_slice.cpu, 1u, 1u,
                256u, 1u, BSP_TEXTURE_CLAMP_LAST_TEXEL,
                BSP_TEXTURE_FILTER_POINT) != 0) {
            ring->slots[slot_index].used = checkpoint;
            return REF_AGC_LIVE_2D_DESCRIPTOR_FAILED;
        }
    }

    GoldSrc2DConstants *constants = constants_slice.cpu;
    memset(constants, 0, sizeof(*constants));
    const uint32_t canvas_width = live->canvas_width != 0u ?
        live->canvas_width : framebuffer_width;
    const uint32_t canvas_height = live->canvas_height != 0u ?
        live->canvas_height : framebuffer_height;
    constants->projection[0] = 2.0f / (float)canvas_width;
    constants->projection[5] = -2.0f / (float)canvas_height;
    constants->projection[10] = 1.0f;
    constants->projection[12] = -1.0f;
    constants->projection[13] = 1.0f;
    constants->projection[15] = 1.0f;
    constants->color_scale[0] = constants->color_scale[1] =
        constants->color_scale[2] = constants->color_scale[3] = 1.0f;
    if (ps5_gfx1013_build_constant_vsharp(
            constant_table.words, (uintptr_t)constants,
            sizeof(*constants)) != 0) {
        ring->slots[slot_index].used = checkpoint;
        return REF_AGC_LIVE_2D_DESCRIPTOR_FAILED;
    }

    GoldSrc2DVertex *vertices = vertices_slice.cpu;
    uint16_t *indices = indices_slice.cpu;
    mode_enabled = 0;
    uint32_t quad = 0u;
    RefAgcLive2DBatch *batch = NULL;
    for (uint32_t i = 0u; i < live->command_2d_count; ++i) {
        RefAgcLive2DCommand command = live->commands_2d[i];
        if (command.type == REF_AGC_LIVE_2D_MODE) {
            mode_enabled = command.enabled != 0u;
            batch = NULL;
            continue;
        }
        if (!mode_enabled) {
            ring->slots[slot_index].used = checkpoint;
            return REF_AGC_LIVE_2D_SEQUENCE_INVALID;
        }
        GoldSrcBlendMode blend = GOLDSRC_BLEND_ALPHA;
        uint32_t texture_handle = 0u;
        uint32_t fill = command.type == REF_AGC_LIVE_2D_FILL_RGBA;
        uint32_t descriptor[BSP_GFX1013_COMBINED_DWORDS];
        if (fill) {
            /* CL_FillRGBA uses additive only for TransAdd, alpha otherwise. */
            blend = command.render_mode == GOLDSRC_RENDER_TRANS_ADD ?
                GOLDSRC_BLEND_ADDITIVE : GOLDSRC_BLEND_ALPHA;
            command.s1 = command.t1 = command.s2 = command.t2 = 0.5f;
            memcpy(descriptor, white_descriptor, sizeof(descriptor));
        } else {
            if (blend_from_render_mode(command.render_mode, &blend) != 0) {
                ring->slots[slot_index].used = checkpoint;
                return REF_AGC_LIVE_2D_SEQUENCE_INVALID;
            }
            RefAgcGpuTextureEntry texture;
            texture_handle = (uint32_t)command.texture;
            if (ref_agc_gpu_texture_cache_get(
                    textures, texture_handle, &texture) !=
                REF_AGC_GPU_TEXTURE_OK) {
                ring->slots[slot_index].used = checkpoint;
                return REF_AGC_LIVE_2D_TEXTURE_UNRESOLVED;
            }
            memcpy(descriptor, texture.descriptor, sizeof(descriptor));
        }
        if (!batch || batch->blend != blend || batch->fill != fill ||
            batch->texture_handle != texture_handle) {
            if (out->batch_count >= REF_AGC_LIVE_2D_MAX_BATCHES) {
                ring->slots[slot_index].used = checkpoint;
                return REF_AGC_LIVE_2D_TRANSIENT_EXHAUSTED;
            }
            Ps5TransientTable texture_table;
            if (ps5_transient_table_allocate(
                    ring, slot_index, BSP_GFX1013_COMBINED_DWORDS,
                    gpu_mapping, gpu_mapping_bytes, &texture_table) != 0) {
                ring->slots[slot_index].used = checkpoint;
                return REF_AGC_LIVE_2D_TRANSIENT_EXHAUSTED;
            }
            memcpy(texture_table.words, descriptor, sizeof(descriptor));
            batch = &out->batches[out->batch_count++];
            *batch = (RefAgcLive2DBatch){
                .texture_table = texture_table.words,
                .first_index = quad * 6u,
                .texture_handle = texture_handle,
                .blend = blend,
                .fill = fill,
            };
            if (blend == GOLDSRC_BLEND_ALPHA)
                ++out->alpha_batches;
            else if (blend == GOLDSRC_BLEND_ADDITIVE)
                ++out->additive_batches;
            else if (blend == GOLDSRC_BLEND_ALPHA_TEST)
                ++out->masked_batches;
            else if (blend == GOLDSRC_BLEND_SCREEN_MODULATE)
                ++out->modulate_batches;
            else
                ++out->opaque_batches;
        }
        write_quad(vertices, indices, quad, &command);
        batch->index_count += 6u;
        ++quad;
    }
    out->vertex_count = quad * 4u;
    out->index_count = quad * 6u;
    if (ps5_gfx1013_build_vsharp(
            vertex_table.words, (uintptr_t)vertices,
            sizeof(GoldSrc2DVertex), out->vertex_count) != 0) {
        ring->slots[slot_index].used = checkpoint;
        return REF_AGC_LIVE_2D_DESCRIPTOR_FAILED;
    }
    out->constant_table = constant_table.words;
    out->vertex_table = vertex_table.words;
    out->vertices = vertices;
    out->indices = indices;
    out->layout_hash = hash_bytes(out->layout_hash, vertices,
        (size_t)out->vertex_count * sizeof(*vertices));
    out->layout_hash = hash_bytes(out->layout_hash, indices,
        (size_t)out->index_count * sizeof(*indices));
    out->transient_bytes = ring->slots[slot_index].used - checkpoint;
    return REF_AGC_LIVE_2D_OK;
}

int ref_agc_live_2d_compose_batch(
    uint32_t **cursor, uint32_t *end, const RefAgcLive2DFrame *frame,
    uint32_t batch_index, const void *gpu_mapping,
    size_t gpu_mapping_bytes, uint64_t modifier,
    BspSetShDirectFn set_sh_direct, BspDrawIndexedFn draw_indexed,
    RefAgcLive2DComposeResult *result)
{
    enum { SET_TWO_DWORDS = 4, SET_ONE_DWORDS = 3, DRAW_DWORDS = 6,
           REQUIRED_DWORDS = SET_TWO_DWORDS + SET_ONE_DWORDS + DRAW_DWORDS };
    if (!cursor || !*cursor || !end || *cursor > end || !frame ||
        batch_index >= frame->batch_count || !gpu_mapping ||
        gpu_mapping_bytes == 0u || modifier == 0u || !set_sh_direct ||
        !draw_indexed || !result)
        return REF_AGC_LIVE_2D_INVALID;
    const RefAgcLive2DBatch *batch = &frame->batches[batch_index];
    if (!batch->texture_table || batch->index_count == 0u ||
        batch->first_index > frame->index_count ||
        batch->index_count > frame->index_count - batch->first_index ||
        (size_t)(end - *cursor) < REQUIRED_DWORDS ||
        !ps5_gpu_span_visible(gpu_mapping, gpu_mapping_bytes,
                              frame->constant_table, 4u*sizeof(uint32_t)) ||
        !ps5_gpu_span_visible(gpu_mapping, gpu_mapping_bytes,
                              frame->vertex_table, 4u*sizeof(uint32_t)) ||
        !ps5_gpu_span_visible(gpu_mapping, gpu_mapping_bytes,
                              batch->texture_table,
                              BSP_GFX1013_COMBINED_DWORDS*sizeof(uint32_t)) ||
        !ps5_gpu_span_visible(gpu_mapping, gpu_mapping_bytes,
                              frame->indices + batch->first_index,
                              (size_t)batch->index_count*sizeof(uint16_t)))
        return REF_AGC_LIVE_2D_INVALID;
    uint32_t *const start = *cursor;
    const uint32_t gs[2] = {
        (uint32_t)(uintptr_t)frame->constant_table,
        (uint32_t)(uintptr_t)frame->vertex_table,
    };
    const uint32_t ps = (uint32_t)(uintptr_t)batch->texture_table;
    if (set_sh_direct(cursor, (uint32_t)(end - *cursor),
                      BSP_RESOURCE_GS_SH_OFFSET, gs, 2u) != 0 ||
        set_sh_direct(cursor, (uint32_t)(end - *cursor),
                      BSP_RESOURCE_PS_SH_OFFSET, &ps, 1u) != 0 ||
        draw_indexed(cursor, (uint32_t)(end - *cursor),
                     batch->index_count,
                     frame->indices + batch->first_index,
                     gpu_mapping, gpu_mapping_bytes, modifier) != 0)
        return REF_AGC_LIVE_2D_INVALID;
    const uint32_t written = (uint32_t)(*cursor - start);
    if (written != REQUIRED_DWORDS)
        return REF_AGC_LIVE_2D_INVALID;
    ++result->draws;
    result->indices += batch->index_count;
    result->command_dwords += written;
    ++result->texture_binds;
    return REF_AGC_LIVE_2D_OK;
}
