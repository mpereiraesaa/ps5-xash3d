#include "bsp_bundle.h"
#include "bsp_dynamic_lightmap.h"
#include "bsp_alpha_test.h"
#include "bsp_sky.h"
#include "bsp_texture_descriptor.h"
#include "goldsrc_lightmap_lighting.h"
#include "goldsrc_brush_entities.h"
#include "goldsrc_visibility.h"

#include <stdio.h>
#include <stdlib.h>

static int verify_lighting_modes(const BspBundleView *view,
                                 const GoldSrcLightmapLightingPlan *plan)
{
    size_t allocation_bytes = 0u;
    if (bsp_dynamic_lightmap_allocation_bytes(
            &plan->layout, &allocation_bytes) != 0 ||
        allocation_bytes % 256u != 0u)
        return -1;
    void *const allocation = aligned_alloc(256u, allocation_bytes);
    void *const transient = aligned_alloc(256u, 262144u);
    if (!allocation || !transient) {
        free(allocation);
        free(transient);
        return -1;
    }
    BspDynamicLightmapSlot lightmap_slot;
    Ps5TransientRing ring;
    if (bsp_dynamic_lightmap_slot_init(
            &lightmap_slot, allocation, allocation_bytes,
            view->lightmap_pixels, plan->layout.image_bytes,
            &plan->layout) != 0 ||
        ps5_transient_ring_init(&ring, transient, 262144u, 2u, 256u) != 0) {
        free(allocation);
        free(transient);
        return -1;
    }
    uint64_t hashes[GOLDSRC_LIGHTING_MODE_COUNT] = {0u};
    uint64_t completed_token = 0u;
    for (uint32_t mode = 0u; mode < GOLDSRC_LIGHTING_MODE_COUNT; ++mode) {
        if (ps5_transient_ring_begin(
                &ring, 0u, completed_token, completed_token != 0u) != 0) {
            free(allocation);
            free(transient);
            return -1;
        }
        BspDynamicLightmapUpdate upload;
        GoldSrcLightmapLightingUpdate lighting;
        if (goldsrc_lightmap_lighting_update(
                plan, &lightmap_slot, &ring, 0u,
                (uint64_t)mode * GOLDSRC_LIGHTING_HOLD_FRAMES,
                mode, &upload, &lighting) != 0 ||
            upload.pattern != mode || upload.patch_hash == 0u ||
            lighting.mode != mode ||
            (mode == GOLDSRC_LIGHTING_MODE_BASE &&
             (lighting.animated_style_scale != 0u ||
              lighting.dynamic_luxels != 0u)) ||
            (mode == GOLDSRC_LIGHTING_MODE_LIGHTSTYLE &&
             (lighting.animated_style_scale == 0u ||
              lighting.dynamic_luxels != 0u)) ||
            (mode == GOLDSRC_LIGHTING_MODE_DLIGHT &&
             (lighting.animated_style_scale != 0u ||
              lighting.dynamic_luxels == 0u)) ||
            (mode == GOLDSRC_LIGHTING_MODE_COMBINED &&
             (lighting.animated_style_scale == 0u ||
              lighting.dynamic_luxels == 0u))) {
            free(allocation);
            free(transient);
            return -1;
        }
        hashes[mode] = upload.patch_hash;
        completed_token = mode + 1u;
        if (ps5_transient_ring_seal(&ring, 0u, completed_token) != 0) {
            free(allocation);
            free(transient);
            return -1;
        }
    }
    const int distinct = hashes[0] != hashes[1] &&
                         hashes[0] != hashes[2] &&
                         hashes[0] != hashes[3] &&
                         hashes[1] != hashes[2] &&
                         hashes[1] != hashes[3] &&
                         hashes[2] != hashes[3];
    if (distinct)
        printf(" lighting_hashes=%016llx,%016llx,%016llx,%016llx",
               (unsigned long long)hashes[0],
               (unsigned long long)hashes[1],
               (unsigned long long)hashes[2],
               (unsigned long long)hashes[3]);
    free(allocation);
    free(transient);
    return distinct ? 0 : -1;
}

