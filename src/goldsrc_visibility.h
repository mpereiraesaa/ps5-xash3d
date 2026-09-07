#ifndef PS5_XASH3D_GOLDSRC_VISIBILITY_H
#define PS5_XASH3D_GOLDSRC_VISIBILITY_H

#include "bsp_bundle.h"
#include "ps5_transient_ring.h"

#include <stddef.h>
#include <stdint.h>

enum { GOLDSRC_VISIBILITY_HOLD_FRAMES = 600 };

typedef enum GoldSrcVisibilityMode {
    GOLDSRC_VISIBILITY_MODE_CONTROL = 0,
    GOLDSRC_VISIBILITY_MODE_PVS = 1,
    GOLDSRC_VISIBILITY_MODE_FRUSTUM = 2,
    GOLDSRC_VISIBILITY_MODE_COMBINED = 3,
    GOLDSRC_VISIBILITY_MODE_COUNT = 4,
} GoldSrcVisibilityMode;

typedef struct GoldSrcVisibilityPlan {
    uint32_t planes;
    uint32_t nodes;
    uint32_t leaves;
    uint32_t pvs_row_bytes;
    uint32_t draw_refs;
    uint32_t world_first_face;
    uint32_t world_face_count;
} GoldSrcVisibilityPlan;

typedef struct GoldSrcVisibilityFrame {
    const uint8_t *draw_mask;
    uint32_t draw_mask_bytes;
    GoldSrcVisibilityMode mode;
    uint32_t camera_leaf;
    uint32_t visible_leaves;
    uint32_t world_draws;
    uint32_t selected_draws;
    uint32_t opaque_draws;
    uint32_t alpha_draws;
    uint32_t sky_draws;
    uint32_t pvs_culled;
    uint32_t frustum_culled;
    uint64_t mask_hash;
    size_t transient_bytes;
} GoldSrcVisibilityFrame;

GoldSrcVisibilityMode goldsrc_visibility_mode(uint64_t frame_index);
const char *goldsrc_visibility_mode_name(GoldSrcVisibilityMode mode);

int goldsrc_visibility_plan_build(GoldSrcVisibilityPlan *out,
                                  const BspBundleView *bundle);

int goldsrc_visibility_frame_build(
    GoldSrcVisibilityFrame *out, const GoldSrcVisibilityPlan *plan,
    const BspBundleView *bundle, Ps5TransientRing *ring,
    uint32_t slot_index, const float camera_position[3],
    const float camera_forward[3], float aspect_ratio,
    uint64_t frame_index);

#endif
