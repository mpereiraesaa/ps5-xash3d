#include "ref_agc_lightmap_atlas.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    const RefAgcLightmapRect rects[4] = {
        {4, 2}, {3, 3}, {0, 0}, {5, 1},
    };
    RefAgcLightmapPlacement placements[4];
    uint32_t width = 0u, height = 0u;
    assert(ref_agc_lightmap_atlas_layout(
        rects, 4u, placements, &width, &height) == 0);
    assert(width == REF_AGC_LIGHTMAP_ATLAS_WIDTH && height == 64u);
    assert(placements[0].active && placements[1].active &&
           !placements[2].active && placements[3].active);
    for (unsigned a = 0u; a < 4u; ++a) {
        if (!placements[a].active) continue;
        assert(placements[a].x && placements[a].y);
        for (unsigned b = a + 1u; b < 4u; ++b) {
            if (!placements[b].active) continue;
            assert(placements[a].x + placements[a].width < placements[b].x ||
                   placements[b].x + placements[b].width < placements[a].x ||
                   placements[a].y + placements[a].height < placements[b].y ||
                   placements[b].y + placements[b].height < placements[a].y);
        }
    }

    uint8_t atlas[8 * 8 * 4];
    const uint8_t source[2 * 2 * 4] = {
        1, 2, 3, 255, 4, 5, 6, 255,
        7, 8, 9, 255, 10, 11, 12, 255,
    };
    const RefAgcLightmapPlacement placement = {2, 2, 2, 2, 1};
    memset(atlas, 0, sizeof(atlas));
    assert(ref_agc_lightmap_atlas_blit_rgba8(
        atlas, 8, 8, 32, &placement, source, 8) == 0);
    for (unsigned y = 1u; y <= 4u; ++y) {
        const uint8_t *row = atlas + y * 32u;
        assert(memcmp(row + 4u, row + 8u, 4u) == 0);
        assert(memcmp(row + 16u, row + 12u, 4u) == 0);
    }
    assert(memcmp(atlas + 1u * 32u + 4u,
                  atlas + 2u * 32u + 4u, 16u) == 0);
    assert(memcmp(atlas + 4u * 32u + 4u,
                  atlas + 3u * 32u + 4u, 16u) == 0);

    const RefAgcLightmapRect too_wide = {
        REF_AGC_LIGHTMAP_ATLAS_WIDTH, 1
    };
    RefAgcLightmapPlacement rejected;
    assert(ref_agc_lightmap_atlas_layout(
        &too_wide, 1, &rejected, &width, &height) ==
        REF_AGC_LIGHTMAP_ATLAS_INVALID);
    puts("ref_agc lightmap atlas tests passed");
    return 0;
}
