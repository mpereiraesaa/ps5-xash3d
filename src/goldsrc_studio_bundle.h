#ifndef PS5_XASH3D_GOLDSRC_STUDIO_BUNDLE_H
#define PS5_XASH3D_GOLDSRC_STUDIO_BUNDLE_H

#include <stddef.h>
#include <stdint.h>

enum {
    GOLDSRC_STUDIO_BUNDLE_VERSION = 1,
    GOLDSRC_STUDIO_MAX_BONES = 128,
    GOLDSRC_STUDIO_MAX_FRAMES = 4096,
    GOLDSRC_STUDIO_TEXTURE_CHROME = 0x0002u,
    GOLDSRC_STUDIO_TEXTURE_ADDITIVE = 0x0020u,
    GOLDSRC_STUDIO_TEXTURE_MASKED = 0x0040u,
    GOLDSRC_STUDIO_SEQUENCE_LOOPING = 0x0001u,
};

typedef struct GoldSrcStudioBundleHeader {
    uint8_t magic[8];
    uint32_t version;
    uint32_t header_bytes;
    uint32_t file_bytes;
    uint32_t payload_crc32;
    uint64_t source_hash;
    uint64_t model_name_hash;
    uint64_t sequence_name_hash;
    uint32_t bone_count;
    uint32_t frame_count;
    float fps;
    uint32_t sequence_flags;
    uint32_t vertex_count;
    uint32_t index_count;
    uint32_t draw_count;
    uint32_t texture_count;
    float bounds_min[3];
    float bounds_max[3];
    uint32_t parents_offset;
    uint32_t poses_offset;
    uint32_t vertices_offset;
    uint32_t indices_offset;
    uint32_t draws_offset;
    uint32_t textures_offset;
    uint32_t pixels_offset;
    uint32_t pixels_bytes;
    uint32_t reserved[6];
} GoldSrcStudioBundleHeader;

typedef struct GoldSrcStudioPose {
    float position[3];
    float quaternion[4];
} GoldSrcStudioPose;

typedef struct GoldSrcStudioSourceVertex {
    float position[3];
    float normal[3];
    float uv[2];
    uint16_t bone;
    uint16_t normal_bone;
} GoldSrcStudioSourceVertex;

typedef struct GoldSrcStudioDraw {
    uint32_t first_index;
    uint32_t index_count;
    uint32_t texture;
    uint32_t flags;
} GoldSrcStudioDraw;

typedef struct GoldSrcStudioTexture {
    uint32_t offset;
    uint32_t bytes;
    uint32_t width;
    uint32_t height;
    uint32_t row_pitch;
    uint32_t flags;
    uint64_t name_hash;
} GoldSrcStudioTexture;

typedef struct GoldSrcStudioBundleView {
    const void *data;
    size_t bytes;
    const GoldSrcStudioBundleHeader *header;
    const int16_t *parents;
    const GoldSrcStudioPose *poses;
    const GoldSrcStudioSourceVertex *vertices;
    const uint16_t *indices;
    const GoldSrcStudioDraw *draws;
    const GoldSrcStudioTexture *textures;
    const uint8_t *pixels;
    uint64_t texture_pixel_bytes;
    uint32_t chrome_textures;
    uint32_t additive_textures;
    uint32_t masked_textures;
} GoldSrcStudioBundleView;

enum goldsrc_studio_bundle_result {
    GOLDSRC_STUDIO_BUNDLE_OK = 0,
    GOLDSRC_STUDIO_BUNDLE_PRECONDITION = -1,
    GOLDSRC_STUDIO_BUNDLE_HEADER_INVALID = -2,
    GOLDSRC_STUDIO_BUNDLE_CHECKSUM_MISMATCH = -3,
    GOLDSRC_STUDIO_BUNDLE_RANGE_INVALID = -4,
    GOLDSRC_STUDIO_BUNDLE_CONTENT_INVALID = -5,
};

int goldsrc_studio_bundle_open(const void *data, size_t bytes,
                               GoldSrcStudioBundleView *view);

#endif
