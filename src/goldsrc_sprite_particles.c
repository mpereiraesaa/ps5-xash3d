#include "goldsrc_sprite_particles.h"

#include "bsp_flat_scene.h"
#include "bsp_resource_draw.h"
#include "bsp_texture_descriptor.h"
#include "ps5_gfx1013_descriptor.h"
#include "ps5_gpu_span.h"
#include "ps5_transient_table.h"

#include <math.h>
#include <string.h>

_Static_assert(sizeof(BspBundleVertex) == 32u, "surface vertex ABI");
_Static_assert(sizeof(GoldSrcSpriteParticleConstants) ==
                   GOLDSRC_EFFECT_CONSTANT_DWORDS * sizeof(uint32_t),
               "effect constant ABI");

typedef struct EffectVec3 {
    float x;
    float y;
    float z;
} EffectVec3;

typedef struct EffectBuilder {
    BspBundleVertex *vertices;
    uint16_t *indices;
    uint32_t quads;
} EffectBuilder;

static uint64_t hash_bytes(const void *data, size_t bytes)
{
    const uint8_t *cursor = data;
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t index = 0u; index < bytes; ++index) {
        hash ^= cursor[index];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static float dot(EffectVec3 a, EffectVec3 b)
{
    return a.x*b.x + a.y*b.y + a.z*b.z;
}

static EffectVec3 add(EffectVec3 a, EffectVec3 b)
{
    const EffectVec3 result = {a.x+b.x, a.y+b.y, a.z+b.z};
    return result;
}

static EffectVec3 scale(EffectVec3 value, float amount)
{
    const EffectVec3 result = {
        value.x*amount, value.y*amount, value.z*amount,
    };
    return result;
}

static EffectVec3 cross(EffectVec3 a, EffectVec3 b)
{
    const EffectVec3 result = {
        a.y*b.z - a.z*b.y,
        a.z*b.x - a.x*b.z,
        a.x*b.y - a.y*b.x,
    };
    return result;
}

static int normalize(EffectVec3 *value)
{
    const float length = sqrtf(dot(*value, *value));
    if (!(length > 0.000001f) || !__builtin_isfinite(length))
        return -1;
    value->x /= length;
    value->y /= length;
    value->z /= length;
    return 0;
}

static int camera_basis(const float position[3], const float forward_in[3],
                        EffectVec3 *position_out, EffectVec3 *forward_out,
                        EffectVec3 *right_out, EffectVec3 *up_out)
{
    if (!position || !forward_in || !position_out || !forward_out ||
        !right_out || !up_out)
        return -1;
    EffectVec3 forward = {
        forward_in[0], forward_in[1], forward_in[2],
    };
    if (normalize(&forward) != 0)
        return -1;
    EffectVec3 world_up = {0.0f, 1.0f, 0.0f};
    if (fabsf(dot(forward, world_up)) > 0.999f)
        world_up = (EffectVec3){0.0f, 0.0f, 1.0f};
    EffectVec3 right = cross(forward, world_up);
    if (normalize(&right) != 0)
        return -1;
    *position_out = (EffectVec3){position[0], position[1], position[2]};
    *forward_out = forward;
    *right_out = right;
    *up_out = cross(right, forward);
    return 0;
}

static uint8_t byte_clamp(float value)
{
    if (value <= 0.0f)
        return 0u;
    if (value >= 255.0f)
        return 255u;
    return (uint8_t)value;
}

static void atlas_region(uint8_t *pixels, uint32_t x0, uint32_t width,
                         const float inner[3], const float outer[3],
                         float edge_power)
{
    const float center_x = (float)x0 + (float)width * 0.5f;
    const float center_y = (float)GOLDSRC_EFFECT_ATLAS_HEIGHT * 0.5f;
    const float radius = (float)width * 0.5f;
    for (uint32_t y = 0u; y < GOLDSRC_EFFECT_ATLAS_HEIGHT; ++y)
        for (uint32_t x = x0; x < x0 + width; ++x) {
            const float dx = ((float)x + 0.5f - center_x) / radius;
            const float dy = ((float)y + 0.5f - center_y) / radius;
            const float distance = sqrtf(dx*dx + dy*dy);
            float alpha = 1.0f - distance;
            if (alpha < 0.0f)
                alpha = 0.0f;
            alpha = powf(alpha, edge_power);
            uint8_t *pixel = pixels +
                (size_t)y * GOLDSRC_EFFECT_ATLAS_ROW_PITCH + x*4u;
            for (uint32_t channel = 0u; channel < 3u; ++channel)
                pixel[channel] = byte_clamp(
                    outer[channel] +
                    (inner[channel] - outer[channel]) * alpha);
            pixel[3] = byte_clamp(alpha * 255.0f);
        }
}

static void build_atlas(uint8_t *pixels)
{
    static const float sprite_inner[3] = {255.0f, 248.0f, 180.0f};
    static const float sprite_outer[3] = {255.0f, 52.0f, 8.0f};
    static const float smoke_inner[3] = {190.0f, 220.0f, 205.0f};
    static const float smoke_outer[3] = {35.0f, 60.0f, 50.0f};
    static const float spark_inner[3] = {255.0f, 255.0f, 210.0f};
    static const float spark_outer[3] = {255.0f, 110.0f, 15.0f};
    memset(pixels, 0, (size_t)GOLDSRC_EFFECT_ATLAS_ROW_PITCH *
                         GOLDSRC_EFFECT_ATLAS_HEIGHT);
    atlas_region(pixels, 0u, 32u, sprite_inner, sprite_outer, 0.65f);
    atlas_region(pixels, 32u, 16u, smoke_inner, smoke_outer, 1.65f);
    atlas_region(pixels, 48u, 16u, spark_inner, spark_outer, 0.45f);
}

static int add_billboard(EffectBuilder *builder, EffectVec3 center,
                         EffectVec3 right, EffectVec3 up,
                         float half_width, float half_height,
                         float u0, float v0, float u1, float v1)
{
    if (!builder || !builder->vertices || !builder->indices ||
        !(half_width > 0.0f) || !(half_height > 0.0f) ||
        builder->quads >= GOLDSRC_EFFECT_TOTAL_QUADS)
        return -1;
    const uint32_t vertex = builder->quads * 4u;
    const uint32_t index = builder->quads * 6u;
    const EffectVec3 horizontal = scale(right, half_width);
    const EffectVec3 vertical = scale(up, half_height);
    const EffectVec3 positions[4] = {
        add(add(center, scale(horizontal, -1.0f)), vertical),
        add(add(center, horizontal), vertical),
        add(add(center, horizontal), scale(vertical, -1.0f)),
        add(add(center, scale(horizontal, -1.0f)), scale(vertical, -1.0f)),
    };
    const float uv[4][2] = {{u0,v0},{u1,v0},{u1,v1},{u0,v1}};
    for (uint32_t corner = 0u; corner < 4u; ++corner) {
        BspBundleVertex *const out = &builder->vertices[vertex + corner];
        memset(out, 0, sizeof(*out));
        out->position[0] = positions[corner].x;
        out->position[1] = positions[corner].y;
        out->position[2] = positions[corner].z;
        out->base_uv[0] = uv[corner][0];
        out->base_uv[1] = uv[corner][1];
        out->light_uv[0] = out->light_uv[1] = 0.5f;
    }
    const uint16_t values[6] = {
        (uint16_t)vertex, (uint16_t)(vertex + 1u),
        (uint16_t)(vertex + 2u), (uint16_t)vertex,
        (uint16_t)(vertex + 2u), (uint16_t)(vertex + 3u),
    };
    memcpy(builder->indices + index, values, sizeof(values));
    ++builder->quads;
    return 0;
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

GoldSrcSpriteParticleMode goldsrc_sprite_particle_mode(uint64_t frame_index)
{
    return (GoldSrcSpriteParticleMode)(
        (frame_index / GOLDSRC_EFFECT_HOLD_FRAMES) %
        GOLDSRC_EFFECT_MODE_COUNT);
}

const char *goldsrc_sprite_particle_mode_name(GoldSrcSpriteParticleMode mode)
{
    static const char *const names[GOLDSRC_EFFECT_MODE_COUNT] = {
        "control", "sprite", "particles", "combined",
    };
    return (unsigned)mode < GOLDSRC_EFFECT_MODE_COUNT
        ? names[mode] : "invalid";
}

int goldsrc_sprite_particle_frame_build(
    GoldSrcSpriteParticleFrame *out, Ps5TransientRing *ring,
    uint32_t slot_index, const void *gpu_mapping, size_t gpu_mapping_bytes,
    const float camera_position[3], const float camera_forward[3],
    float aspect_ratio, uint64_t frame_index)
{
    if (!out || !ring || slot_index >= ring->slot_count || !gpu_mapping ||
        gpu_mapping_bytes == 0u || !camera_position || !camera_forward ||
        !(aspect_ratio > 0.0f) || !__builtin_isfinite(aspect_ratio))
        return -1;
    const size_t checkpoint = ring->slots[slot_index].used;
    memset(out, 0, sizeof(*out));
    Ps5TransientSlice atlas_slice, constants_slice, vertices_slice;
    Ps5TransientSlice indices_slice;
    Ps5TransientTable constant_table, vertex_table, texture_table;
    const size_t atlas_bytes =
        (size_t)GOLDSRC_EFFECT_ATLAS_ROW_PITCH *
        GOLDSRC_EFFECT_ATLAS_HEIGHT;
    if (allocate_slice(ring, slot_index, atlas_bytes, 256u,
                       gpu_mapping, gpu_mapping_bytes, &atlas_slice) != 0 ||
        allocate_slice(ring, slot_index,
                       sizeof(GoldSrcSpriteParticleConstants), 256u,
                       gpu_mapping, gpu_mapping_bytes,
                       &constants_slice) != 0 ||
        allocate_slice(ring, slot_index,
                       GOLDSRC_EFFECT_TOTAL_QUADS * 4u *
                           sizeof(BspBundleVertex), 16u,
                       gpu_mapping, gpu_mapping_bytes,
                       &vertices_slice) != 0 ||
        allocate_slice(ring, slot_index,
                       GOLDSRC_EFFECT_TOTAL_QUADS * 6u * sizeof(uint16_t),
                       2u, gpu_mapping, gpu_mapping_bytes,
                       &indices_slice) != 0 ||
        ps5_transient_table_allocate(
            ring, slot_index, PS5_GFX1013_VSHARP_DWORDS,
            gpu_mapping, gpu_mapping_bytes, &constant_table) != 0 ||
        ps5_transient_table_allocate(
            ring, slot_index, PS5_GFX1013_VSHARP_DWORDS,
            gpu_mapping, gpu_mapping_bytes, &vertex_table) != 0 ||
        ps5_transient_table_allocate(
            ring, slot_index, BSP_TEXTURE_TABLE_DWORDS,
            gpu_mapping, gpu_mapping_bytes, &texture_table) != 0) {
        ring->slots[slot_index].used = checkpoint;
        return -2;
    }

    build_atlas(atlas_slice.cpu);
    GoldSrcSpriteParticleConstants *const constants = constants_slice.cpu;
    memset(constants, 0, sizeof(*constants));
    if (bsp_flat_camera_matrix(constants->mvp, camera_position,
                               camera_forward, aspect_ratio) != 0) {
        ring->slots[slot_index].used = checkpoint;
        return -3;
    }
    constants->render_color[0] = constants->render_color[1] =
        constants->render_color[2] = constants->render_color[3] = 1.0f;
    if (ps5_gfx1013_build_constant_vsharp(
            constant_table.words, (uintptr_t)constants,
            sizeof(*constants)) != 0 ||
        bsp_gfx1013_combined_descriptor(
            texture_table.words, (uintptr_t)atlas_slice.cpu,
            GOLDSRC_EFFECT_ATLAS_WIDTH, GOLDSRC_EFFECT_ATLAS_HEIGHT,
            GOLDSRC_EFFECT_ATLAS_ROW_PITCH, 1u,
            BSP_TEXTURE_CLAMP_LAST_TEXEL,
            BSP_TEXTURE_FILTER_BILINEAR) != 0 ||
        bsp_gfx1013_combined_descriptor(
            texture_table.words + BSP_GFX1013_COMBINED_DWORDS,
            (uintptr_t)atlas_slice.cpu,
            GOLDSRC_EFFECT_ATLAS_WIDTH, GOLDSRC_EFFECT_ATLAS_HEIGHT,
            GOLDSRC_EFFECT_ATLAS_ROW_PITCH, 1u,
            BSP_TEXTURE_CLAMP_LAST_TEXEL,
            BSP_TEXTURE_FILTER_BILINEAR) != 0) {
        ring->slots[slot_index].used = checkpoint;
        return -4;
    }

    EffectVec3 camera, forward, right, up;
    if (camera_basis(camera_position, camera_forward, &camera, &forward,
                     &right, &up) != 0) {
        ring->slots[slot_index].used = checkpoint;
        return -5;
    }
    EffectBuilder builder = {
        vertices_slice.cpu, indices_slice.cpu, 0u,
    };
    const float time = (float)(frame_index % UINT64_C(3600)) / 60.0f;
    const float sprite_half = 20.0f + 2.5f*sinf(time*1.7f);
    EffectVec3 sprite_center = add(camera, scale(forward, 112.0f));
    sprite_center = add(sprite_center, scale(right, -34.0f));
    sprite_center = add(sprite_center, scale(up, 12.0f));
    if (add_billboard(&builder, sprite_center, right, up,
                      sprite_half, sprite_half, 0.5f/64.0f, 0.5f/32.0f,
                      31.5f/64.0f, 31.5f/32.0f) != 0)
        goto exhausted;
    out->sprite_first_index = 0u;
    out->sprite_index_count = GOLDSRC_EFFECT_SPRITE_QUADS * 6u;
    out->sprite_quads = GOLDSRC_EFFECT_SPRITE_QUADS;

    out->alpha_particle_first_index = out->sprite_index_count;
    EffectVec3 smoke_origin = add(camera, scale(forward, 96.0f));
    smoke_origin = add(smoke_origin, scale(right, 20.0f));
    for (uint32_t index = 0u;
         index < GOLDSRC_EFFECT_ALPHA_PARTICLE_QUADS; ++index) {
        const float seed = (float)index * 2.3999632f;
        const float phase = time*0.55f + seed;
        const float radius = 5.0f + (float)(index % 8u) * 2.1f;
        EffectVec3 center = add(smoke_origin,
            scale(right, cosf(phase)*radius));
        center = add(center, scale(up,
            sinf(phase*0.73f)*radius + (float)(index % 6u)*2.5f - 5.0f));
        center = add(center, scale(forward,
            (float)((int32_t)(index % 5u) - 2) * 2.0f));
        const float half = 3.5f + (float)(index % 5u)*0.65f;
        if (add_billboard(&builder, center, right, up, half, half,
                          32.5f/64.0f, 8.5f/32.0f,
                          47.5f/64.0f, 23.5f/32.0f) != 0)
            goto exhausted;
    }
    out->alpha_particle_index_count =
        GOLDSRC_EFFECT_ALPHA_PARTICLE_QUADS * 6u;
    out->alpha_particles = GOLDSRC_EFFECT_ALPHA_PARTICLE_QUADS;

    out->additive_particle_first_index =
        out->alpha_particle_first_index + out->alpha_particle_index_count;
    EffectVec3 spark_origin = add(camera, scale(forward, 82.0f));
    spark_origin = add(spark_origin, scale(right, 16.0f));
    for (uint32_t index = 0u;
         index < GOLDSRC_EFFECT_ADDITIVE_PARTICLE_QUADS; ++index) {
        const float seed = (float)index * 1.618034f;
        const float phase = time*2.2f + seed;
        const float radius = 7.0f + (float)(index % 12u)*1.55f;
        EffectVec3 center = add(spark_origin,
            scale(right, cosf(phase)*radius));
        center = add(center, scale(up,
            sinf(phase)*radius*0.70f +
            sinf(time*1.3f + seed)*5.0f));
        center = add(center, scale(forward,
            (float)((int32_t)(index % 7u) - 3)*1.25f));
        const float half = 1.8f + (float)(index % 4u)*0.45f;
        if (add_billboard(&builder, center, right, up, half, half,
                          48.5f/64.0f, 8.5f/32.0f,
                          63.5f/64.0f, 23.5f/32.0f) != 0)
            goto exhausted;
    }
    out->additive_particle_index_count =
        GOLDSRC_EFFECT_ADDITIVE_PARTICLE_QUADS * 6u;
    out->additive_particles = GOLDSRC_EFFECT_ADDITIVE_PARTICLE_QUADS;
    out->vertex_count = builder.quads * 4u;
    if (builder.quads != GOLDSRC_EFFECT_TOTAL_QUADS ||
        ps5_gfx1013_build_vsharp(
            vertex_table.words, (uintptr_t)builder.vertices,
            sizeof(BspBundleVertex), out->vertex_count) != 0)
        goto exhausted;

    out->constant_table = constant_table.words;
    out->vertex_table = vertex_table.words;
    out->texture_table = texture_table.words;
    out->vertices = builder.vertices;
    out->indices = builder.indices;
    out->mode = goldsrc_sprite_particle_mode(frame_index);
    out->atlas_hash = hash_bytes(atlas_slice.cpu, atlas_bytes);
    out->layout_hash = hash_bytes(
        builder.vertices, (size_t)out->vertex_count * sizeof(*builder.vertices));
    out->layout_hash ^= hash_bytes(
        builder.indices,
        (size_t)GOLDSRC_EFFECT_TOTAL_QUADS * 6u * sizeof(*builder.indices));
    out->transient_bytes = ring->slots[slot_index].used - checkpoint;
    return 0;

exhausted:
    ring->slots[slot_index].used = checkpoint;
    memset(out, 0, sizeof(*out));
    return -6;
}

int goldsrc_sprite_particle_compose_range(
    uint32_t **cursor, uint32_t *end,
    const GoldSrcSpriteParticleFrame *frame, uint32_t first_index,
    uint32_t index_count, const void *gpu_mapping, size_t gpu_mapping_bytes,
    uint64_t modifier, BspSetShDirectFn set_sh_direct,
    BspDrawIndexedFn draw_indexed,
    GoldSrcSpriteParticleComposeResult *result)
{
    enum {
        SET_TWO_DWORDS = 4,
        SET_ONE_DWORDS = 3,
        DRAW_DWORDS = 6,
        REQUIRED_DWORDS = SET_TWO_DWORDS + SET_ONE_DWORDS + DRAW_DWORDS,
    };
    const uint32_t total_indices =
        frame ? frame->sprite_index_count +
                    frame->alpha_particle_index_count +
                    frame->additive_particle_index_count : 0u;
    if (!cursor || !*cursor || !end || *cursor > end || !frame ||
        !gpu_mapping || gpu_mapping_bytes == 0u || modifier == 0u ||
        !set_sh_direct || !draw_indexed || !result || index_count == 0u ||
        first_index > total_indices ||
        index_count > total_indices - first_index ||
        (size_t)(end - *cursor) < REQUIRED_DWORDS ||
        !ps5_gpu_span_visible(gpu_mapping, gpu_mapping_bytes,
                              frame->constant_table,
                              PS5_GFX1013_VSHARP_DWORDS*sizeof(uint32_t)) ||
        !ps5_gpu_span_visible(gpu_mapping, gpu_mapping_bytes,
                              frame->vertex_table,
                              PS5_GFX1013_VSHARP_DWORDS*sizeof(uint32_t)) ||
        !ps5_gpu_span_visible(gpu_mapping, gpu_mapping_bytes,
                              frame->texture_table,
                              BSP_TEXTURE_TABLE_DWORDS*sizeof(uint32_t)) ||
        !ps5_gpu_span_visible(gpu_mapping, gpu_mapping_bytes,
                              frame->indices + first_index,
                              (size_t)index_count*sizeof(uint16_t)))
        return -1;
    uint32_t *const start = *cursor;
    const uint32_t gs[2] = {
        (uint32_t)(uintptr_t)frame->constant_table,
        (uint32_t)(uintptr_t)frame->vertex_table,
    };
    const uint32_t ps = (uint32_t)(uintptr_t)frame->texture_table;
    if (set_sh_direct(cursor, (uint32_t)(end - *cursor),
                      BSP_RESOURCE_GS_SH_OFFSET, gs, 2u) != 0 ||
        set_sh_direct(cursor, (uint32_t)(end - *cursor),
                      BSP_RESOURCE_PS_SH_OFFSET, &ps, 1u) != 0 ||
        draw_indexed(cursor, (uint32_t)(end - *cursor), index_count,
                     frame->indices + first_index, gpu_mapping,
                     gpu_mapping_bytes, modifier) != 0)
        return -2;
    const uint32_t written = (uint32_t)(*cursor - start);
    if (written != REQUIRED_DWORDS)
        return -3;
    ++result->draws;
    result->indices += index_count;
    result->command_dwords += written;
    return 0;
}
