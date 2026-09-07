#include "../src/bsp_bundle.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

enum { HEADER = 160, VERTEX_OFFSET = 160, INDEX_OFFSET = 256,
       DRAW_OFFSET = 272, FILE_BYTES = 304,
       LIGHT_HEADER = 288, LIGHT_VERTEX_OFFSET = 288,
       LIGHT_INDEX_OFFSET = 384, LIGHT_DRAW_OFFSET = 400,
       LIGHT_IMAGE_OFFSET = 432, LIGHT_PIXELS_OFFSET = 512,
       TEXTURE_METADATA_OFFSET = 768, TEXTURE_PIXELS_OFFSET = 1024,
       LIGHT_FILE_BYTES = 1280,
       STYLED_HEADER = 352, STYLED_VERTEX_OFFSET = 352,
       STYLED_INDEX_OFFSET = 448, STYLED_DRAW_OFFSET = 464,
       STYLED_IMAGE_OFFSET = 496, STYLED_PIXELS_OFFSET = 512,
       STYLED_FACE_OFFSET = 768, STYLED_SAMPLES_OFFSET = 800,
       STYLED_TEXTURE_OFFSET = 816, STYLED_TEXTURE_PIXELS_OFFSET = 1024,
       STYLED_FILE_BYTES = 1280 };

static void put_u32(uint8_t *at, uint32_t value)
{
    memcpy(at, &value, sizeof(value));
}

static void put_u16(uint8_t *at, uint16_t value)
{
    memcpy(at, &value, sizeof(value));
}

