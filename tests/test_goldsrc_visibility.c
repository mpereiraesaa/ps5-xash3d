#include "../src/goldsrc_visibility.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

int main(void)
{
    const BspBundleVisibilityHeader header = {
        0u, 1u, 1u, 4u, 1u, 3u, 0u, 3u,
    };
    const BspBundleVisibilityPlane planes[1] = {
        {{1.0f, 0.0f, 0.0f}, -1.0f},
    };
    const BspBundleVisibilityNode nodes[1] = {
        {0u, {-2, -1}, 0u},
    };
    const BspBundleVisibilityLeaf leaves[4] = {
        {-2, 0u, {-2, -2, -2}, {2, 2, 2}, 0u, 0u},
        {-1, 1u, {-2, -2, -2}, {2, 2, 2}, 0u, 1u},
        {-1, 2u, {90, -2, -20}, {110, 2, -5}, 1u, 1u},
        {-1, 3u, {-2, -2, -20}, {2, 2, -5}, 2u, 1u},
    };
    const uint32_t refs[3] = {0u, 1u, 2u};
    const uint8_t pvs[4] = {0u, 0x03u, 0x03u, 0x04u};
    const BspBundleDrawBounds bounds[3] = {
        {{-1, -1, -11}, {1, 1, -9}},
        {{99, -1, -11}, {101, 1, -9}},
        {{-1, -1, -13}, {1, 1, -12}},
    };
    const BspBundleDraw draws[3] = {
        {0, 3, 0, 0, 0, 0, {0, 0}},
        {3, 3, 0, 0, 1, 0, {0, 0}},
        {6, 3, 0, 0, 2, 0, {0, 0}},
    };
    const BspBundleTexture textures[1] = {{0}};
    const BspBundleView bundle = {
        .draws = draws, .draw_count = 3u,
        .textures = textures, .texture_count = 1u,
        .visibility = &header, .visibility_planes = planes,
        .visibility_nodes = nodes, .visibility_leaves = leaves,
        .visibility_draw_refs = refs, .visibility_pvs = pvs,
        .draw_bounds = bounds,
    };
    GoldSrcVisibilityPlan plan;
    assert(goldsrc_visibility_plan_build(&plan, &bundle) == 0);
    assert(plan.planes == 1u && plan.leaves == 4u && plan.draw_refs == 3u);

    _Alignas(4096) uint8_t storage[8192];
    Ps5TransientRing ring;
    assert(ps5_transient_ring_init(&ring, storage, sizeof(storage), 2u,
                                   4096u) == PS5_TRANSIENT_OK);
    const float position[3] = {0.0f, 0.0f, 0.0f};
    const float forward[3] = {0.0f, 0.0f, -1.0f};
    const uint32_t expected[4] = {3u, 2u, 2u, 1u};
    for (uint32_t mode = 0u; mode < 4u; ++mode) {
        assert(ps5_transient_ring_begin(&ring, 0u, 0u, 0) ==
               PS5_TRANSIENT_OK);
        GoldSrcVisibilityFrame frame;
        assert(goldsrc_visibility_frame_build(
            &frame, &plan, &bundle, &ring, 0u, position, forward,
            16.0f / 9.0f, mode * GOLDSRC_VISIBILITY_HOLD_FRAMES) == 0);
        assert(frame.mode == (GoldSrcVisibilityMode)mode);
        assert(frame.camera_leaf == 1u && frame.visible_leaves == 2u);
        assert(frame.world_draws == 3u && frame.selected_draws == expected[mode]);
        assert(frame.pvs_culled == 1u && frame.frustum_culled == 1u);
        assert(frame.opaque_draws == expected[mode]);
        assert(frame.mask_hash != 0u && frame.transient_bytes >= 3u);
        assert(ps5_transient_ring_abort_unsubmitted(&ring, 0u) ==
               PS5_TRANSIENT_OK);
    }
    return 0;
}
