#ifndef PS5_XASH3D_GOLDSRC_STUDIO_MODEL_H
#define PS5_XASH3D_GOLDSRC_STUDIO_MODEL_H

#include "bsp_bundle.h"
#include "bsp_resource_draw.h"
#include "goldsrc_studio_bundle.h"
#include "ps5_transient_ring.h"

#include <stddef.h>
#include <stdint.h>

enum {
    GOLDSRC_STUDIO_INSTANCE_COUNT = 3,
    GOLDSRC_STUDIO_CONSTANT_DWORDS = 32,
    GOLDSRC_STUDIO_CONSTANT_STRIDE = 256,
    GOLDSRC_STUDIO_HOLD_FRAMES = 600,
};

typedef enum GoldSrcStudioMode {
    GOLDSRC_STUDIO_MODE_CONTROL = 0,
    GOLDSRC_STUDIO_MODE_TEXTURED = 1,
    GOLDSRC_STUDIO_MODE_CHROME = 2,
    GOLDSRC_STUDIO_MODE_ADDITIVE = 3,
    GOLDSRC_STUDIO_MODE_COMBINED = 4,
    GOLDSRC_STUDIO_MODE_COUNT = 5,
} GoldSrcStudioMode;

typedef enum GoldSrcStudioInstance {
    GOLDSRC_STUDIO_INSTANCE_TEXTURED = 0,
    GOLDSRC_STUDIO_INSTANCE_CHROME = 1,
    GOLDSRC_STUDIO_INSTANCE_ADDITIVE = 2,
} GoldSrcStudioInstance;

typedef struct GoldSrcStudioConstants {
    float mvp[16];
    float render_color[4];
    float fog_color_density[4];
    float debug_values[8];
} GoldSrcStudioConstants;

typedef struct GoldSrcStudioFrame {
    const uint32_t *constant_tables[GOLDSRC_STUDIO_INSTANCE_COUNT];
    const uint32_t *vertex_table;
    const uint32_t *texture_tables;
    const BspBundleVertex *vertices;
    const uint16_t *indices;
    uint32_t vertices_per_instance;
    uint32_t indices_per_instance;
    uint32_t draw_count;
    uint32_t texture_count;
    uint32_t frame0;
    uint32_t frame1;
    float blend;
    GoldSrcStudioMode mode;
    uint64_t pose_hash;
    uint64_t skinned_hash;
    size_t transient_bytes;
} GoldSrcStudioFrame;

typedef struct GoldSrcStudioComposeResult {
    uint32_t draws;
    uint32_t indices;
    uint32_t textures;
    uint32_t command_dwords;
} GoldSrcStudioComposeResult;

GoldSrcStudioMode goldsrc_studio_mode(uint64_t frame_index);
const char *goldsrc_studio_mode_name(GoldSrcStudioMode mode);

int goldsrc_studio_frame_build(
    GoldSrcStudioFrame *out, const GoldSrcStudioBundleView *bundle,
    Ps5TransientRing *ring, uint32_t slot_index,
    const void *gpu_mapping, size_t gpu_mapping_bytes,
    const float camera_position[3], const float camera_forward[3],
    float aspect_ratio, uint64_t frame_index);

int goldsrc_studio_compose_instance(
    uint32_t **cursor, uint32_t *end, const GoldSrcStudioFrame *frame,
    const GoldSrcStudioBundleView *bundle, GoldSrcStudioInstance instance,
    const void *gpu_mapping, size_t gpu_mapping_bytes, uint64_t modifier,
    BspSetShDirectFn set_sh_direct, BspDrawIndexedFn draw_indexed,
    GoldSrcStudioComposeResult *result);

#endif
