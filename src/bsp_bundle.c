#include "bsp_bundle.h"

#include <limits.h>
#include <string.h>

enum {
    BUNDLE_HEADER_BYTES = 64,
    BUNDLE_CHUNK_BYTES = 32,
    BUNDLE_VERSION = 3,
    MAX_CHUNKS = 64,
};

typedef struct ChunkView {
    uint32_t tag;
    uint32_t offset;
    uint32_t bytes;
    uint32_t count;
    uint32_t stride;
} ChunkView;

_Static_assert(sizeof(BspBundleVertex) == 32u, "bundle vertex ABI");
_Static_assert(sizeof(BspBundleDraw) == 32u, "bundle draw ABI");
_Static_assert(sizeof(BspBundleImage) == 16u, "bundle image ABI");
_Static_assert(sizeof(BspBundleLightmapFace) == 32u,
               "bundle lightmap face ABI");
_Static_assert(sizeof(BspBundleTexture) == 48u, "bundle texture ABI");
_Static_assert(sizeof(BspBundleBrushModel) == 64u, "brush model ABI");
_Static_assert(sizeof(BspBundleBrushEntity) == 96u, "brush entity ABI");
_Static_assert(sizeof(BspBundleVisibilityHeader) == 32u,
               "visibility header ABI");
_Static_assert(sizeof(BspBundleVisibilityPlane) == 16u,
               "visibility plane ABI");
_Static_assert(sizeof(BspBundleVisibilityNode) == 16u,
               "visibility node ABI");
_Static_assert(sizeof(BspBundleVisibilityLeaf) == 40u,
               "visibility leaf ABI");
_Static_assert(sizeof(BspBundleDrawBounds) == 24u, "draw bounds ABI");

static uint32_t align_u32(uint32_t value, uint32_t alignment)
{
    return (value + alignment - 1u) & ~(alignment - 1u);
}

static uint32_t full_mip_count(uint32_t width, uint32_t height)
{
    uint32_t count = 1u;
    while (width > 1u || height > 1u) {
        width = width > 1u ? width >> 1 : 1u;
        height = height > 1u ? height >> 1 : 1u;
        ++count;
    }
    return count;
}

int bsp_bundle_texture_mip_level(const BspBundleTexture *texture,
                                 uint32_t level,
                                 BspBundleMipLevel *out)
{
    if (!texture || !out || texture->width == 0u ||
        texture->height == 0u || texture->width > 16384u ||
        texture->height > 16384u || texture->mip_count == 0u ||
        texture->mip_count > 15u || level >= texture->mip_count)
        return -1;
    uint64_t offset = 0u;
    for (uint32_t candidate = texture->mip_count; candidate-- > 0u;) {
        const uint32_t shifted_width = texture->width >> candidate;
        const uint32_t shifted_height = texture->height >> candidate;
        const uint32_t width = shifted_width ? shifted_width : 1u;
        const uint32_t height = shifted_height ? shifted_height : 1u;
        if (width > UINT32_MAX / 4u)
            return -1;
        const uint32_t row_pitch = align_u32(width * 4u, 256u);
        const uint64_t bytes = (uint64_t)row_pitch * height;
        if (candidate == level) {
            if (offset > UINT32_MAX || bytes > UINT32_MAX)
                return -1;
            *out = (BspBundleMipLevel){
                (uint32_t)offset, (uint32_t)bytes,
                width, height, row_pitch,
            };
            return 0;
        }
        offset += bytes;
        if (offset > UINT32_MAX)
            return -1;
    }
    return -1;
}

static int texture_layout_valid(const BspBundleTexture *texture)
{
    if (!texture || texture->mip_count !=
            full_mip_count(texture->width, texture->height) ||
        texture->mip_count > 15u ||
        texture->width > UINT32_MAX / 4u ||
        texture->row_pitch != align_u32(texture->width * 4u, 256u) ||
        texture->reserved[0] != 0u || texture->reserved[1] != 0u ||
        texture->reserved[2] != 0u)
        return 0;
    BspBundleMipLevel base;
    if (bsp_bundle_texture_mip_level(texture, 0u, &base) != 0)
        return 0;
    return base.offset <= UINT32_MAX - base.bytes &&
           texture->bytes == base.offset + base.bytes;
}

