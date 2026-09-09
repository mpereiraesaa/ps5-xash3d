#include "ref_agc_live_sprite.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void flush(const void *p,size_t n,void *u){(void)u;assert(p&&n);}
static const void *address(const uint32_t *w)
{ return (void *)(uintptr_t)((uint64_t)w[0]|((uint64_t)(w[1]&65535)<<32)); }
int main(void)
{
    static RefAgcLiveFrame live;
    static RefAgcLiveSpriteFrame out;
    static RefAgcGpuTextureCache textures;
    _Alignas(256) unsigned char pixels[256]={0};
    assert(!ref_agc_gpu_texture_cache_init(&textures,pixels,(uintptr_t)pixels,sizeof(pixels),flush,NULL));
    textures.entries[0].active=1;
    live.entity_count=1;
    RefAgcLiveEntity *e=&live.entities[0];
    *e=(RefAgcLiveEntity){.model_type=REF_AGC_LIVE_MODEL_SPRITE,.sprite_texture=1,
        .sprite_type=2,.sprite_extents={2,-2,-3,3},.origin={10,0,0},.scale=1};
    BspBundleVertex v[4];
    assert(!ref_agc_sprite_quad(e,&live.view,v));
    assert(v[0].position[0]==10 && v[0].position[1]==-2 && v[0].position[2]==-3);
    assert(v[2].position[1]==2 && v[2].position[2]==3);
    assert(v[0].base_uv[0]==0 && v[0].base_uv[1]==1 && v[2].base_uv[0]==1 && v[2].base_uv[1]==0);
    e->angles[2]=90;
    assert(!ref_agc_sprite_quad(e,&live.view,v));
    assert(fabsf(v[0].position[1]+3)<.0001f && fabsf(v[0].position[2]-2)<.0001f);
    e->angles[2]=0;e->sprite_type=3;
    assert(!ref_agc_sprite_quad(e,&live.view,v) && fabsf(v[0].position[0]-9.99f)<.0001f);
    e->sprite_type=1;e->origin[0]=0;
    assert(ref_agc_sprite_quad(e,&live.view,v)==1);
    e->origin[0]=10;e->sprite_type=2;
    e->scale=NAN;assert(ref_agc_sprite_quad(e,&live.view,v)<0);e->scale=1;
    unsigned char *memory=aligned_alloc(256,16384);assert(memory);
    Ps5TransientRing ring;
    assert(!ps5_transient_ring_init(&ring,memory,16384,1,256));
    assert(!ps5_transient_ring_begin(&ring,0,0,0));
    const float camera[3]={0},forward[3]={0,0,-1};
#define BUILD() ref_agc_live_sprite_build(&out,&live,&textures,&ring,0,memory,16384,camera,forward,1)
    assert(!BUILD() && out.count==1);
    const BspResourceConstants *c=address(out.draws[0].constants);
    assert(c->control[0]==1 && c->control[3]==1 && c->debug_values[11]==0);
    BspResourceConstants original=*c;
    e->sprite_lit=1;e->sprite_light[0]=.5f;e->sprite_light[1]=.25f;e->sprite_light[2]=1;
    ring.slots[0].used=0;assert(!BUILD());c=address(out.draws[0].constants);
    assert(c->control[0]==.5f && c->control[1]==.25f && c->control[2]==1);
    e->sprite_light[0]=NAN;size_t checkpoint=ring.slots[0].used;
    assert(BUILD()<0 && ring.slots[0].used==checkpoint);
    e->sprite_lit=0;ring.slots[0].used=0;assert(!BUILD());
    assert(out.draws[0].indices[5]==3);
    e->sprite_viewmodel=1;e->render_mode=5;e->render_amount=128;e->render_color[0]=255;
    ring.slots[0].used=0;assert(!BUILD() && out.additive==1 && out.viewmodel==1);
    c=address(out.draws[0].constants);
    assert(c->control[0]==1 && c->control[1]==0 && fabsf(c->control[3]-128/255.0f)<.00001f);
    for(int k=0;k<4;++k) assert(c->mvp[k*4+2]==original.mvp[k*4+2]*.3f);
    size_t saved=ring.slots[0].used;
    e->sprite_texture=0;assert(BUILD()<0 && !out.count && ring.slots[0].used==saved);
    e->sprite_texture=1;textures.entries[0].active=0;
    assert(BUILD()<0 && !out.count && ring.slots[0].used==saved);
    textures.entries[0].active=1;
    ring.slots[0].used=ring.slots[0].bytes-16;saved=ring.slots[0].used;
    assert(BUILD()<0 && !out.count && ring.slots[0].used==saved);
    e->render_amount=0;assert(!BUILD() && !out.count && ring.slots[0].used==saved);
    free(memory);puts("live sprite tests passed");return 0;
}
