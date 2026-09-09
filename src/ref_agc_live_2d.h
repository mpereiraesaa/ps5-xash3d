#ifndef PS5_XASH3D_REF_AGC_LIVE_2D_H
#define PS5_XASH3D_REF_AGC_LIVE_2D_H

#include "goldsrc_2d.h"
#include "goldsrc_render_state.h"
#include "ref_agc_gpu_texture_cache.h"
#include "ref_agc_live_frame.h"

#include <stddef.h>
#include <stdint.h>

enum {
    REF_AGC_LIVE_2D_MAX_QUADS = REF_AGC_LIVE_MAX_2D_COMMANDS,
    REF_AGC_LIVE_2D_MAX_BATCHES = REF_AGC_LIVE_MAX_2D_COMMANDS,
};

typedef struct RefAgcLive2DBatch {
    const uint32_t *texture_table;
    uint32_t first_index;
    uint32_t index_count;
    uint32_t texture_handle;
    GoldSrcBlendMode blend;
    uint32_t fill;
} RefAgcLive2DBatch;

typedef struct RefAgcLive2DFrame {
    const uint32_t *constant_table;
    const uint32_t *vertex_table;
    const GoldSrc2DVertex *vertices;
    const uint16_t *indices;
    RefAgcLive2DBatch batches[REF_AGC_LIVE_2D_MAX_BATCHES];
    uint32_t vertex_count;
    uint32_t index_count;
    uint32_t batch_count;
    uint32_t mode_commands;
    uint32_t stretch_quads;
    uint32_t fill_quads;
    uint32_t alpha_batches;
    uint32_t additive_batches;
    uint32_t opaque_batches;
    uint32_t unresolved_textures;
    uint32_t failed_command;
    int32_t failed_texture;
    uint32_t failed_type;
    uint64_t command_hash;
    uint64_t layout_hash;
    size_t transient_bytes;
} RefAgcLive2DFrame;

typedef struct RefAgcLive2DComposeResult {
    uint32_t draws;
    uint32_t indices;
    uint32_t command_dwords;
    uint32_t texture_binds;
} RefAgcLive2DComposeResult;

enum RefAgcLive2DResult {
    REF_AGC_LIVE_2D_OK = 0,
    REF_AGC_LIVE_2D_INVALID = -1,
    REF_AGC_LIVE_2D_TRANSIENT_EXHAUSTED = -2,
    REF_AGC_LIVE_2D_DESCRIPTOR_FAILED = -3,
    REF_AGC_LIVE_2D_SEQUENCE_INVALID = -4,
    REF_AGC_LIVE_2D_TEXTURE_UNRESOLVED = -5,
};

int ref_agc_live_2d_frame_build(
    RefAgcLive2DFrame *out, Ps5TransientRing *ring, uint32_t slot_index,
    const void *gpu_mapping, size_t gpu_mapping_bytes,
    uint32_t framebuffer_width, uint32_t framebuffer_height,
    const RefAgcLiveFrame *live,
    const RefAgcGpuTextureCache *textures);

int ref_agc_live_2d_compose_batch(
    uint32_t **cursor, uint32_t *end, const RefAgcLive2DFrame *frame,
    uint32_t batch_index, const void *gpu_mapping,
    size_t gpu_mapping_bytes, uint64_t modifier,
    BspSetShDirectFn set_sh_direct, BspDrawIndexedFn draw_indexed,
    RefAgcLive2DComposeResult *result);

#endif
