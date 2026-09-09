#include "ref_agc_live_sprite.h"
#include "bsp_flat_scene.h"
#include "ps5_gfx1013_descriptor.h"
#include "ps5_transient_table.h"
#include "ps5_gpu_span.h"
#include <math.h>
#include <string.h>

static void axes(const float angles[3],float f[3],float r[3],float u[3])
{
    float p=angles[0]*0.01745329252f,y=angles[1]*0.01745329252f,z=angles[2]*0.01745329252f;
    float sp=sinf(p),cp=cosf(p),sy=sinf(y),cy=cosf(y),sr=sinf(z),cr=cosf(z);
    f[0]=cp*cy;f[1]=cp*sy;f[2]=-sp;
    r[0]=-sr*sp*cy+cr*sy;r[1]=-sr*sp*sy-cr*cy;r[2]=-sr*cp;
    u[0]=cr*sp*cy+sr*sy;u[1]=cr*sp*sy-sr*cy;u[2]=cr*cp;
}
int ref_agc_sprite_quad(const RefAgcLiveEntity *e,const RefAgcLiveView *view,BspBundleVertex v[4])
{
    if(!e||!view||!v||e->sprite_type<0||e->sprite_type>4||!isfinite(e->scale)) return -1;
    for(int k=0;k<3;++k) if(!isfinite(e->origin[k])||!isfinite(e->angles[k])||
        !isfinite(view->origin[k])||!isfinite(view->angles[k])) return -1;
    for(int k=0;k<4;++k) if(!isfinite(e->sprite_extents[k])) return -1;
    float f[3],r[3],u[3],origin[3];memcpy(origin,e->origin,sizeof(origin));
    axes(view->angles,f,r,u);
    int type=e->sprite_type;
    if(type==2 && e->angles[2]!=0) type=4;
    if(type==3) { axes(e->angles,f,r,u);for(int k=0;k<3;++k) origin[k]-=.01f*f[k]; }
    else if(type==0||type==1) {
        r[0]=type==1?origin[1]-view->origin[1]:f[1];
        r[1]=type==1?view->origin[0]-origin[0]:-f[0];r[2]=0;
        float len=hypotf(r[0],r[1]);if(len<.0001f) return 1;
        r[0]/=len;r[1]/=len;u[0]=u[1]=0;u[2]=1;
    } else if(type==4) {
        float angle=e->angles[2]*0.01745329252f,s=sinf(angle),c=cosf(angle);
        for(int k=0;k<3;++k) { float old=r[k];r[k]=old*c+u[k]*s;u[k]=-old*s+u[k]*c; }
    }
    float scale=e->scale>0?e->scale:1;
    const float uv[4][2]={{0,1},{0,0},{1,0},{1,1}};
    memset(v,0,4*sizeof(*v));
    for(int i=0;i<4;++i) {
        float pos[3],up=e->sprite_extents[(i==0||i==3)?1:0],right=e->sprite_extents[i<2?2:3];
        for(int k=0;k<3;++k) { pos[k]=origin[k]+scale*(up*u[k]+right*r[k]);if(!isfinite(pos[k])) return -1; }
        v[i].position[0]=pos[0];v[i].position[1]=pos[2];v[i].position[2]=-pos[1];
        memcpy(v[i].base_uv,uv[i],sizeof(uv[i]));
    }
    return 0;
}
static void *alloc(Ps5TransientRing *ring,uint32_t slot,size_t bytes,size_t align,const void *mapping,size_t size)
{
    Ps5TransientSlice slice;
    if(ps5_transient_ring_allocate(ring,slot,bytes,align,&slice)||!ps5_gpu_span_visible(mapping,size,slice.cpu,bytes)) return NULL;
    return slice.cpu;
}
int ref_agc_live_sprite_build(RefAgcLiveSpriteFrame *out,const RefAgcLiveFrame *live,
    const RefAgcGpuTextureCache *textures,Ps5TransientRing *ring,uint32_t slot,
    const void *mapping,size_t bytes,const float camera[3],const float forward[3],float aspect)
{
    if(!out||!live||!textures||!ring||slot>=ring->slot_count||live->entity_count>REF_AGC_LIVE_MAX_ENTITIES) return -1;
    memset(out,0,sizeof(*out));size_t saved=ring->slots[slot].used;float projection[16];
    if(bsp_flat_camera_matrix(projection,camera,forward,aspect)) return -1;
    for(uint32_t i=0;i<live->entity_count;++i) {
        const RefAgcLiveEntity *e=&live->entities[i];
        if(e->model_type!=REF_AGC_LIVE_MODEL_SPRITE) continue;
        if(e->render_mode!=0 && e->render_amount<=0) continue;
        BspBundleVertex quad[4];int rc=ref_agc_sprite_quad(e,&live->view,quad);
        if(rc==1) continue;
        if(rc || !e->sprite_texture) goto fail;
        RefAgcGpuTextureEntry tex;if(ref_agc_gpu_texture_cache_get(textures,e->sprite_texture,&tex)) goto fail;
        BspBundleVertex *v=alloc(ring,slot,sizeof(quad),16,mapping,bytes);
        uint16_t *ix=alloc(ring,slot,12,2,mapping,bytes);
        BspResourceConstants *c=alloc(ring,slot,sizeof(*c),256,mapping,bytes);
        Ps5TransientTable ct,vt,tt;
        if(!v||!ix||!c||ps5_transient_table_allocate(ring,slot,4,mapping,bytes,&ct)||
            ps5_transient_table_allocate(ring,slot,4,mapping,bytes,&vt)||
            ps5_transient_table_allocate(ring,slot,12,mapping,bytes,&tt)) goto fail;
        memcpy(v,quad,sizeof(quad));const uint16_t idx[6]={0,1,2,0,2,3};memcpy(ix,idx,sizeof(idx));
        memset(c,0,sizeof(*c));memcpy(c->mvp,projection,sizeof(projection));
        if(e->sprite_viewmodel) for(int k=0;k<4;++k) c->mvp[k*4+2]*=.3f;
        int tint=e->render_color[0]||e->render_color[1]||e->render_color[2];
        for(int k=0;k<3;++k) c->control[k]=tint?e->render_color[k]/255.0f:1;
        if(e->sprite_lit)for(int k=0;k<3;++k) {
            if(!isfinite(e->sprite_light[k])||e->sprite_light[k]<0||e->sprite_light[k]>1)goto fail;
            c->control[k]*=e->sprite_light[k];
        }
        c->control[3]=e->render_mode==0?1:fminf(1,e->render_amount/255.0f);
        memcpy(tt.words,tex.descriptor,sizeof(tex.descriptor));
        if(ps5_gfx1013_build_constant_vsharp(ct.words,(uintptr_t)c,sizeof(*c))||
            ps5_gfx1013_build_vsharp(vt.words,(uintptr_t)v,sizeof(*v),4)) goto fail;
        out->draws[out->count++]=(RefAgcLiveStudioDraw){ct.words,vt.words,tt.words,ix,6,e->sprite_texture,i,(uint32_t)e->sprite_format};
        out->additive+=e->render_mode==5;out->masked+=e->sprite_format==3;out->viewmodel+=e->sprite_viewmodel!=0;
    }
    return 0;
fail:
    ring->slots[slot].used=saved;memset(out,0,sizeof(*out));return -1;
}

