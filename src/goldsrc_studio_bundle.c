#include "goldsrc_studio_bundle.h"

#include <math.h>
#include <string.h>

_Static_assert(sizeof(GoldSrcStudioBundleHeader) == 160u,
               "studio bundle header ABI");
_Static_assert(sizeof(GoldSrcStudioPose) == 28u, "studio pose ABI");
_Static_assert(sizeof(GoldSrcStudioSourceVertex) == 36u,
               "studio source vertex ABI");
_Static_assert(sizeof(GoldSrcStudioDraw) == 16u, "studio draw ABI");
_Static_assert(sizeof(GoldSrcStudioTexture) == 32u, "studio texture ABI");

static uint32_t crc32_bytes(const uint8_t *data, size_t bytes)
{
    uint32_t crc = UINT32_C(0xffffffff);
    for (size_t index = 0u; index < bytes; ++index) {
        crc ^= data[index];
        for (unsigned bit = 0u; bit < 8u; ++bit)
            crc = (crc >> 1) ^ (UINT32_C(0xedb88320) &
                                (uint32_t)-(int32_t)(crc & 1u));
    }
    return ~crc;
}

static int span(size_t bytes, uint32_t offset, size_t count, size_t stride,
                size_t alignment)
{
    if (offset < sizeof(GoldSrcStudioBundleHeader) || alignment == 0u ||
        (offset & (alignment - 1u)) != 0u ||
        (count != 0u && stride > SIZE_MAX / count))
        return 0;
    const size_t required = count * stride;
    return offset <= bytes && required <= bytes - offset;
}

static int finite3(const float value[3])
{
    return __builtin_isfinite(value[0]) && __builtin_isfinite(value[1]) &&
           __builtin_isfinite(value[2]);
}

