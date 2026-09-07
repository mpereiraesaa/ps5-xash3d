#ifndef PS5_XASH3D_GOLDSRC_BRUSH_ENTITIES_H
#define PS5_XASH3D_GOLDSRC_BRUSH_ENTITIES_H

#include "bsp_bundle.h"
#include "bsp_resource_draw.h"
#include "bsp_resource_frame.h"
#include "ps5_transient_ring.h"

#include <stddef.h>
#include <stdint.h>

enum {
    GOLDSRC_BRUSH_INSTANCE_COUNT = 3,
    GOLDSRC_BRUSH_PHASE4_SCENE_INSTANCE_COUNT = 5,
    GOLDSRC_BRUSH_HOLD_FRAMES = 600,
};

typedef enum GoldSrcBrushMode {
    GOLDSRC_BRUSH_MODE_CONTROL = 0,
    GOLDSRC_BRUSH_MODE_OPAQUE = 1,
    GOLDSRC_BRUSH_MODE_ALPHA = 2,
    GOLDSRC_BRUSH_MODE_ADDITIVE = 3,
    GOLDSRC_BRUSH_MODE_COMBINED = 4,
    GOLDSRC_BRUSH_MODE_COUNT = 5,
} GoldSrcBrushMode;

typedef enum GoldSrcBrushInstance {
    GOLDSRC_BRUSH_INSTANCE_OPAQUE = 0,
    GOLDSRC_BRUSH_INSTANCE_ALPHA = 1,
    GOLDSRC_BRUSH_INSTANCE_ADDITIVE = 2,
    GOLDSRC_BRUSH_INSTANCE_WATER = 3,
    GOLDSRC_BRUSH_INSTANCE_GLASS = 4,
} GoldSrcBrushInstance;

typedef struct GoldSrcBrushPlan {
    uint32_t entity_indices[GOLDSRC_BRUSH_PHASE4_SCENE_INSTANCE_COUNT];
    uint32_t draw_counts[GOLDSRC_BRUSH_PHASE4_SCENE_INSTANCE_COUNT];
    uint32_t index_counts[GOLDSRC_BRUSH_PHASE4_SCENE_INSTANCE_COUNT];
    uint32_t source_render_modes[GOLDSRC_BRUSH_PHASE4_SCENE_INSTANCE_COUNT];
    uint32_t classname_hashes[GOLDSRC_BRUSH_PHASE4_SCENE_INSTANCE_COUNT];
    uint32_t instance_count;
} GoldSrcBrushPlan;

typedef struct GoldSrcBrushFrame {
    const uint32_t
        *constant_tables[GOLDSRC_BRUSH_PHASE4_SCENE_INSTANCE_COUNT];
    GoldSrcBrushMode mode;
    uint64_t transform_hash;
    size_t transient_bytes;
} GoldSrcBrushFrame;

typedef struct GoldSrcBrushComposeResult {
    uint32_t instances;
    uint32_t draws;
    uint32_t indices;
    uint32_t texture_binds;
    uint32_t command_dwords;
} GoldSrcBrushComposeResult;

GoldSrcBrushMode goldsrc_brush_mode(uint64_t frame_index);
const char *goldsrc_brush_mode_name(GoldSrcBrushMode mode);

int goldsrc_brush_plan_build(GoldSrcBrushPlan *out,
                             const BspBundleView *bundle);

int goldsrc_brush_phase4_scene_plan_extend(GoldSrcBrushPlan *plan,
                                           const BspBundleView *bundle);

int goldsrc_brush_frame_build(
    GoldSrcBrushFrame *out, const GoldSrcBrushPlan *plan,
    const BspBundleView *bundle, Ps5TransientRing *ring,
    uint32_t slot_index, const void *gpu_mapping, size_t gpu_mapping_bytes,
    const float camera_position[3], const float camera_forward[3],
    float aspect_ratio, uint64_t frame_index);

int goldsrc_brush_compose_instance(
    uint32_t **cursor, uint32_t *end, const GoldSrcBrushFrame *frame,
    const GoldSrcBrushPlan *plan, const BspResourceFrame *resource_frame,
    const BspBundleView *bundle, GoldSrcBrushInstance instance,
    const void *gpu_mapping, size_t gpu_mapping_bytes, uint64_t modifier,
    BspSetShDirectFn set_sh_direct, BspDrawIndexedFn draw_indexed,
    GoldSrcBrushComposeResult *result);

#endif
