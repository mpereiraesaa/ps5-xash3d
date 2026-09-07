#include "../src/goldsrc_studio_model.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static int set_sh(uint32_t **cursor, uint32_t capacity, uint32_t offset,
                  const uint32_t *values, uint32_t count)
{
    assert(cursor && values && capacity>=count+2u);
    assert(offset==0x8du || offset==0x0du);
    *cursor+=count+2u;
    return 0;
}

static int draw(uint32_t **cursor, uint32_t capacity, uint32_t count,
                const uint16_t *indices, const void *mapping,
                size_t mapping_bytes, uint64_t modifier)
{
    assert(cursor && capacity>=6u && count==3u && indices && mapping);
    assert(mapping_bytes==262144u && modifier==UINT64_C(0x1234));
    *cursor+=6u;
    return 0;
}

int main(void)
{
    _Alignas(256) uint8_t memory[262144];
    memset(memory,0,sizeof(memory));
    GoldSrcStudioBundleHeader header={0};
    header.bone_count=1u;
    header.frame_count=3u;
    header.fps=1.0f;
    header.sequence_flags=GOLDSRC_STUDIO_SEQUENCE_LOOPING;
    header.vertex_count=3u;
    header.index_count=3u;
    header.draw_count=1u;
    header.texture_count=1u;
    header.bounds_min[0]=header.bounds_min[1]=header.bounds_min[2]=-1.0f;
    header.bounds_max[0]=header.bounds_max[1]=header.bounds_max[2]=1.0f;
    int16_t parents[1]={-1};
    GoldSrcStudioPose poses[3]={0};
    for (unsigned frame=0u; frame<3u; ++frame) {
        poses[frame].position[0]=(float)frame;
        poses[frame].quaternion[3]=1.0f;
    }
    GoldSrcStudioSourceVertex source[3]={0};
    source[0].position[0]=-1.0f;
    source[1].position[0]=1.0f;
    source[2].position[1]=1.0f;
    for (unsigned vertex=0u; vertex<3u; ++vertex)
        source[vertex].normal[2]=1.0f;
    uint16_t source_indices[3]={0u,1u,2u};
    GoldSrcStudioDraw source_draw={0u,3u,0u,0u};
    GoldSrcStudioTexture texture={0u,256u,1u,1u,256u,0u,1u};
    memset(memory,0xff,256u);
    GoldSrcStudioBundleView bundle={
        .header=&header, .parents=parents, .poses=poses,
        .vertices=source, .indices=source_indices, .draws=&source_draw,
        .textures=&texture, .pixels=memory, .texture_pixel_bytes=256u,
    };
    Ps5TransientRing ring;
    assert(ps5_transient_ring_init(&ring,memory+4096u,
                                   sizeof(memory)-4096u,2u,256u)==
           PS5_TRANSIENT_OK);
    const float camera[3]={0.0f,0.0f,0.0f};
    const float forward[3]={0.0f,0.0f,-1.0f};
    assert(ps5_transient_ring_begin(&ring,0u,0u,0)==PS5_TRANSIENT_OK);
    GoldSrcStudioFrame first;
    assert(goldsrc_studio_frame_build(
        &first,&bundle,&ring,0u,memory,sizeof(memory),camera,forward,
        16.0f/9.0f,0u)==0);
    assert(first.mode==GOLDSRC_STUDIO_MODE_CONTROL);
    assert(first.vertices_per_instance==3u && first.indices_per_instance==3u);
    assert(first.draw_count==1u && first.texture_count==1u);
    assert(first.frame0==0u && first.frame1==1u && first.blend==0.0f);
    assert(first.pose_hash && first.skinned_hash && first.transient_bytes>0u);
    uint32_t commands[32]={0};
    uint32_t *cursor=commands;
    GoldSrcStudioComposeResult composed={0};
    assert(goldsrc_studio_compose_instance(
        &cursor,commands+32,&first,&bundle,GOLDSRC_STUDIO_INSTANCE_TEXTURED,
        memory,sizeof(memory),UINT64_C(0x1234),set_sh,draw,&composed)==0);
    assert(composed.draws==1u && composed.indices==3u &&
           composed.textures==1u && composed.command_dwords==13u &&
           cursor==commands+13u);
    assert(ps5_transient_ring_abort_unsubmitted(&ring,0u)==PS5_TRANSIENT_OK);

    assert(ps5_transient_ring_begin(&ring,1u,0u,0)==PS5_TRANSIENT_OK);
    GoldSrcStudioFrame second;
    assert(goldsrc_studio_frame_build(
        &second,&bundle,&ring,1u,memory,sizeof(memory),camera,forward,
        16.0f/9.0f,30u)==0);
    assert(second.frame0==0u && second.frame1==1u && second.blend==0.5f);
    assert(second.pose_hash!=first.pose_hash &&
           second.skinned_hash!=first.skinned_hash);
    assert(second.transient_bytes==first.transient_bytes);
    assert(ps5_transient_ring_abort_unsubmitted(&ring,1u)==PS5_TRANSIENT_OK);

    assert(goldsrc_studio_mode(600u)==GOLDSRC_STUDIO_MODE_TEXTURED);
    assert(goldsrc_studio_mode(1200u)==GOLDSRC_STUDIO_MODE_CHROME);
    assert(goldsrc_studio_mode(1800u)==GOLDSRC_STUDIO_MODE_ADDITIVE);
    assert(goldsrc_studio_mode(2400u)==GOLDSRC_STUDIO_MODE_COMBINED);
    assert(goldsrc_studio_mode(3000u)==GOLDSRC_STUDIO_MODE_CONTROL);
    assert(strcmp(goldsrc_studio_mode_name(GOLDSRC_STUDIO_MODE_COMBINED),
                  "combined")==0);
    assert(strcmp(goldsrc_studio_mode_name(GOLDSRC_STUDIO_MODE_COUNT),
                  "invalid")==0);
    return 0;
}