static int verify_visibility_modes(const BspBundleView *view)
{
    GoldSrcVisibilityPlan plan;
    if (goldsrc_visibility_plan_build(&plan, view) != 0)
        return -1;
    const size_t slot_bytes = 4096u;
    void *const transient = aligned_alloc(4096u, slot_bytes * 2u);
    if (!transient)
        return -1;
    Ps5TransientRing ring;
    if (ps5_transient_ring_init(
            &ring, transient, slot_bytes * 2u, 2u, 4096u) != 0) {
        free(transient);
        return -1;
    }
    GoldSrcVisibilityFrame frames[GOLDSRC_VISIBILITY_MODE_COUNT];
    for (uint32_t mode = 0u; mode < GOLDSRC_VISIBILITY_MODE_COUNT; ++mode) {
        if (ps5_transient_ring_begin(&ring, 0u, 0u, 0) != 0 ||
            goldsrc_visibility_frame_build(
                &frames[mode], &plan, view, &ring, 0u,
                view->camera_position, view->camera_forward, 16.0f / 9.0f,
                (uint64_t)mode * GOLDSRC_VISIBILITY_HOLD_FRAMES) != 0 ||
            ps5_transient_ring_abort_unsubmitted(&ring, 0u) != 0) {
            free(transient);
            return -1;
        }
    }
    const uint32_t control = frames[GOLDSRC_VISIBILITY_MODE_CONTROL].selected_draws;
    const uint32_t pvs = frames[GOLDSRC_VISIBILITY_MODE_PVS].selected_draws;
    const uint32_t frustum = frames[GOLDSRC_VISIBILITY_MODE_FRUSTUM].selected_draws;
    const uint32_t combined = frames[GOLDSRC_VISIBILITY_MODE_COMBINED].selected_draws;
    if (pvs >= control || frustum >= control || combined > pvs ||
        combined > frustum || combined == 0u) {
        free(transient);
        return -1;
    }
    printf(" visibility_planes=%u visibility_nodes=%u visibility_leaves=%u "
           "pvs_row_bytes=%u visibility_draw_refs=%u world_faces=%u "
           "camera_leaf=%u visible_leaves=%u visibility_draws=%u,%u,%u,%u "
           "pvs_culled=%u frustum_culled=%u",
           plan.planes, plan.nodes, plan.leaves, plan.pvs_row_bytes,
           plan.draw_refs, plan.world_face_count, frames[0].camera_leaf,
           frames[0].visible_leaves, control, pvs, frustum, combined,
           frames[0].pvs_culled, frames[0].frustum_culled);
    free(transient);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: inspect_bsp_bundle BUNDLE\n");
        return 2;
    }
    FILE *file = fopen(argv[1], "rb");
    if (!file || fseek(file, 0, SEEK_END) != 0) {
        fprintf(stderr, "bundle open failed\n");
        if (file) fclose(file);
        return 1;
    }
    const long length = ftell(file);
    if (length <= 0 || fseek(file, 0, SEEK_SET) != 0) {
        fprintf(stderr, "bundle size failed\n");
        fclose(file);
        return 1;
    }
    void *data = malloc((size_t)length);
    if (!data || fread(data, 1u, (size_t)length, file) != (size_t)length ||
        fclose(file) != 0) {
        fprintf(stderr, "bundle read failed\n");
        free(data);
        return 1;
    }
    BspBundleView view;
    const int result = bsp_bundle_open(data, (size_t)length, &view);
    if (result != BSP_BUNDLE_OK) {
        fprintf(stderr, "bundle validation failed: %d\n", result);
        free(data);
        return 1;
    }
    printf("bundle valid: bytes=%zu vertices=%u indices=%u draws=%u",
           view.bytes, view.vertex_count, view.index_count, view.draw_count);
    if (view.visibility && verify_visibility_modes(&view) != 0) {
        fprintf(stderr, "visibility verification failed\n");
        free(data);
        return 1;
    }
    if (view.brush_models && view.brush_entities) {
        GoldSrcBrushPlan brush;
        if (goldsrc_brush_plan_build(&brush, &view) != 0) {
            fprintf(stderr, "brush entity plan failed\n");
            free(data);
            return 1;
        }
        printf(" brush_models=%u brush_entities=%u "
               "brush_selection=%u,%u,%u brush_source_modes=%u,%u,%u "
               "brush_draws=%u,%u,%u brush_indices=%u,%u,%u",
               view.brush_model_count, view.brush_entity_count,
               brush.entity_indices[0], brush.entity_indices[1],
               brush.entity_indices[2], brush.source_render_modes[0],
               brush.source_render_modes[1], brush.source_render_modes[2],
               brush.draw_counts[0], brush.draw_counts[1],
               brush.draw_counts[2], brush.index_counts[0],
               brush.index_counts[1], brush.index_counts[2]);
        if (goldsrc_brush_phase4_scene_plan_extend(&brush, &view) == 0)
            printf(" phase4_water=%u:%u:%u phase4_glass=%u:%u:%u",
                   brush.entity_indices[GOLDSRC_BRUSH_INSTANCE_WATER],
                   brush.draw_counts[GOLDSRC_BRUSH_INSTANCE_WATER],
                   brush.index_counts[GOLDSRC_BRUSH_INSTANCE_WATER],
                   brush.entity_indices[GOLDSRC_BRUSH_INSTANCE_GLASS],
                   brush.draw_counts[GOLDSRC_BRUSH_INSTANCE_GLASS],
                   brush.index_counts[GOLDSRC_BRUSH_INSTANCE_GLASS]);
    }
    if (view.lightmap_image)
        printf(" lightmap=%ux%u lightmap_pixels=%u lightmap_faces=%u "
               "lightmap_samples=%u",
               view.lightmap_image->width, view.lightmap_image->height,
               view.lightmap_pixel_count, view.lightmap_face_count,
               view.lightmap_sample_bytes);
    if (view.textures) {
        uint32_t descriptor_dwords = 0u;
        uint32_t minimum_mips = 15u;
        uint32_t maximum_mips = 0u;
        uint64_t chain_bytes = 0u;
        if (bsp_texture_table_required_dwords(&view,
                                                &descriptor_dwords) != 0) {
            fprintf(stderr, "descriptor sizing failed\n");
            free(data);
            return 1;
        }
        for (uint32_t index = 0u; index < view.texture_count; ++index) {
            const BspBundleTexture *const texture = &view.textures[index];
            BspBundleMipLevel base;
            if (bsp_bundle_texture_mip_level(texture, 0u, &base) != 0 ||
                base.offset > texture->bytes ||
                base.bytes != texture->bytes - base.offset) {
                fprintf(stderr, "mip layout derivation failed\n");
                free(data);
                return 1;
            }
            if (texture->mip_count < minimum_mips)
                minimum_mips = texture->mip_count;
            if (texture->mip_count > maximum_mips)
                maximum_mips = texture->mip_count;
            chain_bytes += texture->bytes;
        }
        printf(" textures=%u texture_bytes=%u descriptor_dwords=%u "
               "mip_layout=addr-sw-linear mip_order=smallest-to-base "
               "mip_levels=%u..%u mip_chain_bytes=%llu",
               view.texture_count, view.texture_pixel_bytes,
               descriptor_dwords, minimum_mips, maximum_mips,
               (unsigned long long)chain_bytes);
        BspDynamicLightmapLayout dynamic;
        if (bsp_dynamic_lightmap_select(&view, &dynamic) != 0) {
            fprintf(stderr, "dynamic lightmap selection failed\n");
            free(data);
            return 1;
        }
        printf(" dynamic_patch=%u,%u+%ux%u hit_face=%u patch_bytes=%zu "
               "dirty_span_bytes=%zu",
               dynamic.patch_x, dynamic.patch_y, dynamic.patch_width,
               dynamic.patch_height, dynamic.hit_face, dynamic.patch_bytes,
               dynamic.dirty_span_bytes);
        if (view.lightmap_faces) {
            GoldSrcLightmapLightingPlan lighting;
            if (goldsrc_lightmap_lighting_plan(
                    &view, 65536u, &lighting) != 0) {
                fprintf(stderr, "GoldSrc lighting planning failed\n");
                free(data);
                return 1;
            }
            printf(" lighting_face=%u styles=%u styled_faces=%u "
                   "styled_layers=%u patch=%u,%u+%ux%u patch_bytes=%zu "
                   "source_hash=%016llx target=%.3f,%.3f,%.3f "
                   "forward=%.6f,%.6f,%.6f",
                   lighting.layout.hit_face, lighting.style_count,
                   lighting.styled_face_count, lighting.styled_layer_count,
                   lighting.layout.patch_x, lighting.layout.patch_y,
                   lighting.layout.patch_width, lighting.layout.patch_height,
                   lighting.layout.patch_bytes,
                   (unsigned long long)lighting.source_hash,
                   lighting.target_position[0], lighting.target_position[1],
                   lighting.target_position[2], lighting.target_forward[0],
                   lighting.target_forward[1], lighting.target_forward[2]);
            if (verify_lighting_modes(&view, &lighting) != 0) {
                fprintf(stderr, "GoldSrc lighting mode verification failed\n");
                free(data);
                return 1;
            }
        }
        BspAlphaTestPlan alpha;
        const int alpha_result =
            bsp_alpha_test_plan(&view, view.camera_position, &alpha);
        if (alpha_result == 0)
            printf(" alpha_textures=%u alpha_draws=%u alpha_target=%u:%u",
                   alpha.texture_count, alpha.draw_count,
                   alpha.target_texture, alpha.target_face);
        else if (alpha_result == -2)
            printf(" alpha_textures=0 alpha_draws=0");
        else {
            fprintf(stderr, "alpha-test planning failed: %d\n", alpha_result);
            free(data);
            return 1;
        }
        BspSkyPlan sky;
        const int sky_result =
            bsp_sky_plan(&view, view.camera_position, &sky);
        if (sky_result == 0)
            printf(" sky_textures=%u sky_draws=%u sky_target=%u:%u",
                   sky.texture_count, sky.draw_count,
                   sky.target_texture, sky.target_face);
        else if (sky_result == -2)
            printf(" sky_textures=0 sky_draws=0");
        else {
            fprintf(stderr, "sky planning failed: %d\n", sky_result);
            free(data);
            return 1;
        }
    }
    putchar('\n');
    free(data);
    return 0;
}
