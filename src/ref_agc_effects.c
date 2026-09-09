#include "ref_agc_effects.h"
#include <math.h>
#include <string.h>
static float dot(const float a[3],const float b[3])
{ return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
int ref_agc_decal_clip(const float (*p)[3],uint32_t count,const float center[3],
    const float normal[3],float width,float height,BspBundleVertex out[REF_AGC_DECAL_VERTICES])
{
    if(!p||!center||!normal||!out||count<3||count>REF_AGC_DECAL_VERTICES||
       !isfinite(width)||!isfinite(height)||width<=0||height<=0) return -1;
    for(int k=0;k<3;++k) if(!isfinite(center[k])||!isfinite(normal[k])) return -1;
    float nlen=sqrtf(dot(normal,normal));if(fabsf(nlen-1)>.01f) return -1;
    float s[3]={0},t[3];
    if(fabsf(normal[2])>.707f) { s[0]=1; }
    else { s[0]=-normal[1];s[1]=normal[0]; }
    float d=dot(s,normal);for(int k=0;k<3;++k) s[k]-=d*normal[k];
    float len=sqrtf(dot(s,s));if(len<.0001f) return -1;
    for(int k=0;k<3;++k)s[k]/=len;
    t[0]=normal[1]*s[2]-normal[2]*s[1];
    t[1]=normal[2]*s[0]-normal[0]*s[2];
    t[2]=normal[0]*s[1]-normal[1]*s[0];
    BspBundleVertex work[2][REF_AGC_DECAL_VERTICES];memset(work,0,sizeof(work));
    for(uint32_t i=0;i<count;++i) {
        float delta[3];for(int k=0;k<3;++k) {
            if(!isfinite(p[i][k])) return -1;
            work[0][i].position[k]=p[i][k]+normal[k]*.03f;
            delta[k]=p[i][k]-center[k];
        }
        work[0][i].base_uv[0]=dot(delta,s)/width+.5f;
        work[0][i].base_uv[1]=-dot(delta,t)/height+.5f;
        work[0][i].face_id=UINT32_MAX;
    }
    unsigned source=0;
    for(unsigned edge=0;edge<4 && count;++edge) {
        unsigned dest=source^1,axis=edge/2;float bound=(edge&1)?1:0;
        uint32_t next=0;
        for(uint32_t i=0;i<count;++i) {
            const BspBundleVertex *a=&work[source][i],*b=&work[source][(i+1)%count];
            float da=(a->base_uv[axis]-bound)*((edge&1)?-1:1);
            float db=(b->base_uv[axis]-bound)*((edge&1)?-1:1);
            if(da>=0) { if(next==REF_AGC_DECAL_VERTICES)return -1;work[dest][next++]=*a; }
            if((da<0)!=(db<0)) {
                if(next==REF_AGC_DECAL_VERTICES)return -1;
                float f=da/(da-db);BspBundleVertex *v=&work[dest][next++];*v=*a;
                for(int k=0;k<3;++k)v->position[k]=a->position[k]+f*(b->position[k]-a->position[k]);
                for(int k=0;k<2;++k)v->base_uv[k]=a->base_uv[k]+f*(b->base_uv[k]-a->base_uv[k]);
            }
        }
        count=next;source=dest;
    }
    if(count<3)return 0;
    memcpy(out,work[source],count*sizeof(*out));return (int)count;
}
int ref_agc_effect_append(RefAgcEffects *fx,const BspBundleVertex *v,uint32_t n,
    uint32_t texture,uint32_t mode,float alpha)
{
    if(!fx||!v||n<3||n>REF_AGC_DECAL_VERTICES||!texture||mode>5||!isfinite(alpha)||alpha<0||alpha>1)return -1;
    if(fx->count>=REF_AGC_EFFECT_POLYS||fx->vertices_count>REF_AGC_EFFECT_VERTICES-n) {++fx->dropped;return -2;}
    for(uint32_t i=0;i<n;++i) {
        for(int k=0;k<3;++k)if(!isfinite(v[i].position[k]))return -1;
        for(int k=0;k<2;++k)if(!isfinite(v[i].base_uv[k]))return -1;
    }
    fx->polys[fx->count++]=(RefAgcEffectPoly){fx->vertices_count,n,texture,mode,alpha};
    memcpy(fx->vertices+fx->vertices_count,v,n*sizeof(*v));fx->vertices_count+=n;return 0;
}