int ref_agc_live_effect_build(RefAgcLiveEffectFrame *out,const RefAgcEffects *fx,
    const RefAgcGpuTextureCache *textures,Ps5TransientRing *ring,uint32_t slot,
    const void *mapping,size_t bytes,const float camera[3],const float forward[3],float aspect)
{
    if(!out||!fx||!textures||!ring||slot>=ring->slot_count||
       fx->count>REF_AGC_EFFECT_POLYS||fx->vertices_count>REF_AGC_EFFECT_VERTICES)return -1;
    memset(out,0,sizeof(*out));size_t saved=ring->slots[slot].used;float projection[16];
    if(bsp_flat_camera_matrix(projection,camera,forward,aspect))return -1;
    for(uint32_t i=0;i<fx->count;) {
        const RefAgcEffectPoly *first=&fx->polys[i];uint32_t end=i,nv=0,ni=0;
        while(end<fx->count) {
            const RefAgcEffectPoly *p=&fx->polys[end];
            if(p->texture!=first->texture||p->mode!=first->mode||p->alpha!=first->alpha)break;
            if(p->count<3||p->count>REF_AGC_DECAL_VERTICES||p->first>fx->vertices_count||
               p->count>fx->vertices_count-p->first||p->mode>5||!isfinite(p->alpha)||p->alpha<0||p->alpha>1)goto fail;
            if(nv>65535-p->count)break;
            nv+=p->count;ni+=(p->count-2)*3;++end;
        }
        RefAgcGpuTextureEntry tex;if(ref_agc_gpu_texture_cache_get(textures,first->texture,&tex))goto fail;
        BspBundleVertex *v=alloc(ring,slot,nv*sizeof(*v),16,mapping,bytes);
        uint16_t *ix=alloc(ring,slot,ni*sizeof(*ix),2,mapping,bytes);
        BspResourceConstants *c=alloc(ring,slot,sizeof(*c),256,mapping,bytes);
        Ps5TransientTable ct,vt,tt;
        if(!v||!ix||!c||ps5_transient_table_allocate(ring,slot,4,mapping,bytes,&ct)||
           ps5_transient_table_allocate(ring,slot,4,mapping,bytes,&vt)||
           ps5_transient_table_allocate(ring,slot,12,mapping,bytes,&tt))goto fail;
        uint32_t base=0,index=0;
        for(uint32_t j=i;j<end;++j) {
            const RefAgcEffectPoly *p=&fx->polys[j];
            for(uint32_t k=0;k<p->count;++k) {
                const BspBundleVertex *a=&fx->vertices[p->first+k];v[base+k]=*a;
                for(int axis=0;axis<3;++axis)if(!isfinite(a->position[axis]))goto fail;
                for(int axis=0;axis<2;++axis)if(!isfinite(a->base_uv[axis]))goto fail;
                v[base+k].position[0]=a->position[0];v[base+k].position[1]=a->position[2];v[base+k].position[2]=-a->position[1];
            }
            for(uint32_t k=1;k+1<p->count;++k) { ix[index++]=base;ix[index++]=base+k;ix[index++]=base+k+1; }
            base+=p->count;
        }
        memset(c,0,sizeof(*c));memcpy(c->mvp,projection,sizeof(projection));
        c->control[0]=c->control[1]=c->control[2]=1;c->control[3]=first->alpha;
        c->debug_values[11]=-1; /* existing packed RGB path; no new shader ABI */
        memcpy(tt.words,tex.descriptor,sizeof(tex.descriptor));
        if(ps5_gfx1013_build_constant_vsharp(ct.words,(uintptr_t)c,sizeof(*c))||
           ps5_gfx1013_build_vsharp(vt.words,(uintptr_t)v,sizeof(*v),nv))goto fail;
        out->draws[out->count++]=(RefAgcLiveStudioDraw){ct.words,vt.words,tt.words,ix,ni,first->texture,first->mode,0};
        i=end;
    }
    return 0;
fail:
    ring->slots[slot].used=saved;memset(out,0,sizeof(*out));return -1;
}