static uint32_t load_u32(const uint8_t *at)
{
    uint32_t value;
    memcpy(&value, at, sizeof(value));
    return value;
}

static float load_f32(const uint8_t *at)
{
    float value;
    memcpy(&value, at, sizeof(value));
    return value;
}

static uint32_t crc32_bytes(const uint8_t *data, size_t bytes)
{
    uint32_t crc = UINT32_C(0xffffffff);
    for (size_t index = 0; index < bytes; ++index) {
        crc ^= data[index];
        for (unsigned bit = 0; bit < 8u; ++bit)
            crc = (crc >> 1) ^ (UINT32_C(0xedb88320) &
                                (uint32_t)-(int32_t)(crc & 1u));
    }
    return ~crc;
}

static int tag_equal(uint32_t tag, const char name[4])
{
    uint32_t expected;
    memcpy(&expected, name, sizeof(expected));
    return tag == expected;
}

int bsp_bundle_open(const void *opaque, size_t bytes, BspBundleView *view)
{
    static const uint8_t magic[8] = {'P','S','5','B','S','P',0,0};
    if (!opaque || !view)
        return BSP_BUNDLE_PRECONDITION;
    memset(view, 0, sizeof(*view));
    if (bytes < BUNDLE_HEADER_BYTES || bytes > UINT32_MAX)
        return BSP_BUNDLE_HEADER_INVALID;
    const uint8_t *const data = opaque;
    const uint32_t version = load_u32(data + 8u);
    const uint32_t header_bytes = load_u32(data + 12u);
    const uint32_t file_bytes = load_u32(data + 16u);
    const uint32_t payload_crc = load_u32(data + 20u);
    const uint32_t chunk_count = load_u32(data + 48u);
    if (memcmp(data, magic, sizeof(magic)) != 0 ||
        version != BUNDLE_VERSION || file_bytes != bytes ||
        chunk_count == 0u || chunk_count > MAX_CHUNKS ||
        header_bytes < BUNDLE_HEADER_BYTES + chunk_count * BUNDLE_CHUNK_BYTES ||
        header_bytes > bytes || (header_bytes & 15u) != 0u ||
        load_u32(data + 52u) != 0u || load_u32(data + 56u) != 0u ||
        load_u32(data + 60u) != 0u)
        return BSP_BUNDLE_HEADER_INVALID;
    if (crc32_bytes(data + header_bytes, bytes - header_bytes) != payload_crc)
        return BSP_BUNDLE_CHECKSUM_MISMATCH;

    ChunkView chunks[MAX_CHUNKS];
    memset(chunks, 0, sizeof(chunks));
    for (uint32_t index = 0; index < chunk_count; ++index) {
        const uint8_t *const row = data + BUNDLE_HEADER_BYTES +
                                   index * BUNDLE_CHUNK_BYTES;
        ChunkView *const chunk = &chunks[index];
        chunk->tag = load_u32(row);
        chunk->offset = load_u32(row + 4u);
        chunk->bytes = load_u32(row + 8u);
        chunk->count = load_u32(row + 12u);
        chunk->stride = load_u32(row + 16u);
        if (chunk->tag == 0u || chunk->offset < header_bytes ||
            (chunk->offset & 15u) != 0u || chunk->bytes == 0u ||
            chunk->stride == 0u || chunk->count == 0u ||
            chunk->count > UINT32_MAX / chunk->stride ||
            chunk->count * chunk->stride != chunk->bytes ||
            chunk->offset > bytes || chunk->bytes > bytes - chunk->offset ||
            load_u32(row + 24u) != 0u || load_u32(row + 28u) != 0u)
            return BSP_BUNDLE_DIRECTORY_INVALID;
        if (crc32_bytes(data + chunk->offset, chunk->bytes) !=
            load_u32(row + 20u))
            return BSP_BUNDLE_CHECKSUM_MISMATCH;
        for (uint32_t prior = 0; prior < index; ++prior) {
            const uint64_t begin = chunk->offset;
            const uint64_t end = begin + chunk->bytes;
            const uint64_t prior_begin = chunks[prior].offset;
            const uint64_t prior_end = prior_begin + chunks[prior].bytes;
            if (chunk->tag == chunks[prior].tag ||
                (begin < prior_end && prior_begin < end))
                return BSP_BUNDLE_DIRECTORY_INVALID;
        }
    }

    const ChunkView *vertex_chunk = 0;
    const ChunkView *index_chunk = 0;
    const ChunkView *draw_chunk = 0;
    const ChunkView *lightmap_header_chunk = 0;
    const ChunkView *lightmap_pixels_chunk = 0;
    const ChunkView *lightmap_face_chunk = 0;
    const ChunkView *lightmap_sample_chunk = 0;
    const ChunkView *texture_metadata_chunk = 0;
    const ChunkView *texture_pixels_chunk = 0;
    const ChunkView *brush_model_chunk = 0;
    const ChunkView *brush_entity_chunk = 0;
    const ChunkView *visibility_header_chunk = 0;
    const ChunkView *visibility_plane_chunk = 0;
    const ChunkView *visibility_node_chunk = 0;
    const ChunkView *visibility_leaf_chunk = 0;
    const ChunkView *visibility_draw_chunk = 0;
    const ChunkView *visibility_pvs_chunk = 0;
    const ChunkView *draw_bounds_chunk = 0;
    for (uint32_t index = 0; index < chunk_count; ++index) {
        if (tag_equal(chunks[index].tag, "VERT")) vertex_chunk = &chunks[index];
        if (tag_equal(chunks[index].tag, "INDX")) index_chunk = &chunks[index];
        if (tag_equal(chunks[index].tag, "DRAW")) draw_chunk = &chunks[index];
        if (tag_equal(chunks[index].tag, "LMHD")) lightmap_header_chunk = &chunks[index];
        if (tag_equal(chunks[index].tag, "LMPX")) lightmap_pixels_chunk = &chunks[index];
        if (tag_equal(chunks[index].tag, "LMFM")) lightmap_face_chunk = &chunks[index];
        if (tag_equal(chunks[index].tag, "LMSP")) lightmap_sample_chunk = &chunks[index];
        if (tag_equal(chunks[index].tag, "TEXM")) texture_metadata_chunk = &chunks[index];
        if (tag_equal(chunks[index].tag, "TEXP")) texture_pixels_chunk = &chunks[index];
        if (tag_equal(chunks[index].tag, "BMOD")) brush_model_chunk = &chunks[index];
        if (tag_equal(chunks[index].tag, "BENT")) brush_entity_chunk = &chunks[index];
        if (tag_equal(chunks[index].tag, "VHDR")) visibility_header_chunk = &chunks[index];
        if (tag_equal(chunks[index].tag, "VPLN")) visibility_plane_chunk = &chunks[index];
        if (tag_equal(chunks[index].tag, "VNOD")) visibility_node_chunk = &chunks[index];
        if (tag_equal(chunks[index].tag, "VLEF")) visibility_leaf_chunk = &chunks[index];
        if (tag_equal(chunks[index].tag, "VDRW")) visibility_draw_chunk = &chunks[index];
        if (tag_equal(chunks[index].tag, "VPVS")) visibility_pvs_chunk = &chunks[index];
        if (tag_equal(chunks[index].tag, "DBND")) draw_bounds_chunk = &chunks[index];
    }
    if (!vertex_chunk || !index_chunk || !draw_chunk ||
        vertex_chunk->stride != sizeof(BspBundleVertex) ||
        index_chunk->stride != sizeof(uint16_t) ||
        draw_chunk->stride != sizeof(BspBundleDraw) ||
        index_chunk->count % 3u != 0u)
        return BSP_BUNDLE_GEOMETRY_INVALID;
    if (!!lightmap_header_chunk != !!lightmap_pixels_chunk)
        return BSP_BUNDLE_GEOMETRY_INVALID;
    if (!!lightmap_face_chunk != !!lightmap_sample_chunk ||
        (lightmap_face_chunk && !lightmap_header_chunk))
        return BSP_BUNDLE_GEOMETRY_INVALID;
    if (lightmap_header_chunk) {
        if (lightmap_header_chunk->count != 1u ||
            lightmap_header_chunk->stride != sizeof(BspBundleImage) ||
            lightmap_pixels_chunk->stride != 4u)
            return BSP_BUNDLE_GEOMETRY_INVALID;
        const BspBundleImage *const image =
            (const BspBundleImage *)(data + lightmap_header_chunk->offset);
        if (image->width == 0u || image->height == 0u ||
            image->width > 2048u || image->height > 2048u ||
            (lightmap_pixels_chunk->offset & 255u) != 0u ||
            image->row_pitch != image->width * 4u ||
            (image->row_pitch & 255u) != 0u ||
            image->format != BSP_BUNDLE_IMAGE_RGBA8_UNORM ||
            image->width > UINT32_MAX / image->height ||
            image->width * image->height != lightmap_pixels_chunk->count)
            return BSP_BUNDLE_GEOMETRY_INVALID;
    }
    const BspBundleLightmapFace *lightmap_faces = 0;
    if (lightmap_face_chunk) {
        if (lightmap_face_chunk->stride !=
                sizeof(BspBundleLightmapFace) ||
            lightmap_sample_chunk->stride != 1u)
            return BSP_BUNDLE_GEOMETRY_INVALID;
        lightmap_faces = (const BspBundleLightmapFace *)(
            data + lightmap_face_chunk->offset);
        const BspBundleImage *const image =
            (const BspBundleImage *)(data + lightmap_header_chunk->offset);
        uint32_t expected_sample_offset = 0u;
        for (uint32_t index = 0u; index < lightmap_face_chunk->count;
             ++index) {
            const BspBundleLightmapFace *const face = &lightmap_faces[index];
            uint32_t style_count = 0u;
            while (style_count < 4u && face->styles[style_count] != 255u)
                ++style_count;
            for (uint32_t style = style_count; style < 4u; ++style)
                if (face->styles[style] != 255u)
                    return BSP_BUNDLE_GEOMETRY_INVALID;
            const uint64_t plane_bytes =
                (uint64_t)face->width * face->height * 3u;
            if (style_count == 0u || face->width == 0u ||
                face->height == 0u || face->atlas_x >= image->width ||
                face->width > image->width - face->atlas_x ||
                face->atlas_y >= image->height ||
                face->height > image->height - face->atlas_y ||
                plane_bytes > UINT32_MAX ||
                plane_bytes * style_count != face->sample_bytes ||
                face->sample_offset != expected_sample_offset ||
                face->sample_offset > lightmap_sample_chunk->bytes ||
                face->sample_bytes >
                    lightmap_sample_chunk->bytes - face->sample_offset ||
                (index > 0u && lightmap_faces[index - 1u].face_id >=
                                  face->face_id))
                return BSP_BUNDLE_GEOMETRY_INVALID;
            expected_sample_offset += face->sample_bytes;
        }
        if (expected_sample_offset != lightmap_sample_chunk->bytes)
            return BSP_BUNDLE_GEOMETRY_INVALID;
    }
    if (!!texture_metadata_chunk != !!texture_pixels_chunk)
        return BSP_BUNDLE_GEOMETRY_INVALID;
    if (!!brush_model_chunk != !!brush_entity_chunk)
        return BSP_BUNDLE_GEOMETRY_INVALID;
    const unsigned visibility_chunks =
        !!visibility_header_chunk + !!visibility_plane_chunk +
        !!visibility_node_chunk + !!visibility_leaf_chunk +
        !!visibility_draw_chunk + !!visibility_pvs_chunk +
        !!draw_bounds_chunk;
    if (visibility_chunks != 0u && visibility_chunks != 7u)
        return BSP_BUNDLE_GEOMETRY_INVALID;
    const BspBundleTexture *textures = 0;
    if (texture_metadata_chunk) {
        if (texture_metadata_chunk->stride != sizeof(BspBundleTexture) ||
            texture_pixels_chunk->stride != 1u ||
            (texture_pixels_chunk->offset & 255u) != 0u)
            return BSP_BUNDLE_GEOMETRY_INVALID;
        textures = (const BspBundleTexture *)(
            data + texture_metadata_chunk->offset);
        for (uint32_t index = 0; index < texture_metadata_chunk->count; ++index) {
            const BspBundleTexture *const texture = &textures[index];
            if (texture->width == 0u || texture->height == 0u ||
                texture->width > 16384u || texture->height > 16384u ||
                !texture_layout_valid(texture) ||
                texture->format != BSP_BUNDLE_IMAGE_RGBA8_UNORM ||
                (texture->flags & ~(BSP_BUNDLE_TEXTURE_TRANSPARENT |
                                    BSP_BUNDLE_TEXTURE_FALLBACK |
                                    BSP_BUNDLE_TEXTURE_NODRAW |
                                    BSP_BUNDLE_TEXTURE_SKY)) != 0u ||
                ((texture->flags & BSP_BUNDLE_TEXTURE_SKY) != 0u &&
                 (texture->flags & (BSP_BUNDLE_TEXTURE_TRANSPARENT |
                                    BSP_BUNDLE_TEXTURE_NODRAW)) != 0u) ||
                texture->name_hash == 0u ||
                (texture->offset & 255u) != 0u ||
                texture->offset > texture_pixels_chunk->bytes ||
                texture->bytes > texture_pixels_chunk->bytes - texture->offset)
                return BSP_BUNDLE_GEOMETRY_INVALID;
            if (index > 0u) {
                const BspBundleTexture *const prior = &textures[index - 1u];
                if (texture->offset < prior->offset + prior->bytes)
                    return BSP_BUNDLE_GEOMETRY_INVALID;
            }
        }
    }
    const BspBundleBrushModel *brush_models = 0;
    const BspBundleBrushEntity *brush_entities = 0;
    if (brush_model_chunk) {
        if (brush_model_chunk->stride != sizeof(BspBundleBrushModel) ||
            brush_entity_chunk->stride != sizeof(BspBundleBrushEntity) ||
            brush_model_chunk->count < 2u)
            return BSP_BUNDLE_GEOMETRY_INVALID;
        brush_models = (const BspBundleBrushModel *)(
            data + brush_model_chunk->offset);
        brush_entities = (const BspBundleBrushEntity *)(
            data + brush_entity_chunk->offset);
        for (uint32_t index = 0u; index < brush_model_chunk->count; ++index) {
            const BspBundleBrushModel *const model = &brush_models[index];
            if (model->model_index != index || model->face_count == 0u ||
                model->first_face > UINT32_MAX - model->face_count ||
                (index == 0u && model->first_face != 0u))
                return BSP_BUNDLE_GEOMETRY_INVALID;
            for (uint32_t component = 0u; component < 3u; ++component)
                if (!__builtin_isfinite(model->mins[component]) ||
                    !__builtin_isfinite(model->maxs[component]) ||
                    !__builtin_isfinite(model->origin[component]) ||
                    model->mins[component] > model->maxs[component] ||
                    model->reserved[component] != 0.0f)
                    return BSP_BUNDLE_GEOMETRY_INVALID;
        }
        for (uint32_t index = 0u; index < brush_entity_chunk->count; ++index) {
            const BspBundleBrushEntity *const entity = &brush_entities[index];
            if (entity->model_index == 0u ||
                entity->model_index >= brush_model_chunk->count ||
                entity->first_face !=
                    brush_models[entity->model_index].first_face ||
                entity->face_count !=
                    brush_models[entity->model_index].face_count ||
                entity->render_mode > 5u || entity->classname_hash == 0u ||
                entity->render_color[3] < 0.0f ||
                entity->render_color[3] > 1.0f ||
                entity->reserved[0] != 0u || entity->reserved[1] != 0u ||
                entity->reserved[2] != 0u)
                return BSP_BUNDLE_GEOMETRY_INVALID;
            for (uint32_t component = 0u; component < 3u; ++component)
                if (!__builtin_isfinite(entity->mins[component]) ||
                    !__builtin_isfinite(entity->maxs[component]) ||
                    !__builtin_isfinite(entity->origin[component]) ||
                    !__builtin_isfinite(entity->angles[component]) ||
                    !__builtin_isfinite(entity->render_color[component]) ||
                    entity->mins[component] > entity->maxs[component] ||
                    entity->render_color[component] < 0.0f ||
                    entity->render_color[component] > 1.0f)
                    return BSP_BUNDLE_GEOMETRY_INVALID;
        }
    }

    const BspBundleVisibilityHeader *visibility_header = 0;
    const BspBundleVisibilityPlane *visibility_planes = 0;
    const BspBundleVisibilityNode *visibility_nodes = 0;
    const BspBundleVisibilityLeaf *visibility_leaves = 0;
    const uint32_t *visibility_draws = 0;
    const BspBundleDrawBounds *draw_bounds = 0;
    if (visibility_header_chunk) {
        if (visibility_header_chunk->count != 1u ||
            visibility_header_chunk->stride !=
                sizeof(BspBundleVisibilityHeader) ||
            visibility_plane_chunk->stride !=
                sizeof(BspBundleVisibilityPlane) ||
            visibility_node_chunk->stride !=
                sizeof(BspBundleVisibilityNode) ||
            visibility_leaf_chunk->stride !=
                sizeof(BspBundleVisibilityLeaf) ||
            visibility_draw_chunk->stride != sizeof(uint32_t) ||
            visibility_pvs_chunk->stride != 1u ||
            draw_bounds_chunk->stride != sizeof(BspBundleDrawBounds) ||
            draw_bounds_chunk->count != draw_chunk->count)
            return BSP_BUNDLE_GEOMETRY_INVALID;
        visibility_header = (const BspBundleVisibilityHeader *)(
            data + visibility_header_chunk->offset);
        visibility_planes = (const BspBundleVisibilityPlane *)(
            data + visibility_plane_chunk->offset);
        visibility_nodes = (const BspBundleVisibilityNode *)(
            data + visibility_node_chunk->offset);
        visibility_leaves = (const BspBundleVisibilityLeaf *)(
            data + visibility_leaf_chunk->offset);
        visibility_draws = (const uint32_t *)(
            data + visibility_draw_chunk->offset);
        draw_bounds = (const BspBundleDrawBounds *)(
            data + draw_bounds_chunk->offset);
        const uint64_t pvs_bytes =
            (uint64_t)visibility_header->leaf_count *
            visibility_header->pvs_row_bytes;
        if (visibility_header->plane_count != visibility_plane_chunk->count ||
            visibility_header->node_count != visibility_node_chunk->count ||
            visibility_header->leaf_count != visibility_leaf_chunk->count ||
            visibility_header->draw_ref_count !=
                visibility_draw_chunk->count ||
            visibility_header->leaf_count < 2u ||
            visibility_header->pvs_row_bytes !=
                ((visibility_header->leaf_count - 1u + 7u) >> 3u) ||
            pvs_bytes != visibility_pvs_chunk->bytes ||
            visibility_header->root_node >= visibility_header->node_count ||
            visibility_header->world_face_count == 0u ||
            visibility_header->world_first_face >
                UINT32_MAX - visibility_header->world_face_count ||
            (brush_models &&
             (visibility_header->world_first_face !=
                  brush_models[0].first_face ||
              visibility_header->world_face_count !=
                  brush_models[0].face_count)))
            return BSP_BUNDLE_GEOMETRY_INVALID;
        for (uint32_t index = 0u; index < visibility_header->plane_count;
             ++index) {
            const BspBundleVisibilityPlane *const plane =
                &visibility_planes[index];
            float magnitude = 0.0f;
            for (uint32_t axis = 0u; axis < 3u; ++axis) {
                if (!__builtin_isfinite(plane->normal[axis]))
                    return BSP_BUNDLE_GEOMETRY_INVALID;
                magnitude += plane->normal[axis] * plane->normal[axis];
            }
            if (!__builtin_isfinite(plane->distance) || magnitude < 0.25f ||
                magnitude > 4.0f)
                return BSP_BUNDLE_GEOMETRY_INVALID;
        }
        for (uint32_t index = 0u; index < visibility_header->node_count;
             ++index) {
            const BspBundleVisibilityNode *const node =
                &visibility_nodes[index];
            if (node->plane >= visibility_header->plane_count ||
                node->reserved != 0u)
                return BSP_BUNDLE_GEOMETRY_INVALID;
            for (uint32_t side = 0u; side < 2u; ++side) {
                const int32_t child = node->children[side];
                if ((child >= 0 &&
                     (uint32_t)child >= visibility_header->node_count) ||
                    (child == INT32_MIN) ||
                    (child < 0 &&
                     (uint32_t)(-child - 1) >=
                         visibility_header->leaf_count))
                    return BSP_BUNDLE_GEOMETRY_INVALID;
            }
        }
        for (uint32_t index = 0u; index < visibility_header->leaf_count;
             ++index) {
            const BspBundleVisibilityLeaf *const leaf =
                &visibility_leaves[index];
            if ((uint64_t)leaf->pvs_offset !=
                    (uint64_t)index * visibility_header->pvs_row_bytes ||
                leaf->first_draw_ref > visibility_header->draw_ref_count ||
                leaf->draw_ref_count > visibility_header->draw_ref_count -
                    leaf->first_draw_ref)
                return BSP_BUNDLE_GEOMETRY_INVALID;
            for (uint32_t axis = 0u; axis < 3u; ++axis)
                if (!__builtin_isfinite(leaf->mins[axis]) ||
                    !__builtin_isfinite(leaf->maxs[axis]) ||
                    leaf->mins[axis] > leaf->maxs[axis])
                    return BSP_BUNDLE_GEOMETRY_INVALID;
            for (uint32_t ref = 0u; ref < leaf->draw_ref_count; ++ref) {
                const uint32_t draw =
                    visibility_draws[leaf->first_draw_ref + ref];
                if (draw >= draw_chunk->count ||
                    (ref > 0u && draw <= visibility_draws[
                        leaf->first_draw_ref + ref - 1u]))
                    return BSP_BUNDLE_GEOMETRY_INVALID;
            }
        }
        for (uint32_t index = 0u; index < draw_bounds_chunk->count; ++index)
            for (uint32_t axis = 0u; axis < 3u; ++axis)
                if (!__builtin_isfinite(draw_bounds[index].mins[axis]) ||
                    !__builtin_isfinite(draw_bounds[index].maxs[axis]) ||
                    draw_bounds[index].mins[axis] >
                        draw_bounds[index].maxs[axis])
                    return BSP_BUNDLE_GEOMETRY_INVALID;
    }

    const uint16_t *const indices =
        (const uint16_t *)(data + index_chunk->offset);
    const BspBundleDraw *const draws =
        (const BspBundleDraw *)(data + draw_chunk->offset);
    for (uint32_t index = 0; index < index_chunk->count; ++index)
        if (indices[index] >= vertex_chunk->count)
            return BSP_BUNDLE_GEOMETRY_INVALID;
    for (uint32_t index = 0; index < draw_chunk->count; ++index) {
        const BspBundleDraw *const draw = &draws[index];
        if (draw->index_count == 0u || draw->index_count % 3u != 0u ||
            draw->first_index > index_chunk->count ||
            draw->index_count > index_chunk->count - draw->first_index ||
            draw->reserved[0] != 0u || draw->reserved[1] != 0u ||
            (!lightmap_header_chunk && draw->lightmap != UINT32_MAX) ||
            (lightmap_header_chunk && draw->lightmap != 0u &&
             draw->lightmap != UINT32_MAX) ||
            (texture_metadata_chunk &&
             (draw->base_texture >= texture_metadata_chunk->count ||
              (textures[draw->base_texture].flags &
               BSP_BUNDLE_TEXTURE_NODRAW) != 0u)) ||
            (index > 0u && (draw[-1].base_texture > draw->base_texture ||
             (draw[-1].base_texture == draw->base_texture &&
              draw[-1].face_id >= draw->face_id))))
            return BSP_BUNDLE_GEOMETRY_INVALID;
    }
    if (visibility_header) {
        const uint32_t first = visibility_header->world_first_face;
        const uint32_t count = visibility_header->world_face_count;
        for (uint32_t ref = 0u; ref < visibility_header->draw_ref_count;
             ++ref) {
            const uint32_t face = draws[visibility_draws[ref]].face_id;
            if (face < first || face - first >= count)
                return BSP_BUNDLE_GEOMETRY_INVALID;
        }
    }
    if (lightmap_faces) {
        for (uint32_t metadata_index = 0u;
             metadata_index < lightmap_face_chunk->count;
             ++metadata_index) {
            int found = 0;
            for (uint32_t draw_index = 0u; draw_index < draw_chunk->count;
                 ++draw_index)
                if (draws[draw_index].face_id ==
                        lightmap_faces[metadata_index].face_id) {
                    if (draws[draw_index].lightmap != 0u)
                        return BSP_BUNDLE_GEOMETRY_INVALID;
                    found = 1;
                    break;
                }
            if (!found)
                return BSP_BUNDLE_GEOMETRY_INVALID;
        }
    }

    view->data = data;
    view->bytes = bytes;
    view->vertices = (const BspBundleVertex *)(data + vertex_chunk->offset);
    view->vertex_count = vertex_chunk->count;
    view->indices = indices;
    view->index_count = index_chunk->count;
    view->draws = draws;
    view->draw_count = draw_chunk->count;
    if (lightmap_header_chunk) {
        view->lightmap_image = (const BspBundleImage *)(
            data + lightmap_header_chunk->offset);
        view->lightmap_pixels = data + lightmap_pixels_chunk->offset;
        view->lightmap_pixel_count = lightmap_pixels_chunk->count;
    }
    if (lightmap_face_chunk) {
        view->lightmap_faces = lightmap_faces;
        view->lightmap_face_count = lightmap_face_chunk->count;
        view->lightmap_samples = data + lightmap_sample_chunk->offset;
        view->lightmap_sample_bytes = lightmap_sample_chunk->bytes;
    }
    if (texture_metadata_chunk) {
        view->textures = (const BspBundleTexture *)(
            data + texture_metadata_chunk->offset);
        view->texture_count = texture_metadata_chunk->count;
        view->texture_pixels = data + texture_pixels_chunk->offset;
        view->texture_pixel_bytes = texture_pixels_chunk->bytes;
    }
    if (brush_model_chunk) {
        view->brush_models = brush_models;
        view->brush_model_count = brush_model_chunk->count;
        view->brush_entities = brush_entities;
        view->brush_entity_count = brush_entity_chunk->count;
    }
    if (visibility_header_chunk) {
        view->visibility = visibility_header;
        view->visibility_planes = visibility_planes;
        view->visibility_nodes = visibility_nodes;
        view->visibility_leaves = visibility_leaves;
        view->visibility_draw_refs = visibility_draws;
        view->visibility_pvs = data + visibility_pvs_chunk->offset;
        view->draw_bounds = draw_bounds;
    }
    for (uint32_t vertex = 0; vertex < vertex_chunk->count; ++vertex) {
        const BspBundleVertex *const item = &view->vertices[vertex];
        for (unsigned component = 0; component < 3u; ++component)
            if (!__builtin_isfinite(item->position[component]))
                return BSP_BUNDLE_GEOMETRY_INVALID;
        for (unsigned component = 0; component < 2u; ++component)
            if (!__builtin_isfinite(item->base_uv[component]) ||
                !__builtin_isfinite(item->light_uv[component]))
                return BSP_BUNDLE_GEOMETRY_INVALID;
        if (lightmap_header_chunk &&
            (item->light_uv[0] < 0.0f || item->light_uv[0] > 1.0f ||
             item->light_uv[1] < 0.0f || item->light_uv[1] > 1.0f))
            return BSP_BUNDLE_GEOMETRY_INVALID;
    }
    for (unsigned component = 0; component < 3u; ++component) {
        view->camera_position[component] = load_f32(data + 24u + component * 4u);
        view->camera_forward[component] = load_f32(data + 36u + component * 4u);
        if (!__builtin_isfinite(view->camera_position[component]) ||
            !__builtin_isfinite(view->camera_forward[component]))
            return BSP_BUNDLE_GEOMETRY_INVALID;
    }
    return BSP_BUNDLE_OK;
}