static void put_f32(uint8_t *at, float value)
{
    memcpy(at, &value, sizeof(value));
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

static void descriptor(uint8_t *data, unsigned index, const char tag[4],
                       uint32_t offset, uint32_t bytes, uint32_t count,
                       uint32_t stride)
{
    uint8_t *const row = data + 64u + index * 32u;
    memcpy(row, tag, 4u);
    put_u32(row + 4u, offset);
    put_u32(row + 8u, bytes);
    put_u32(row + 12u, count);
    put_u32(row + 16u, stride);
    put_u32(row + 20u, crc32_bytes(data + offset, bytes));
}

static void checksums(uint8_t *data)
{
    descriptor(data, 0u, "VERT", VERTEX_OFFSET, 96u, 3u, 32u);
    descriptor(data, 1u, "INDX", INDEX_OFFSET, 6u, 3u, 2u);
    descriptor(data, 2u, "DRAW", DRAW_OFFSET, 32u, 1u, 32u);
    put_u32(data + 20u, crc32_bytes(data + HEADER, FILE_BYTES - HEADER));
}

static void make_bundle(uint8_t data[FILE_BYTES])
{
    memset(data, 0, FILE_BYTES);
    memcpy(data, "PS5BSP\0\0", 8u);
    put_u32(data + 8u, 3u);
    put_u32(data + 12u, HEADER);
    put_u32(data + 16u, FILE_BYTES);
    put_f32(data + 24u, 1.0f);
    put_f32(data + 28u, 2.0f);
    put_f32(data + 32u, 3.0f);
    put_f32(data + 36u, 0.0f);
    put_f32(data + 40u, 0.0f);
    put_f32(data + 44u, -1.0f);
    put_u32(data + 48u, 3u);
    BspBundleVertex *const vertices = (BspBundleVertex *)(data + VERTEX_OFFSET);
    vertices[0].position[0] = 0.0f;
    vertices[1].position[0] = 1.0f;
    vertices[2].position[1] = 1.0f;
    put_u16(data + INDEX_OFFSET, 0u);
    put_u16(data + INDEX_OFFSET + 2u, 1u);
    put_u16(data + INDEX_OFFSET + 4u, 2u);
    BspBundleDraw *const draw = (BspBundleDraw *)(data + DRAW_OFFSET);
    draw->index_count = 3u;
    draw->lightmap = UINT32_MAX;
    checksums(data);
}

static void light_checksums(uint8_t *data)
{
    descriptor(data, 0u, "VERT", LIGHT_VERTEX_OFFSET, 96u, 3u, 32u);
    descriptor(data, 1u, "INDX", LIGHT_INDEX_OFFSET, 6u, 3u, 2u);
    descriptor(data, 2u, "DRAW", LIGHT_DRAW_OFFSET, 32u, 1u, 32u);
    descriptor(data, 3u, "LMHD", LIGHT_IMAGE_OFFSET, 16u, 1u, 16u);
    descriptor(data, 4u, "LMPX", LIGHT_PIXELS_OFFSET, 256u, 64u, 4u);
    descriptor(data, 5u, "TEXM", TEXTURE_METADATA_OFFSET, 48u, 1u, 48u);
    descriptor(data, 6u, "TEXP", TEXTURE_PIXELS_OFFSET, 256u, 256u, 1u);
    put_u32(data + 20u, crc32_bytes(data + LIGHT_HEADER,
                                    LIGHT_FILE_BYTES - LIGHT_HEADER));
}

static void make_light_bundle(uint8_t data[LIGHT_FILE_BYTES])
{
    memset(data, 0, LIGHT_FILE_BYTES);
    memcpy(data, "PS5BSP\0\0", 8u);
    put_u32(data + 8u, 3u);
    put_u32(data + 12u, LIGHT_HEADER);
    put_u32(data + 16u, LIGHT_FILE_BYTES);
    put_f32(data + 36u, 0.0f);
    put_f32(data + 40u, 0.0f);
    put_f32(data + 44u, -1.0f);
    put_u32(data + 48u, 7u);
    BspBundleVertex *const vertices =
        (BspBundleVertex *)(data + LIGHT_VERTEX_OFFSET);
    vertices[0].light_uv[0] = vertices[0].light_uv[1] = 0.5f;
    vertices[1].position[0] = 1.0f;
    vertices[1].light_uv[0] = vertices[1].light_uv[1] = 0.5f;
    vertices[2].position[1] = 1.0f;
    vertices[2].light_uv[0] = vertices[2].light_uv[1] = 0.5f;
    put_u16(data + LIGHT_INDEX_OFFSET, 0u);
    put_u16(data + LIGHT_INDEX_OFFSET + 2u, 1u);
    put_u16(data + LIGHT_INDEX_OFFSET + 4u, 2u);
    BspBundleDraw *const draw = (BspBundleDraw *)(data + LIGHT_DRAW_OFFSET);
    draw->index_count = 3u;
    draw->lightmap = 0u;
    BspBundleImage *const image =
        (BspBundleImage *)(data + LIGHT_IMAGE_OFFSET);
    image->width = 64u;
    image->height = 1u;
    image->row_pitch = 256u;
    image->format = BSP_BUNDLE_IMAGE_RGBA8_UNORM;
    memset(data + LIGHT_PIXELS_OFFSET, 0xff, 256u);
    BspBundleTexture *const texture =
        (BspBundleTexture *)(data + TEXTURE_METADATA_OFFSET);
    texture->offset = 0u;
    texture->bytes = 256u;
    texture->width = 1u;
    texture->height = 1u;
    texture->row_pitch = 256u;
    texture->format = BSP_BUNDLE_IMAGE_RGBA8_UNORM;
    texture->name_hash = UINT32_C(0x12345678);
    texture->flags = BSP_BUNDLE_TEXTURE_TRANSPARENT;
    texture->mip_count = 1u;
    memset(data + TEXTURE_PIXELS_OFFSET, 0xaa, 256u);
    light_checksums(data);
}

static void styled_checksums(uint8_t *data)
{
    descriptor(data, 0u, "VERT", STYLED_VERTEX_OFFSET, 96u, 3u, 32u);
    descriptor(data, 1u, "INDX", STYLED_INDEX_OFFSET, 6u, 3u, 2u);
    descriptor(data, 2u, "DRAW", STYLED_DRAW_OFFSET, 32u, 1u, 32u);
    descriptor(data, 3u, "LMHD", STYLED_IMAGE_OFFSET, 16u, 1u, 16u);
    descriptor(data, 4u, "LMPX", STYLED_PIXELS_OFFSET, 256u, 64u, 4u);
    descriptor(data, 5u, "LMFM", STYLED_FACE_OFFSET, 32u, 1u, 32u);
    descriptor(data, 6u, "LMSP", STYLED_SAMPLES_OFFSET, 3u, 3u, 1u);
    descriptor(data, 7u, "TEXM", STYLED_TEXTURE_OFFSET, 48u, 1u, 48u);
    descriptor(data, 8u, "TEXP", STYLED_TEXTURE_PIXELS_OFFSET,
               256u, 256u, 1u);
    put_u32(data + 20u, crc32_bytes(data + STYLED_HEADER,
                                    STYLED_FILE_BYTES - STYLED_HEADER));
}

static void make_styled_bundle(uint8_t data[STYLED_FILE_BYTES])
{
    memset(data, 0, STYLED_FILE_BYTES);
    memcpy(data, "PS5BSP\0\0", 8u);
    put_u32(data + 8u, 3u);
    put_u32(data + 12u, STYLED_HEADER);
    put_u32(data + 16u, STYLED_FILE_BYTES);
    put_f32(data + 44u, -1.0f);
    put_u32(data + 48u, 9u);
    BspBundleVertex *const vertices =
        (BspBundleVertex *)(data + STYLED_VERTEX_OFFSET);
    vertices[1].position[0] = 1.0f;
    vertices[2].position[1] = 1.0f;
    put_u16(data + STYLED_INDEX_OFFSET, 0u);
    put_u16(data + STYLED_INDEX_OFFSET + 2u, 1u);
    put_u16(data + STYLED_INDEX_OFFSET + 4u, 2u);
    BspBundleDraw *const draw =
        (BspBundleDraw *)(data + STYLED_DRAW_OFFSET);
    draw->index_count = 3u;
    draw->lightmap = 0u;
    draw->face_id = 7u;
    BspBundleImage *const image =
        (BspBundleImage *)(data + STYLED_IMAGE_OFFSET);
    image->width = 64u;
    image->height = 1u;
    image->row_pitch = 256u;
    image->format = BSP_BUNDLE_IMAGE_RGBA8_UNORM;
    memset(data + STYLED_PIXELS_OFFSET, 0xff, 256u);
    BspBundleLightmapFace *const face =
        (BspBundleLightmapFace *)(data + STYLED_FACE_OFFSET);
    face->face_id = 7u;
    face->width = 1u;
    face->height = 1u;
    face->sample_bytes = 3u;
    face->styles[0] = 0u;
    face->styles[1] = face->styles[2] = face->styles[3] = 255u;
    data[STYLED_SAMPLES_OFFSET] = 1u;
    data[STYLED_SAMPLES_OFFSET + 1u] = 2u;
    data[STYLED_SAMPLES_OFFSET + 2u] = 3u;
    BspBundleTexture *const texture =
        (BspBundleTexture *)(data + STYLED_TEXTURE_OFFSET);
    texture->bytes = 256u;
    texture->width = 1u;
    texture->height = 1u;
    texture->row_pitch = 256u;
    texture->format = BSP_BUNDLE_IMAGE_RGBA8_UNORM;
    texture->name_hash = 1u;
    texture->mip_count = 1u;
    styled_checksums(data);
}

int main(void)
{
    const BspBundleTexture mip_texture = {
        .bytes = 32512u, .width = 64u, .height = 64u,
        .row_pitch = 256u, .format = BSP_BUNDLE_IMAGE_RGBA8_UNORM,
        .name_hash = 1u, .mip_count = 7u,
    };
    BspBundleMipLevel derived;
    assert(bsp_bundle_texture_mip_level(&mip_texture, 6u, &derived) == 0);
    assert(derived.offset == 0u && derived.bytes == 256u &&
           derived.width == 1u && derived.height == 1u);
    assert(bsp_bundle_texture_mip_level(&mip_texture, 0u, &derived) == 0);
    assert(derived.offset == 16128u && derived.bytes == 16384u &&
           derived.width == 64u && derived.height == 64u);
    assert(bsp_bundle_texture_mip_level(&mip_texture, 7u, &derived) != 0);

    union { uint64_t align; uint8_t bytes[FILE_BYTES]; } storage;
    make_bundle(storage.bytes);
    BspBundleView view;
    assert(bsp_bundle_open(storage.bytes, sizeof(storage.bytes), &view) ==
           BSP_BUNDLE_OK);
    assert(view.vertex_count == 3u && view.index_count == 3u &&
           view.draw_count == 1u && view.camera_position[1] == 2.0f &&
           view.camera_forward[2] == -1.0f);

    uint8_t saved = storage.bytes[VERTEX_OFFSET];
    storage.bytes[VERTEX_OFFSET] ^= 1u;
    assert(bsp_bundle_open(storage.bytes, sizeof(storage.bytes), &view) ==
           BSP_BUNDLE_CHECKSUM_MISMATCH);
    storage.bytes[VERTEX_OFFSET] = saved;
    checksums(storage.bytes);

    uint16_t *const indices = (uint16_t *)(storage.bytes + INDEX_OFFSET);
    indices[2] = 3u;
    checksums(storage.bytes);
    assert(bsp_bundle_open(storage.bytes, sizeof(storage.bytes), &view) ==
           BSP_BUNDLE_GEOMETRY_INVALID);
    indices[2] = 2u;
    checksums(storage.bytes);

    put_u32(storage.bytes + 64u + 24u, 1u);
    assert(bsp_bundle_open(storage.bytes, sizeof(storage.bytes), &view) ==
           BSP_BUNDLE_DIRECTORY_INVALID);
    put_u32(storage.bytes + 64u + 24u, 0u);
    checksums(storage.bytes);
    assert(bsp_bundle_open(storage.bytes, sizeof(storage.bytes) - 1u, &view) ==
           BSP_BUNDLE_HEADER_INVALID);

    union { uint64_t align; uint8_t bytes[LIGHT_FILE_BYTES]; } light;
    make_light_bundle(light.bytes);
    assert(bsp_bundle_open(light.bytes, sizeof(light.bytes), &view) ==
           BSP_BUNDLE_OK);
    assert(view.lightmap_image && view.lightmap_pixels &&
           view.lightmap_image->width == 64u &&
           view.lightmap_pixel_count == 64u && view.textures &&
           view.texture_count == 1u && view.texture_pixel_bytes == 256u &&
           view.textures[0].width == 1u);
    BspBundleMipLevel mip;
    assert(bsp_bundle_texture_mip_level(&view.textures[0], 0u, &mip) == 0);
    assert(mip.offset == 0u && mip.bytes == 256u && mip.width == 1u &&
           mip.height == 1u && mip.row_pitch == 256u);
    BspBundleTexture *const texture =
        (BspBundleTexture *)(light.bytes + TEXTURE_METADATA_OFFSET);
    texture->flags = BSP_BUNDLE_TEXTURE_NODRAW;
    light_checksums(light.bytes);
    assert(bsp_bundle_open(light.bytes, sizeof(light.bytes), &view) ==
           BSP_BUNDLE_GEOMETRY_INVALID);
    texture->flags = BSP_BUNDLE_TEXTURE_SKY;
    light_checksums(light.bytes);
    assert(bsp_bundle_open(light.bytes, sizeof(light.bytes), &view) ==
           BSP_BUNDLE_OK);
    texture->flags = BSP_BUNDLE_TEXTURE_SKY |
                     BSP_BUNDLE_TEXTURE_TRANSPARENT;
    light_checksums(light.bytes);
    assert(bsp_bundle_open(light.bytes, sizeof(light.bytes), &view) ==
           BSP_BUNDLE_GEOMETRY_INVALID);
    texture->flags = BSP_BUNDLE_TEXTURE_TRANSPARENT;
    BspBundleImage *const image =
        (BspBundleImage *)(light.bytes + LIGHT_IMAGE_OFFSET);
    image->row_pitch = 252u;
    light_checksums(light.bytes);
    assert(bsp_bundle_open(light.bytes, sizeof(light.bytes), &view) ==
           BSP_BUNDLE_GEOMETRY_INVALID);
    image->row_pitch = 256u;
    texture->reserved[0] = 1u;
    light_checksums(light.bytes);
    assert(bsp_bundle_open(light.bytes, sizeof(light.bytes), &view) ==
           BSP_BUNDLE_GEOMETRY_INVALID);
    texture->reserved[0] = 0u;
    texture->offset = 1u;
    light_checksums(light.bytes);
    assert(bsp_bundle_open(light.bytes, sizeof(light.bytes), &view) ==
           BSP_BUNDLE_GEOMETRY_INVALID);

    union { uint64_t align; uint8_t bytes[STYLED_FILE_BYTES]; } styled;
    make_styled_bundle(styled.bytes);
    assert(bsp_bundle_open(styled.bytes, sizeof(styled.bytes), &view) ==
           BSP_BUNDLE_OK);
    assert(view.lightmap_faces && view.lightmap_face_count == 1u &&
           view.lightmap_samples && view.lightmap_sample_bytes == 3u &&
           view.lightmap_faces[0].face_id == 7u &&
           view.lightmap_faces[0].styles[0] == 0u &&
           view.lightmap_samples[2] == 3u);
    BspBundleLightmapFace *const styled_face =
        (BspBundleLightmapFace *)(styled.bytes + STYLED_FACE_OFFSET);
    styled_face->styles[2] = 32u;
    styled_checksums(styled.bytes);
    assert(bsp_bundle_open(styled.bytes, sizeof(styled.bytes), &view) ==
           BSP_BUNDLE_GEOMETRY_INVALID);
    styled_face->styles[2] = 255u;
    styled_face->sample_bytes = 6u;
    styled_checksums(styled.bytes);
    assert(bsp_bundle_open(styled.bytes, sizeof(styled.bytes), &view) ==
           BSP_BUNDLE_GEOMETRY_INVALID);
    return 0;
}
