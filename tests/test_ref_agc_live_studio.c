#include "ref_agc_live_studio.h"
#include "bsp_bundle.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
static void p32(uint8_t *p,size_t o,int32_t v){memcpy(p+o,&v,4);}
static void p16(uint8_t *p,size_t o,int16_t v){memcpy(p+o,&v,2);}
static void flush(const void *p,size_t n,void *u){(void)u;assert(p&&n);}
int main(void)
{
    static RefAgcLiveFrame live;
    static RefAgcGpuTextureCache textures;
    static RefAgcGpuStudioCache models;
    _Alignas(256) uint8_t arena[1024]={0}, pixels[256]={0};
    uint8_t data[1024]={0};
    memcpy(data,"IDST",4);p32(data,4,10);p32(data,72,1024);
    p32(data,140,1);p32(data,180,1);p32(data,184,244);
    p32(data,192,1);p32(data,196,1);p32(data,200,324);
    p32(data,204,1);p32(data,208,328);
    p32(data,312,1);p32(data,316,1);p32(data,320,1);
    p32(data,392,1);p32(data,396,1);p32(data,400,404);
    p32(data,476,1);p32(data,480,516);p32(data,484,4);
    p32(data,488,536);p32(data,492,540);p32(data,496,1);
    p32(data,500,652);p32(data,504,640);
    float normal_x=1.0f;memcpy(data+640,&normal_x,4);
    p32(data,516,2);p32(data,520,588);p16(data,588,4);
    for(int i=0;i<4;++i)p16(data,590+i*8,i);
    assert(!ref_agc_gpu_studio_cache_init(&models,arena,sizeof(arena),flush,NULL));
    assert(!ref_agc_gpu_studio_cache_apply(&models,&(RefAgcStudioView){1,1,1,"models/test.mdl",data,sizeof(data),1},1));
    assert(!ref_agc_gpu_texture_cache_init(&textures,pixels,(uintptr_t)pixels,sizeof(pixels),flush,NULL));
    textures.entries[0].active=1; /* synthetic texture, no GPU submission */
    live.entity_count=1;live.studio_pose_count=1;
    live.entities[0]=(RefAgcLiveEntity){.index=42,.model_type=3,.studio_handle=1,.studio_pose=1};
    live.studio_poses[0].bones=1;
    for(int k=0;k<3;++k){live.studio_poses[0].matrices[0][k][k]=1;live.studio_poses[0].matrices[0][k][3]=10*(k+1);}
    uint8_t *memory=aligned_alloc(256,16384);assert(memory);
    Ps5TransientRing ring;
    assert(!ps5_transient_ring_init(&ring,memory,16384,1,256));
    assert(!ps5_transient_ring_begin(&ring,0,0,0));
    const float camera[3]={0},forward[3]={0,0,-1};
    RefAgcLiveStudioFrame out;
#define BUILD() ref_agc_live_studio_build(&out,&live,&models,&textures,&ring,0,memory,16384,camera,forward,1)
    assert(!BUILD());assert(out.entities==1&&out.count==1&&out.vertices==4&&out.indices==6);
    const uint32_t *w=out.draws[0].vertices;
    const BspBundleVertex *v=(void *)(uintptr_t)((uint64_t)w[0]|((uint64_t)(w[1]&65535)<<32));
    assert(v[0].position[0]==10&&v[0].position[1]==30&&v[0].position[2]==-20);
    const uint16_t strip[]={0,1,2,2,1,3};assert(!memcmp(out.draws[0].indices,strip,sizeof(strip)));
    uint64_t h=out.pose_hash;live.studio_poses[0].matrices[0][0][3]=11;ring.slots[0].used=0;
    uint64_t nh=out.normal_hash;assert(out.normals==4);
    assert(!BUILD());assert(h!=out.pose_hash&&nh==out.normal_hash);
    p32(arena,140,2);live.studio_poses[0].bones=2;arena[652]=1;
    live.studio_poses[0].matrices[1][1][0]=1;
    ring.slots[0].used=0;assert(!BUILD()&&out.normal_hash!=nh);
    w=out.draws[0].vertices;
    v=(void *)(uintptr_t)((uint64_t)w[0]|((uint64_t)(w[1]&65535)<<32));
    assert(v[0].position[0]==11); /* vertices still use bone zero */
    p32(arena,140,1);live.studio_poses[0].bones=1;arena[652]=0;
    /* Normal bone ownership is independent of the vertex bone table. */
    arena[652]=1;size_t before=ring.slots[0].used;
    assert(BUILD()<0&&!out.normals&&!out.normal_hash&&ring.slots[0].used==before);
    arena[652]=0;
    float nonfinite=NAN;memcpy(arena+640,&nonfinite,4);
    assert(BUILD()<0&&ring.slots[0].used==before);
    memcpy(arena+640,&normal_x,4);
    p32(arena,504,1020);assert(BUILD()<0&&ring.slots[0].used==before);p32(arena,504,640);
    p32(arena,500,-1);assert(BUILD()<0&&ring.slots[0].used==before);p32(arena,500,652);
    live.studio_poses[0].matrices[0][0][0]=0;
    live.studio_poses[0].matrices[0][1][0]=1;
    ring.slots[0].used=0;assert(!BUILD()&&out.normal_hash!=nh);
    live.studio_poses[0].matrices[0][0][0]=1;
    live.studio_poses[0].matrices[0][1][0]=0;
    size_t checkpoint=ring.slots[0].used;live.entities[0].studio_pose=0;
    assert(BUILD()<0);assert(out.failed_entity==42&&!out.count&&ring.slots[0].used==checkpoint);
    live.entities[0].studio_pose=1;arena[536]=1;
    assert(BUILD()<0);assert(ring.slots[0].used==checkpoint);
    arena[536]=0;p16(arena,588,-4);ring.slots[0].used=0;
    assert(!BUILD());const uint16_t fan[]={0,1,2,0,2,3};assert(!memcmp(out.draws[0].indices,fan,sizeof(fan)));
    p32(arena,520,1024);checkpoint=ring.slots[0].used;
    assert(BUILD()<0);assert(ring.slots[0].used==checkpoint);
    free(memory);puts("ref_agc live Studio geometry tests passed");
}
