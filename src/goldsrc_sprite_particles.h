#ifndef PS5_XASH3D_GOLDSRC_SPRITE_PARTICLES_H
#define PS5_XASH3D_GOLDSRC_SPRITE_PARTICLES_H

#include "bsp_bundle.h"
#include "bsp_flat_draw.h"
#include "ps5_transient_ring.h"

#include <stddef.h>
#include <stdint.h>

enum {
    GOLDSRC_EFFECT_ATLAS_WIDTH = 64,
    GOLDSRC_EFFECT_ATLAS_HEIGHT = 32,
    GOLDSRC_EFFECT_ATLAS_ROW_PITCH = GOLDSRC_EFFECT_ATLAS_WIDTH * 4,
    GOLDSRC_EFFECT_CONSTANT_DWORDS = 32,
    GOLDSRC_EFFECT_SPRITE_QUADS = 1,
    GOLDSRC_EFFECT_ALPHA_PARTICLE_QUADS = 24,
    GOLDSRC_EFFECT_ADDITIVE_PARTICLE_QUADS = 48,
    GOLDSRC_EFFECT_TOTAL_QUADS =
        GOLDSRC_EFFECT_SPRITE_QUADS +
        GOLDSRC_EFFECT_ALPHA_PARTICLE_QUADS +
        GOLDSRC_EFFECT_ADDITIVE_PARTICLE_QUADS,
    GOLDSRC_EFFECT_HOLD_FRAMES = 600,
};

typedef enum GoldSrcSpriteParticleMode {
    GOLDSRC_EFFECT_MODE_CONTROL = 0,
    GOLDSRC_EFFECT_MODE_SPRITE = 1,
    GOLDSRC_EFFECT_MODE_PARTICLES = 2,
    GOLDSRC_EFFECT_MODE_COMBINED = 3,
    GOLDSRC_EFFECT_MODE_COUNT = 4,
} GoldSrcSpriteParticleMode;

typedef struct GoldSrcSpriteParticleConstants {
    float mvp[16];
    float render_color[4];
    float fog_color_density[4];
    float debug_values[8];
} GoldSrcSpriteParticleConstants;

typedef struct GoldSrcSpriteParticleFrame {
    const uint32_t *constant_table;
    const uint32_t *vertex_table;
    const uint32_t *texture_table;
    const BspBundleVertex *vertices;
    const uint16_t *indices;
    uint32_t vertex_count;
    uint32_t sprite_first_index;
    uint32_t sprite_index_count;
    uint32_t alpha_particle_first_index;
    uint32_t alpha_particle_index_count;
    uint32_t additive_particle_first_index;
    uint32_t additive_particle_index_count;
    uint32_t sprite_quads;
    uint32_t alpha_particles;
    uint32_t additive_particles;
    GoldSrcSpriteParticleMode mode;
    uint64_t atlas_hash;
    uint64_t layout_hash;
    size_t transient_bytes;
} GoldSrcSpriteParticleFrame;

typedef struct GoldSrcSpriteParticleComposeResult {
    uint32_t draws;
    uint32_t indices;
    uint32_t command_dwords;
} GoldSrcSpriteParticleComposeResult;

GoldSrcSpriteParticleMode goldsrc_sprite_particle_mode(uint64_t frame_index);
const char *goldsrc_sprite_particle_mode_name(GoldSrcSpriteParticleMode mode);

int goldsrc_sprite_particle_frame_build(
    GoldSrcSpriteParticleFrame *out, Ps5TransientRing *ring,
    uint32_t slot_index, const void *gpu_mapping, size_t gpu_mapping_bytes,
    const float camera_position[3], const float camera_forward[3],
    float aspect_ratio, uint64_t frame_index);

int goldsrc_sprite_particle_compose_range(
    uint32_t **cursor, uint32_t *end,
    const GoldSrcSpriteParticleFrame *frame, uint32_t first_index,
    uint32_t index_count, const void *gpu_mapping, size_t gpu_mapping_bytes,
    uint64_t modifier, BspSetShDirectFn set_sh_direct,
    BspDrawIndexedFn draw_indexed,
    GoldSrcSpriteParticleComposeResult *result);

#endif
