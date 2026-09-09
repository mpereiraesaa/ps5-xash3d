#include "ref_agc_live_brush.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "bsp_flat_scene.h"

static const BspResourceConstants *constants(const RefAgcLiveBrushEntry *entry)
{
    const uint32_t *words = entry->constant_table;
    return (const BspResourceConstants *)(uintptr_t)(
        (uint64_t)words[0] | ((uint64_t)(words[1] & 0xffffu) << 32));
}

static void expect_point(const float matrix[16], const float camera[16],
                         const float local[3], const float world[3])
{
    for (unsigned row = 0; row < 4; ++row) {
        float actual = matrix[12 + row], expected = camera[12 + row];
        for (unsigned column = 0; column < 3; ++column) {
            actual += matrix[column * 4 + row] * local[column];
            expected += camera[column * 4 + row] * world[column];
        }
        assert(fabsf(actual - expected) < 0.0001f);
    }
}

int main(void)
{
    uint8_t *memory = aligned_alloc(256u, 16384u);
    assert(memory);
    Ps5TransientRing ring;
    assert(ps5_transient_ring_init(&ring, memory, 16384u, 1u, 256u) ==
           PS5_TRANSIENT_OK);
    assert(ps5_transient_ring_begin(&ring, 0u, 0u, 0) == PS5_TRANSIENT_OK);
    RefAgcLiveFrame live = {0};
    live.world.surfaces = 100u;
    live.entity_count = 4u;
    live.entities[0] = (RefAgcLiveEntity){
        .model_type = REF_AGC_LIVE_MODEL_BRUSH,
        .first_surface = 30u, .surface_count = 4u,
        .render_mode = REF_AGC_LIVE_RENDER_NORMAL,
        .render_color = {255u, 255u, 255u, 255u},
        .origin = {10.0f, 20.0f, 30.0f},
    };
    live.entities[1] = (RefAgcLiveEntity){
        .model_type = REF_AGC_LIVE_MODEL_BRUSH,
        .first_surface = 40u, .surface_count = 5u,
        .render_mode = REF_AGC_LIVE_RENDER_TRANS_TEXTURE,
        .render_amount = 128,
        .render_color = {64u, 128u, 255u, 128u},
        .angles = {0.0f, 90.0f, 0.0f},
    };
    live.entities[2] = (RefAgcLiveEntity){
        .model_type = REF_AGC_LIVE_MODEL_BRUSH,
        .first_surface = 60u, .surface_count = 2u,
        .render_mode = REF_AGC_LIVE_RENDER_TRANS_ADD,
        .render_amount = 192,
    };
    live.entities[3] = (RefAgcLiveEntity){
        .model_type = 3, .first_surface = 99u, .surface_count = 1u,
    };
    RefAgcLiveBrushFrame frame;
    const float camera[3] = {0.0f, 0.0f, 0.0f};
    const float forward[3] = {0.0f, 0.0f, -1.0f};
    assert(ref_agc_live_brush_frame_build(
        &frame, &live, &ring, 0u, memory, 16384u,
        camera, forward, 16.0f / 9.0f) == 0);
    assert(frame.count == 3u && frame.opaque_count == 1u &&
           frame.alpha_count == 1u && frame.additive_count == 1u &&
           frame.rejected == 0u && frame.transient_bytes > 0u &&
           frame.transform_hash != 0u);
    assert(frame.entries[0].first_surface == 30u &&
           frame.entries[1].render_class == REF_AGC_LIVE_BRUSH_ALPHA &&
           frame.entries[2].render_class == REF_AGC_LIVE_BRUSH_ADDITIVE);
    for (uint32_t i = 0u; i < frame.count; ++i)
        assert(frame.entries[i].constant_table != NULL);
    float camera_matrix[16];
    assert(bsp_flat_camera_matrix(camera_matrix, camera, forward, 16.0f / 9.0f) == 0);
    /* GoldSrc (10,20,30) maps to AGC (10,30,-20). */
    expect_point(constants(&frame.entries[0])->mvp, camera_matrix,
                 (float[3]){0,0,0}, (float[3]){10,30,-20});
    /* A 90-degree GoldSrc yaw rotates +X onto +Y, hence AGC -Z. */
    expect_point(constants(&frame.entries[1])->mvp, camera_matrix,
                 (float[3]){1,0,0}, (float[3]){0,0,-1});
    assert(fabsf(constants(&frame.entries[1])->control[3] - 128.0f/255.0f) < 0.0001f);

    Ps5TransientRing second_ring;
    assert(ps5_transient_ring_init(&second_ring, memory, 16384u, 1u, 256u) ==
           PS5_TRANSIENT_OK);
    assert(ps5_transient_ring_begin(&second_ring, 0u, 0u, 0) == PS5_TRANSIENT_OK);
    live.entities[0].first_surface = 100u;
    assert(ref_agc_live_brush_frame_build(
        &frame, &live, &second_ring, 0u, memory, 16384u,
        camera, forward, 16.0f / 9.0f) == 0);
    assert(frame.count == 2u && frame.rejected == 1u);
    free(memory);
    puts("ref_agc live brush tests passed");
    return 0;
}
