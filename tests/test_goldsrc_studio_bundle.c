#include "../src/goldsrc_studio_bundle.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

enum { FILE_BYTES=768, PARENTS=160, POSES=176, VERTICES=240,
       INDICES=348, DRAWS=368, TEXTURES=384, PIXELS=512 };

static uint32_t crc32_bytes(const uint8_t *data, size_t bytes)
{
    uint32_t crc=UINT32_C(0xffffffff);
    for (size_t index=0u; index<bytes; ++index) {
        crc^=data[index];
        for (unsigned bit=0u; bit<8u; ++bit)
            crc=(crc>>1)^(UINT32_C(0xedb88320)&
                         (uint32_t)-(int32_t)(crc&1u));
    }
    return ~crc;
}

static void checksum(uint8_t data[FILE_BYTES])
{
    GoldSrcStudioBundleHeader *header=(void *)data;
    header->payload_crc32=crc32_bytes(data+header->header_bytes,
                                      FILE_BYTES-header->header_bytes);
}

static void make_bundle(uint8_t data[FILE_BYTES])
{
    memset(data,0,FILE_BYTES);
    GoldSrcStudioBundleHeader *header=(void *)data;
    memcpy(header->magic,"PS5MDL\0\0",8u);
    header->version=GOLDSRC_STUDIO_BUNDLE_VERSION;
    header->header_bytes=sizeof(*header);
    header->file_bytes=FILE_BYTES;
    header->source_hash=1u;
    header->model_name_hash=2u;
    header->sequence_name_hash=3u;
    header->bone_count=1u;
    header->frame_count=2u;
    header->fps=1.0f;
    header->sequence_flags=GOLDSRC_STUDIO_SEQUENCE_LOOPING;
    header->vertex_count=3u;
    header->index_count=3u;
    header->draw_count=1u;
    header->texture_count=1u;
    header->bounds_min[0]=header->bounds_min[1]=header->bounds_min[2]=-1.0f;
    header->bounds_max[0]=header->bounds_max[1]=header->bounds_max[2]=1.0f;
    header->parents_offset=PARENTS;
    header->poses_offset=POSES;
    header->vertices_offset=VERTICES;
    header->indices_offset=INDICES;
    header->draws_offset=DRAWS;
    header->textures_offset=TEXTURES;
    header->pixels_offset=PIXELS;
    header->pixels_bytes=256u;
    *(int16_t *)(data+PARENTS)=-1;
    GoldSrcStudioPose *poses=(void *)(data+POSES);
    poses[0].quaternion[3]=1.0f;
    poses[1].position[0]=1.0f;
    poses[1].quaternion[3]=1.0f;
    GoldSrcStudioSourceVertex *vertices=(void *)(data+VERTICES);
    vertices[0].position[0]=-1.0f;
    vertices[1].position[0]=1.0f;
    vertices[2].position[1]=1.0f;
    for (unsigned index=0u; index<3u; ++index)
        vertices[index].normal[2]=1.0f;
    uint16_t *indices=(void *)(data+INDICES);
    indices[0]=0u; indices[1]=1u; indices[2]=2u;
    GoldSrcStudioDraw *draw=(void *)(data+DRAWS);
    draw->index_count=3u;
    draw->flags=GOLDSRC_STUDIO_TEXTURE_CHROME;
    GoldSrcStudioTexture *texture=(void *)(data+TEXTURES);
    texture->bytes=256u;
    texture->width=1u;
    texture->height=1u;
    texture->row_pitch=256u;
    texture->flags=GOLDSRC_STUDIO_TEXTURE_CHROME;
    texture->name_hash=4u;
    memset(data+PIXELS,0xff,256u);
    checksum(data);
}

int main(void)
{
    _Alignas(256) uint8_t data[FILE_BYTES];
    make_bundle(data);
    GoldSrcStudioBundleView view;
    assert(goldsrc_studio_bundle_open(data,sizeof(data),&view)==
           GOLDSRC_STUDIO_BUNDLE_OK);
    assert(view.header->bone_count==1u && view.header->frame_count==2u);
    assert(view.header->vertex_count==3u && view.header->index_count==3u);
    assert(view.chrome_textures==1u && view.additive_textures==0u);
    data[PIXELS]^=1u;
    assert(goldsrc_studio_bundle_open(data,sizeof(data),&view)==
           GOLDSRC_STUDIO_BUNDLE_CHECKSUM_MISMATCH);
    make_bundle(data);
    ((GoldSrcStudioBundleHeader *)data)->reserved[0]=1u;
    assert(goldsrc_studio_bundle_open(data,sizeof(data),&view)==
           GOLDSRC_STUDIO_BUNDLE_HEADER_INVALID);
    make_bundle(data);
    ((uint16_t *)(data+INDICES))[2]=3u;
    checksum(data);
    assert(goldsrc_studio_bundle_open(data,sizeof(data),&view)==
           GOLDSRC_STUDIO_BUNDLE_CONTENT_INVALID);
    assert(goldsrc_studio_bundle_open(0,sizeof(data),&view)==
           GOLDSRC_STUDIO_BUNDLE_PRECONDITION);
    return 0;
}