int goldsrc_studio_bundle_open(const void *data, size_t bytes,
                               GoldSrcStudioBundleView *view)
{
    static const uint8_t magic[8] = {'P','S','5','M','D','L',0,0};
    if (!data || !view || bytes < sizeof(GoldSrcStudioBundleHeader))
        return GOLDSRC_STUDIO_BUNDLE_PRECONDITION;
    const uint8_t *base = data;
    const GoldSrcStudioBundleHeader *header = data;
    if (memcmp(header->magic, magic, sizeof(magic)) != 0 ||
        header->version != GOLDSRC_STUDIO_BUNDLE_VERSION ||
        header->header_bytes != sizeof(*header) ||
        header->file_bytes != bytes || header->bone_count == 0u ||
        header->bone_count > GOLDSRC_STUDIO_MAX_BONES ||
        header->frame_count < 2u ||
        header->frame_count > GOLDSRC_STUDIO_MAX_FRAMES ||
        !(header->fps > 0.0f) || !__builtin_isfinite(header->fps) ||
        header->vertex_count == 0u || header->vertex_count > UINT16_MAX ||
        header->index_count == 0u || header->index_count > UINT16_MAX ||
        header->index_count % 3u != 0u || header->draw_count == 0u ||
        header->texture_count == 0u || header->pixels_bytes == 0u ||
        !finite3(header->bounds_min) || !finite3(header->bounds_max))
        return GOLDSRC_STUDIO_BUNDLE_HEADER_INVALID;
    for (unsigned axis = 0u; axis < 3u; ++axis)
        if (!(header->bounds_max[axis] > header->bounds_min[axis]))
            return GOLDSRC_STUDIO_BUNDLE_HEADER_INVALID;
    for (unsigned index = 0u; index < 6u; ++index)
        if (header->reserved[index] != 0u)
            return GOLDSRC_STUDIO_BUNDLE_HEADER_INVALID;
    if (crc32_bytes(base + header->header_bytes,
                    bytes - header->header_bytes) != header->payload_crc32)
        return GOLDSRC_STUDIO_BUNDLE_CHECKSUM_MISMATCH;
    const size_t pose_count =
        (size_t)header->bone_count * header->frame_count;
    if (header->frame_count != 0u &&
        pose_count / header->frame_count != header->bone_count)
        return GOLDSRC_STUDIO_BUNDLE_RANGE_INVALID;
    if (!span(bytes, header->parents_offset, header->bone_count,
              sizeof(int16_t), 2u) ||
        !span(bytes, header->poses_offset, pose_count,
              sizeof(GoldSrcStudioPose), 16u) ||
        !span(bytes, header->vertices_offset, header->vertex_count,
              sizeof(GoldSrcStudioSourceVertex), 16u) ||
        !span(bytes, header->indices_offset, header->index_count,
              sizeof(uint16_t), 2u) ||
        !span(bytes, header->draws_offset, header->draw_count,
              sizeof(GoldSrcStudioDraw), 16u) ||
        !span(bytes, header->textures_offset, header->texture_count,
              sizeof(GoldSrcStudioTexture), 16u) ||
        !span(bytes, header->pixels_offset, header->pixels_bytes, 1u, 256u) ||
        (size_t)header->pixels_offset + header->pixels_bytes != bytes)
        return GOLDSRC_STUDIO_BUNDLE_RANGE_INVALID;

    GoldSrcStudioBundleView result = {
        .data = data,
        .bytes = bytes,
        .header = header,
        .parents = (const int16_t *)(base + header->parents_offset),
        .poses = (const GoldSrcStudioPose *)(base + header->poses_offset),
        .vertices = (const GoldSrcStudioSourceVertex *)(
            base + header->vertices_offset),
        .indices = (const uint16_t *)(base + header->indices_offset),
        .draws = (const GoldSrcStudioDraw *)(base + header->draws_offset),
        .textures = (const GoldSrcStudioTexture *)(
            base + header->textures_offset),
        .pixels = base + header->pixels_offset,
        .texture_pixel_bytes = header->pixels_bytes,
    };
    for (uint32_t bone = 0u; bone < header->bone_count; ++bone)
        if (result.parents[bone] < -1 || result.parents[bone] >= (int)bone)
            return GOLDSRC_STUDIO_BUNDLE_CONTENT_INVALID;
    for (size_t pose = 0u; pose < pose_count; ++pose) {
        const GoldSrcStudioPose *item = &result.poses[pose];
        float norm = 0.0f;
        for (unsigned component = 0u; component < 4u; ++component) {
            if (!__builtin_isfinite(item->quaternion[component]))
                return GOLDSRC_STUDIO_BUNDLE_CONTENT_INVALID;
            norm += item->quaternion[component] *
                    item->quaternion[component];
        }
        if (!finite3(item->position) || !(norm > 0.99f && norm < 1.01f))
            return GOLDSRC_STUDIO_BUNDLE_CONTENT_INVALID;
    }
    for (uint32_t vertex = 0u; vertex < header->vertex_count; ++vertex) {
        const GoldSrcStudioSourceVertex *item = &result.vertices[vertex];
        if (!finite3(item->position) || !finite3(item->normal) ||
            !__builtin_isfinite(item->uv[0]) ||
            !__builtin_isfinite(item->uv[1]) ||
            item->bone >= header->bone_count ||
            item->normal_bone >= header->bone_count)
            return GOLDSRC_STUDIO_BUNDLE_CONTENT_INVALID;
    }
    for (uint32_t index = 0u; index < header->index_count; ++index)
        if (result.indices[index] >= header->vertex_count)
            return GOLDSRC_STUDIO_BUNDLE_CONTENT_INVALID;
    for (uint32_t draw = 0u; draw < header->draw_count; ++draw) {
        const GoldSrcStudioDraw *item = &result.draws[draw];
        if (item->first_index > header->index_count ||
            item->index_count == 0u || item->index_count % 3u != 0u ||
            item->index_count > header->index_count - item->first_index ||
            item->texture >= header->texture_count)
            return GOLDSRC_STUDIO_BUNDLE_CONTENT_INVALID;
    }
    for (uint32_t texture = 0u; texture < header->texture_count; ++texture) {
        const GoldSrcStudioTexture *item = &result.textures[texture];
        if (item->width == 0u || item->height == 0u ||
            item->width > 2048u || item->height > 2048u ||
            item->row_pitch < item->width * 4u ||
            item->offset > header->pixels_bytes || item->bytes == 0u ||
            item->bytes > header->pixels_bytes - item->offset ||
            item->bytes != (uint64_t)item->row_pitch * item->height ||
            item->name_hash == 0u)
            return GOLDSRC_STUDIO_BUNDLE_CONTENT_INVALID;
        result.chrome_textures +=
            (item->flags & GOLDSRC_STUDIO_TEXTURE_CHROME) != 0u;
        result.additive_textures +=
            (item->flags & GOLDSRC_STUDIO_TEXTURE_ADDITIVE) != 0u;
        result.masked_textures +=
            (item->flags & GOLDSRC_STUDIO_TEXTURE_MASKED) != 0u;
    }
    *view = result;
    return GOLDSRC_STUDIO_BUNDLE_OK;
}
