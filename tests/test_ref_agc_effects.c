#include "ref_agc_live_sprite.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void flush(const void *p,size_t n,void *u){(void)u;assert(p&&n);}
static const void *address(const uint32_t *w)
{return (void *)(uintptr_t)((uint64_t)w[0]|((uint64_t)(w[1]&65535)<<32));}
int main(void)
{
    const float face[4][3]={{-10,-10,0},{10,-10,0},{10,10,0},{-10,10,0}};
    float center[3]={0},normal[3]={0,0,1};BspBundleVertex v[32];
    int count=ref_agc_decal_clip(face,4,center,normal,4,4,v);assert(count==4);
    for(int i=0;i<count;++i) {
        assert(fabsf(v[i].position[0])<=2.001f && fabsf(v[i].position[1])<=2.001f);
        assert(fabsf(v[i].position[2]-.03f)<.0001f);
        for(int k=0;k<2;++k)assert(v[i].base_uv[k]>=0 && v[i].base_uv[k]<=1);
    }
    center[0]=10;count=ref_agc_decal_clip(face,4,center,normal,4,4,v);assert(count==4);
    for(int i=0;i<count;++i)assert(v[i].position[0]>=7.999f && v[i].position[0]<=10.001f);
    center[0]=30;assert(ref_agc_decal_clip(face,4,center,normal,4,4,v)==0);
    center[0]=NAN;assert(ref_agc_decal_clip(face,4,center,normal,4,4,v)<0);center[0]=0;
    assert(ref_agc_decal_clip(face,33,center,normal,4,4,v)<0);
    assert(ref_agc_decal_clip(face,4,center,normal,0,4,v)<0);
    normal[2]=-1;assert(ref_agc_decal_clip(face,4,center,normal,4,4,v)==4);
    assert(v[0].position[2]<0);normal[2]=1;
    assert(ref_agc_decal_clip(face,4,center,normal,4,4,v)==4);
    static RefAgcEffects fx;
    static RefAgcLiveEffectFrame out;
    assert(!ref_agc_effect_append(&fx,v,4,1,2,1));
    v[0].face_id=0xff0000ff;assert(!ref_agc_effect_append(&fx,v,4,1,2,1));
    assert(fx.count==2 && fx.vertices_count==8);
    v[0].position[0]=NAN;assert(ref_agc_effect_append(&fx,v,4,1,2,1)<0 && fx.count==2);
    static RefAgcGpuTextureCache textures;_Alignas(256) unsigned char pixels[256]={0};
    assert(!ref_agc_gpu_texture_cache_init(&textures,pixels,(uintptr_t)pixels,sizeof(pixels),flush,NULL));
    textures.entries[0].active=1;
    unsigned char *memory=aligned_alloc(256,16384);assert(memory);Ps5TransientRing ring;
    assert(!ps5_transient_ring_init(&ring,memory,16384,1,256));assert(!ps5_transient_ring_begin(&ring,0,0,0));
    const float camera[3]={0},forward[3]={0,0,-1};
#define BUILD() ref_agc_live_effect_build(&out,&fx,&textures,&ring,0,memory,16384,camera,forward,1)
    assert(!BUILD() && out.count==1 && out.draws[0].count==12); /* batch same texture/state */
    const BspBundleVertex *gpu=address(out.draws[0].vertices);
    assert(gpu[4].face_id==0xff0000ff && gpu[0].position[1]==.03f);
    const BspResourceConstants *c=address(out.draws[0].constants);assert(c->debug_values[11]==-1);
    assert(out.draws[0].indices[6]==4 && out.draws[0].indices[11]==7);
    ring.slots[0].used=0;fx.polys[1].alpha=.5f;assert(!BUILD() && out.count==2);
    size_t saved=ring.slots[0].used;fx.polys[1].texture=0;
    assert(BUILD()<0 && out.count==0 && ring.slots[0].used==saved);
    fx.polys[1].texture=1;fx.polys[1].first=REF_AGC_EFFECT_VERTICES;
    assert(BUILD()<0 && ring.slots[0].used==saved);
    fx.polys[1].first=4;ring.slots[0].used=16380;assert(BUILD()<0 && ring.slots[0].used==16380);
    fx.vertices_count=REF_AGC_EFFECT_VERTICES;v[0].position[0]=0;
    assert(ref_agc_effect_append(&fx,v,4,1,2,1)==-2 && fx.dropped==1);
    free(memory);puts("effect clipping, batching and rollback tests passed");return 0;
}
