#ifndef PS5_XASH3D_GOLDSRC_2D_H
#define PS5_XASH3D_GOLDSRC_2D_H

#include "bsp_flat_draw.h"
#include "ps5_transient_ring.h"

#include <stddef.h>
#include <stdint.h>

enum {
    GOLDSRC_2D_ATLAS_WIDTH = 128,
    GOLDSRC_2D_ATLAS_HEIGHT = 32,
    GOLDSRC_2D_ATLAS_ROW_PITCH = GOLDSRC_2D_ATLAS_WIDTH * 4,
    GOLDSRC_2D_CONSTANT_DWORDS = 32,
    GOLDSRC_2D_MAX_QUADS = 128,
};

typedef struct GoldSrc2DVertex {
    float position[2];
    float uv[2];
    float color[4];
} GoldSrc2DVertex;

typedef struct GoldSrc2DConstants {
    float projection[16];
    float color_scale[4];
    float debug_values[12];
} GoldSrc2DConstants;

typedef struct GoldSrc2DFrame {
    const uint32_t *constant_table;
    const uint32_t *vertex_table;
    const uint32_t *texture_table;
    const GoldSrc2DVertex *vertices;
    const uint16_t *indices;
    uint32_t vertex_count;
    uint32_t alpha_first_index;
    uint32_t alpha_index_count;
    uint32_t additive_first_index;
    uint32_t additive_index_count;
    uint32_t hud_quads;
    uint32_t console_quads;
    uint32_t menu_quads;
    uint32_t font_quads;
    uint64_t atlas_hash;
    uint64_t layout_hash;
    size_t transient_bytes;
} GoldSrc2DFrame;

typedef struct GoldSrc2DComposeResult {
    uint32_t draws;
    uint32_t indices;
    uint32_t command_dwords;
} GoldSrc2DComposeResult;

int goldsrc_2d_frame_build(
    GoldSrc2DFrame *out, Ps5TransientRing *ring, uint32_t slot_index,
    const void *gpu_mapping, size_t gpu_mapping_bytes,
    uint32_t framebuffer_width, uint32_t framebuffer_height,
    uint64_t frame_index);

int goldsrc_2d_compose_range(
    uint32_t **cursor, uint32_t *end, const GoldSrc2DFrame *frame,
    uint32_t first_index, uint32_t index_count,
    const void *gpu_mapping, size_t gpu_mapping_bytes, uint64_t modifier,
    BspSetShDirectFn set_sh_direct, BspDrawIndexedFn draw_indexed,
    GoldSrc2DComposeResult *result);

#endif
