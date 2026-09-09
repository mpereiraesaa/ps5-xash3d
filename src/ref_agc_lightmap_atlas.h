#ifndef PS5_XASH3D_REF_AGC_LIGHTMAP_ATLAS_H
#define PS5_XASH3D_REF_AGC_LIGHTMAP_ATLAS_H

#include <stddef.h>
#include <stdint.h>

enum {
    REF_AGC_LIGHTMAP_ATLAS_WIDTH = 1024,
    REF_AGC_LIGHTMAP_ATLAS_MAX_HEIGHT = 4096,
    REF_AGC_LIGHTMAP_ATLAS_GUTTER = 1,
};

typedef struct RefAgcLightmapRect {
    uint32_t width;
    uint32_t height;
} RefAgcLightmapRect;

typedef struct RefAgcLightmapPlacement {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    int active;
} RefAgcLightmapPlacement;

enum RefAgcLightmapAtlasResult {
    REF_AGC_LIGHTMAP_ATLAS_OK = 0,
    REF_AGC_LIGHTMAP_ATLAS_INVALID = -1,
    REF_AGC_LIGHTMAP_ATLAS_EXHAUSTED = -2,
};

int ref_agc_lightmap_atlas_layout(
    const RefAgcLightmapRect *rects, uint32_t count,
    RefAgcLightmapPlacement *placements, uint32_t *out_width,
    uint32_t *out_height);

int ref_agc_lightmap_atlas_blit_rgba8(
    uint8_t *atlas, uint32_t atlas_width, uint32_t atlas_height,
    uint32_t atlas_row_pitch, const RefAgcLightmapPlacement *placement,
    const uint8_t *source, uint32_t source_row_pitch);

#endif
