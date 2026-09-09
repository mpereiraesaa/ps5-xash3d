#include "ref_agc_lightmap_atlas.h"

#include <limits.h>
#include <string.h>

static uint32_t next_power_of_two(uint32_t value)
{
    uint32_t result = 1u;
    while (result < value && result <= UINT32_MAX / 2u)
        result <<= 1u;
    return result;
}

int ref_agc_lightmap_atlas_layout(
    const RefAgcLightmapRect *rects, uint32_t count,
    RefAgcLightmapPlacement *placements, uint32_t *out_width,
    uint32_t *out_height)
{
    uint16_t skyline[REF_AGC_LIGHTMAP_ATLAS_WIDTH] = {0};
    uint32_t used_height = 0u;
    if (!rects || !count || !placements || !out_width || !out_height)
        return REF_AGC_LIGHTMAP_ATLAS_INVALID;
    memset(placements, 0, (size_t)count * sizeof(*placements));
    for (uint32_t rect = 0u; rect < count; ++rect) {
        const uint32_t width = rects[rect].width;
        const uint32_t height = rects[rect].height;
        if (!width && !height)
            continue;
        if (!width || !height ||
            width > REF_AGC_LIGHTMAP_ATLAS_WIDTH - 2u ||
            height > REF_AGC_LIGHTMAP_ATLAS_MAX_HEIGHT - 2u)
            return REF_AGC_LIGHTMAP_ATLAS_INVALID;
        const uint32_t packed_width = width + 2u;
        const uint32_t packed_height = height + 2u;
        uint32_t best_x = 0u;
        uint32_t best_y = UINT32_MAX;
        for (uint32_t x = 0u;
             x + packed_width <= REF_AGC_LIGHTMAP_ATLAS_WIDTH; ++x) {
            uint32_t y = 0u;
            for (uint32_t column = 0u; column < packed_width; ++column)
                if (skyline[x + column] > y)
                    y = skyline[x + column];
            if (y < best_y) {
                best_x = x;
                best_y = y;
            }
        }
        if (best_y == UINT32_MAX ||
            best_y + packed_height > REF_AGC_LIGHTMAP_ATLAS_MAX_HEIGHT)
            return REF_AGC_LIGHTMAP_ATLAS_EXHAUSTED;
        for (uint32_t column = 0u; column < packed_width; ++column)
            skyline[best_x + column] =
                (uint16_t)(best_y + packed_height);
        placements[rect] = (RefAgcLightmapPlacement){
            best_x + 1u, best_y + 1u, width, height, 1
        };
        if (best_y + packed_height > used_height)
            used_height = best_y + packed_height;
    }
    if (!used_height)
        return REF_AGC_LIGHTMAP_ATLAS_INVALID;
    *out_width = REF_AGC_LIGHTMAP_ATLAS_WIDTH;
    *out_height = next_power_of_two(used_height < 64u ? 64u : used_height);
    if (*out_height > REF_AGC_LIGHTMAP_ATLAS_MAX_HEIGHT)
        return REF_AGC_LIGHTMAP_ATLAS_EXHAUSTED;
    return REF_AGC_LIGHTMAP_ATLAS_OK;
}

int ref_agc_lightmap_atlas_blit_rgba8(
    uint8_t *atlas, uint32_t atlas_width, uint32_t atlas_height,
    uint32_t atlas_row_pitch, const RefAgcLightmapPlacement *placement,
    const uint8_t *source, uint32_t source_row_pitch)
{
    if (!atlas || !placement || !placement->active || !source ||
        !atlas_width || !atlas_height ||
        atlas_width > UINT32_MAX / 4u ||
        atlas_row_pitch < atlas_width * 4u ||
        placement->x == 0u || placement->y == 0u ||
        placement->x + placement->width >= atlas_width ||
        placement->y + placement->height >= atlas_height ||
        placement->width > UINT32_MAX / 4u ||
        source_row_pitch < placement->width * 4u)
        return REF_AGC_LIGHTMAP_ATLAS_INVALID;
    const size_t row_bytes = (size_t)placement->width * 4u;
    for (uint32_t row = 0u; row < placement->height; ++row) {
        uint8_t *destination = atlas +
            (size_t)(placement->y + row) * atlas_row_pitch +
            (size_t)placement->x * 4u;
        memcpy(destination, source + (size_t)row * source_row_pitch,
               row_bytes);
        memcpy(destination - 4u, destination, 4u);
        memcpy(destination + row_bytes, destination + row_bytes - 4u, 4u);
    }
    uint8_t *first = atlas + (size_t)placement->y * atlas_row_pitch +
                     (size_t)(placement->x - 1u) * 4u;
    uint8_t *last = atlas +
        (size_t)(placement->y + placement->height - 1u) * atlas_row_pitch +
        (size_t)(placement->x - 1u) * 4u;
    memcpy(first - atlas_row_pitch, first, row_bytes + 8u);
    memcpy(last + atlas_row_pitch, last, row_bytes + 8u);
    return REF_AGC_LIGHTMAP_ATLAS_OK;
}
