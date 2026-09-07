#include "goldsrc_visibility.h"

#include "bsp_flat_scene.h"

#include <string.h>

typedef struct Plane { float x, y, z, w; } Plane;

static uint64_t hash_bytes(const void *opaque, size_t bytes)
{
    const uint8_t *data = opaque;
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t index = 0u; index < bytes; ++index) {
        hash ^= data[index];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

GoldSrcVisibilityMode goldsrc_visibility_mode(uint64_t frame_index)
{
    return (GoldSrcVisibilityMode)(
        (frame_index / GOLDSRC_VISIBILITY_HOLD_FRAMES) %
        GOLDSRC_VISIBILITY_MODE_COUNT);
}

const char *goldsrc_visibility_mode_name(GoldSrcVisibilityMode mode)
{
    static const char *const names[GOLDSRC_VISIBILITY_MODE_COUNT] = {
        "control", "pvs", "frustum", "combined",
    };
    return mode < GOLDSRC_VISIBILITY_MODE_COUNT ? names[mode] : "invalid";
}

int goldsrc_visibility_plan_build(GoldSrcVisibilityPlan *out,
                                  const BspBundleView *bundle)
{
    if (!out || !bundle || !bundle->visibility ||
        !bundle->visibility_planes || !bundle->visibility_nodes ||
        !bundle->visibility_leaves || !bundle->visibility_draw_refs ||
        !bundle->visibility_pvs || !bundle->draw_bounds ||
        !bundle->draws || !bundle->textures || bundle->draw_count == 0u)
        return -1;
    const BspBundleVisibilityHeader *const source = bundle->visibility;
    *out = (GoldSrcVisibilityPlan){
        source->plane_count, source->node_count, source->leaf_count,
        source->pvs_row_bytes, source->draw_ref_count,
        source->world_first_face, source->world_face_count,
    };
    return 0;
}

static int camera_leaf(const BspBundleView *bundle,
                       const float position[3], uint32_t *out)
{
    int32_t child = (int32_t)bundle->visibility->root_node;
    for (uint32_t depth = 0u;
         depth <= bundle->visibility->node_count; ++depth) {
        if (child < 0) {
            const uint32_t leaf = (uint32_t)(-child - 1);
            if (leaf >= bundle->visibility->leaf_count)
                return -1;
            *out = leaf;
            return 0;
        }
        if ((uint32_t)child >= bundle->visibility->node_count)
            return -1;
        const BspBundleVisibilityNode *const node =
            &bundle->visibility_nodes[child];
        const BspBundleVisibilityPlane *const plane =
            &bundle->visibility_planes[node->plane];
        const float distance =
            plane->normal[0] * position[0] +
            plane->normal[1] * position[1] +
            plane->normal[2] * position[2] - plane->distance;
        child = node->children[distance >= 0.0f ? 0u : 1u];
    }
    return -1;
}

static int pvs_leaf_visible(const BspBundleView *bundle,
                            uint32_t camera, uint32_t candidate)
{
    if (candidate == 0u)
        return 0;
    if (candidate == camera)
        return 1;
    const uint32_t bit = candidate - 1u;
    const BspBundleVisibilityLeaf *const leaf =
        &bundle->visibility_leaves[camera];
    return (bundle->visibility_pvs[leaf->pvs_offset + (bit >> 3u)] &
            (uint8_t)(1u << (bit & 7u))) != 0u;
}

static void frustum_planes(const float m[16], Plane planes[6])
{
    planes[0] = (Plane){m[3]+m[0], m[7]+m[4], m[11]+m[8], m[15]+m[12]};
    planes[1] = (Plane){m[3]-m[0], m[7]-m[4], m[11]-m[8], m[15]-m[12]};
    planes[2] = (Plane){m[3]+m[1], m[7]+m[5], m[11]+m[9], m[15]+m[13]};
    planes[3] = (Plane){m[3]-m[1], m[7]-m[5], m[11]-m[9], m[15]-m[13]};
    planes[4] = (Plane){m[3]+m[2], m[7]+m[6], m[11]+m[10],m[15]+m[14]};
    planes[5] = (Plane){m[3]-m[2], m[7]-m[6], m[11]-m[10],m[15]-m[14]};
}

static int bounds_in_frustum(const BspBundleDrawBounds *bounds,
                             const Plane planes[6])
{
    for (uint32_t index = 0u; index < 6u; ++index) {
        const Plane *const plane = &planes[index];
        const float x = plane->x >= 0.0f ? bounds->maxs[0] : bounds->mins[0];
        const float y = plane->y >= 0.0f ? bounds->maxs[1] : bounds->mins[1];
        const float z = plane->z >= 0.0f ? bounds->maxs[2] : bounds->mins[2];
        if (plane->x*x + plane->y*y + plane->z*z + plane->w < 0.0f)
            return 0;
    }
    return 1;
}

static int world_draw(const GoldSrcVisibilityPlan *plan,
                      const BspBundleDraw *draw)
{
    return draw->face_id >= plan->world_first_face &&
           draw->face_id - plan->world_first_face < plan->world_face_count;
}

int goldsrc_visibility_frame_build(
    GoldSrcVisibilityFrame *out, const GoldSrcVisibilityPlan *plan,
    const BspBundleView *bundle, Ps5TransientRing *ring,
    uint32_t slot_index, const float camera_position[3],
    const float camera_forward[3], float aspect_ratio,
    uint64_t frame_index)
{
    if (!out || !plan || !bundle || !ring ||
        slot_index >= ring->slot_count || !camera_position ||
        !camera_forward || bundle->draw_count == 0u)
        return -1;
    memset(out, 0, sizeof(*out));
    const size_t start = ring->slots[slot_index].used;
    Ps5TransientSlice slice;
    if (ps5_transient_ring_allocate(ring, slot_index, bundle->draw_count,
                                    16u, &slice) != PS5_TRANSIENT_OK)
        return -2;
    uint8_t *const mask = slice.cpu;
    memset(mask, 0, bundle->draw_count);
    out->draw_mask = mask;
    out->draw_mask_bytes = bundle->draw_count;
    out->mode = goldsrc_visibility_mode(frame_index);
    if (camera_leaf(bundle, camera_position, &out->camera_leaf) != 0 ||
        out->camera_leaf == 0u)
        return -3;

    uint8_t *const pvs_mask = mask;
    for (uint32_t leaf = 1u; leaf < plan->leaves; ++leaf) {
        if (!pvs_leaf_visible(bundle, out->camera_leaf, leaf))
            continue;
        ++out->visible_leaves;
        const BspBundleVisibilityLeaf *const source =
            &bundle->visibility_leaves[leaf];
        for (uint32_t ref = 0u; ref < source->draw_ref_count; ++ref)
            pvs_mask[bundle->visibility_draw_refs[
                source->first_draw_ref + ref]] = 1u;
    }

    float matrix[16];
    if (bsp_flat_camera_matrix(matrix, camera_position, camera_forward,
                               aspect_ratio) != 0)
        return -3;
    Plane planes[6];
    frustum_planes(matrix, planes);
    for (uint32_t draw = 0u; draw < bundle->draw_count; ++draw) {
        const int world = world_draw(plan, &bundle->draws[draw]);
        const int pvs = world && pvs_mask[draw];
        const int frustum = world &&
            bounds_in_frustum(&bundle->draw_bounds[draw], planes);
        if (world)
            ++out->world_draws;
        if (world && !pvs)
            ++out->pvs_culled;
        if (world && !frustum)
            ++out->frustum_culled;
        const int selected =
            out->mode == GOLDSRC_VISIBILITY_MODE_CONTROL ? world :
            out->mode == GOLDSRC_VISIBILITY_MODE_PVS ? pvs :
            out->mode == GOLDSRC_VISIBILITY_MODE_FRUSTUM ? frustum :
            pvs && frustum;
        mask[draw] = (uint8_t)selected;
        if (!selected)
            continue;
        ++out->selected_draws;
        const uint32_t flags =
            bundle->textures[bundle->draws[draw].base_texture].flags;
        if (flags & BSP_BUNDLE_TEXTURE_SKY)
            ++out->sky_draws;
        else if (flags & BSP_BUNDLE_TEXTURE_TRANSPARENT)
            ++out->alpha_draws;
        else
            ++out->opaque_draws;
    }
    if (out->world_draws == 0u || out->selected_draws == 0u ||
        out->opaque_draws + out->alpha_draws + out->sky_draws !=
            out->selected_draws || out->pvs_culled == 0u ||
        out->frustum_culled == 0u)
        return -4;
    out->mask_hash = hash_bytes(mask, bundle->draw_count);
    out->transient_bytes = ring->slots[slot_index].used - start;
    return 0;
}
