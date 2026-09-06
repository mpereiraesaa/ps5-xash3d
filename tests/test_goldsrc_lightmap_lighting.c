#include "../src/goldsrc_lightmap_lighting.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static uint64_t run_mode(const GoldSrcLightmapLightingPlan *plan,
                         BspDynamicLightmapSlot *slot,
                         Ps5TransientRing *ring, uint64_t frame,
                         uint32_t mode, uint32_t *dynamic_luxels,
                         uint32_t *animated_scale)
{
    const int completion_proven =
        ring->slots[0].state == PS5_TRANSIENT_SEALED;
    assert(ps5_transient_ring_begin(
               ring, 0u, ring->slots[0].retire_token,
               completion_proven) == 0);
    BspDynamicLightmapUpdate upload;
    GoldSrcLightmapLightingUpdate lighting;
    assert(goldsrc_lightmap_lighting_update(
               plan, slot, ring, 0u, frame, mode,
               &upload, &lighting) == 0);
    assert(upload.pattern == mode && upload.patch_hash != 0u);
    assert(lighting.mode == mode && lighting.style_state_hash != 0u);
    *dynamic_luxels = lighting.dynamic_luxels;
    *animated_scale = lighting.animated_style_scale;
    assert(ps5_transient_ring_seal(ring, 0u, frame + 1u) == 0);
    return upload.patch_hash;
}

int main(void)
{
    assert(goldsrc_lightstyle_scale("a", 0u) == 0u);
    assert(goldsrc_lightstyle_scale("m", 0u) == 256u);
    assert(goldsrc_lightstyle_scale("z", 0u) == 533u);
    assert(goldsrc_lightstyle_scale("az", 1u) == 533u);
    assert(goldsrc_lightstyle_scale("?", 0u) == 0u);
    assert(goldsrc_lighting_mode(0u) == GOLDSRC_LIGHTING_MODE_BASE);
    assert(goldsrc_lighting_mode(GOLDSRC_LIGHTING_HOLD_FRAMES) ==
           GOLDSRC_LIGHTING_MODE_LIGHTSTYLE);

    BspBundleVertex vertices[3] = {
        {{-1.0f, -1.0f, 0.0f}, {0, 0}, {0, 0}, 7u},
        {{ 1.0f, -1.0f, 0.0f}, {0, 0}, {0, 0}, 7u},
        {{ 0.0f,  1.0f, 0.0f}, {0, 0}, {0, 0}, 7u},
    };
    const uint16_t indices[3] = {0u, 1u, 2u};
    const BspBundleDraw draw = {
        .first_index = 0u, .index_count = 3u, .base_texture = 0u,
        .lightmap = 0u, .face_id = 7u,
    };
    const BspBundleTexture texture = {
        .width = 1u, .height = 1u, .row_pitch = 256u,
        .format = BSP_BUNDLE_IMAGE_RGBA8_UNORM,
    };
    const BspBundleImage image = {
        .width = 16u, .height = 16u, .row_pitch = 64u,
        .format = BSP_BUNDLE_IMAGE_RGBA8_UNORM,
    };
    const BspBundleLightmapFace face = {
        .face_id = 7u, .atlas_x = 4u, .atlas_y = 4u,
        .width = 8u, .height = 8u, .sample_offset = 0u,
        .sample_bytes = 8u * 8u * 3u * 2u,
        .styles = {0u, 32u, 255u, 255u},
    };
    uint8_t samples[8u * 8u * 3u * 2u];
    memset(samples, 20, 8u * 8u * 3u);
    memset(samples + 8u * 8u * 3u, 40, 8u * 8u * 3u);
    uint8_t atlas[16u * 16u * 4u];
    memset(atlas, 20, sizeof(atlas));
    BspBundleView bundle = {
        .vertices = vertices, .vertex_count = 3u,
        .indices = indices, .index_count = 3u,
        .draws = &draw, .draw_count = 1u,
        .lightmap_image = &image, .lightmap_pixels = atlas,
        .lightmap_pixel_count = 256u,
        .lightmap_faces = &face, .lightmap_face_count = 1u,
        .lightmap_samples = samples, .lightmap_sample_bytes = sizeof(samples),
        .textures = &texture, .texture_count = 1u,
        .camera_position = {0.0f, 0.0f, 10.0f},
    };
    GoldSrcLightmapLightingPlan plan;
    assert(goldsrc_lightmap_lighting_plan(&bundle, 4096u, &plan) == 0);
    assert(plan.layout.hit_face == 7u && plan.layout.patch_x == 4u &&
           plan.layout.patch_y == 4u && plan.layout.patch_width == 8u &&
           plan.layout.patch_height == 8u && plan.style_count == 2u &&
           plan.styled_face_count == 1u && plan.styled_layer_count == 1u);
    assert(plan.target_forward[2] < -0.99f && plan.source_hash != 0u);

    _Alignas(256) uint8_t allocation[16u * 16u * 4u + 512u];
    BspDynamicLightmapSlot slot;
    assert(bsp_dynamic_lightmap_slot_init(
               &slot, allocation, sizeof(allocation), atlas, sizeof(atlas),
               &plan.layout) == 0);
    _Alignas(256) uint8_t transient[8192] = {0};
    Ps5TransientRing ring;
    assert(ps5_transient_ring_init(&ring, transient, sizeof(transient),
                                   2u, 256u) == 0);
    uint32_t luxels = 0u;
    uint32_t scale = 0u;
    const uint64_t base = run_mode(&plan, &slot, &ring, 0u,
                                   GOLDSRC_LIGHTING_MODE_BASE,
                                   &luxels, &scale);
    assert(luxels == 0u && scale == 0u);
    const uint64_t styled = run_mode(&plan, &slot, &ring, 72u,
                                     GOLDSRC_LIGHTING_MODE_LIGHTSTYLE,
                                     &luxels, &scale);
    assert(luxels == 0u && scale != 0u && styled != base);
    const uint64_t dynamic = run_mode(&plan, &slot, &ring, 73u,
                                      GOLDSRC_LIGHTING_MODE_DLIGHT,
                                      &luxels, &scale);
    assert(luxels != 0u && scale == 0u && dynamic != base);
    const uint64_t combined = run_mode(&plan, &slot, &ring, 74u,
                                       GOLDSRC_LIGHTING_MODE_COMBINED,
                                       &luxels, &scale);
    assert(luxels != 0u && scale != 0u && combined != base &&
           combined != styled && combined != dynamic);
    assert(bsp_dynamic_lightmap_guards_intact(&slot, &plan.layout));
    return 0;
}
