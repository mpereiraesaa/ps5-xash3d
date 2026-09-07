#ifndef PS5_XASH3D_GOLDSRC_LIGHTMAP_LIGHTING_H
#define PS5_XASH3D_GOLDSRC_LIGHTMAP_LIGHTING_H

#include "bsp_bundle.h"
#include "bsp_dynamic_lightmap.h"

#include <stddef.h>
#include <stdint.h>

enum {
    GOLDSRC_LIGHTING_MODE_BASE = 0,
    GOLDSRC_LIGHTING_MODE_LIGHTSTYLE = 1,
    GOLDSRC_LIGHTING_MODE_DLIGHT = 2,
    GOLDSRC_LIGHTING_MODE_COMBINED = 3,
    GOLDSRC_LIGHTING_MODE_COUNT = 4,
    GOLDSRC_LIGHTING_HOLD_FRAMES = 600,
};

typedef struct GoldSrcLightmapLightingPlan {
    BspDynamicLightmapLayout layout;
    const BspBundleLightmapFace *face;
    const uint8_t *samples;
    uint32_t draw_index;
    uint32_t style_count;
    uint32_t styled_face_count;
    uint32_t styled_layer_count;
    float target_position[3];
    float target_forward[3];
    uint64_t source_hash;
} GoldSrcLightmapLightingPlan;

typedef struct GoldSrcLightmapLightingUpdate {
    uint32_t mode;
    uint32_t style_tick;
    uint32_t animated_style_scale;
    uint32_t dynamic_luxels;
    uint32_t dynamic_center_x;
    uint32_t dynamic_center_y;
    uint64_t style_state_hash;
} GoldSrcLightmapLightingUpdate;

const char *goldsrc_lighting_mode_name(uint32_t mode);
uint32_t goldsrc_lighting_mode(uint64_t frame_index);
uint16_t goldsrc_lightstyle_scale(const char *pattern, uint64_t tick);

/* Select the largest styled, opaque face whose complete atlas rectangle fits
 * the per-frame staging budget, then derive a face-normal proof camera. */
int goldsrc_lightmap_lighting_plan(
    const BspBundleView *bundle, size_t maximum_patch_bytes,
    GoldSrcLightmapLightingPlan *out);

/* Rebuild the selected face from its real BSP style planes, optionally add a
 * face-local radial dynamic light, and commit through the Phase 3 uploader. */
int goldsrc_lightmap_lighting_update(
    const GoldSrcLightmapLightingPlan *plan,
    BspDynamicLightmapSlot *slot, Ps5TransientRing *ring,
    uint32_t slot_index, uint64_t frame_index, uint32_t mode,
    BspDynamicLightmapUpdate *upload,
    GoldSrcLightmapLightingUpdate *lighting);

#endif
