#ifndef PS5_XASH3D_REF_AGC_LIVE_BRUSH_H
#define PS5_XASH3D_REF_AGC_LIVE_BRUSH_H

#include "bsp_resource_frame.h"
#include "ref_agc_gpu_world_draw.h"
#include "ref_agc_live_frame.h"

enum {
    REF_AGC_LIVE_MAX_BRUSH_ENTITIES = 512,
    REF_AGC_LIVE_RENDER_NORMAL = 0,
    REF_AGC_LIVE_RENDER_TRANS_COLOR = 1,
    REF_AGC_LIVE_RENDER_TRANS_TEXTURE = 2,
    REF_AGC_LIVE_RENDER_GLOW = 3,
    REF_AGC_LIVE_RENDER_TRANS_ALPHA = 4,
    REF_AGC_LIVE_RENDER_TRANS_ADD = 5,
};

typedef enum RefAgcLiveBrushClass {
    REF_AGC_LIVE_BRUSH_OPAQUE = 0,
    REF_AGC_LIVE_BRUSH_ALPHA = 1,
    REF_AGC_LIVE_BRUSH_ADDITIVE = 2,
} RefAgcLiveBrushClass;

typedef struct RefAgcLiveBrushEntry {
    const uint32_t *constant_table;
    uint32_t live_entity;
    uint32_t first_surface;
    uint32_t surface_count;
    uint32_t render_mode;
    RefAgcLiveBrushClass render_class;
} RefAgcLiveBrushEntry;

typedef struct RefAgcLiveBrushFrame {
    RefAgcLiveBrushEntry entries[REF_AGC_LIVE_MAX_BRUSH_ENTITIES];
    uint32_t count;
    uint32_t opaque_count;
    uint32_t alpha_count;
    uint32_t additive_count;
    uint32_t rejected;
    uint64_t transform_hash;
    size_t transient_bytes;
} RefAgcLiveBrushFrame;

int ref_agc_live_brush_frame_build(
    RefAgcLiveBrushFrame *out, const RefAgcLiveFrame *live,
    Ps5TransientRing *ring, uint32_t slot_index,
    const void *gpu_mapping, size_t gpu_mapping_bytes,
    const float camera_position[3], const float camera_forward[3],
    float aspect_ratio);

int ref_agc_live_brush_compose_entry(
    uint32_t **cursor, uint32_t *end, const RefAgcLiveBrushFrame *frame,
    uint32_t entry_index, const RefAgcGpuWorldCache *cache,
    uint32_t flag_mask, uint32_t flag_value,
    const void *gpu_mapping, size_t gpu_mapping_bytes, uint64_t modifier,
    BspSetShDirectFn set_sh_direct, BspDrawIndexedFn draw_indexed,
    RefAgcGpuWorldComposeResult *out);

#endif
