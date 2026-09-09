#ifndef REF_AGC_STUDIO_LIGHTING_H
#define REF_AGC_STUDIO_LIGHTING_H
#include <stdint.h>
#include <math.h>
/* Owned snapshot, never an engine pointer. Units follow R_StudioLighting. */
typedef struct RefAgcStudioLighting {
    float ambient, shade, color[3], direction[3], scale;
    uint32_t valid;
} RefAgcStudioLighting;

/* Pinned FWGS gl_studio.c modified hemispherical lighting, SHADE_LAMBERT
 * 1.4953241; engine gamma-table lookup is performed before interpolation. */
static inline int ref_agc_studio_vertex_light(const RefAgcStudioLighting *l,
    const uint16_t gamma[1024], const float normal[3], uint32_t flags,
    uint32_t *rgba)
{
    if(!l||!gamma||!normal||!rgba||l->valid!=1||!isfinite(l->ambient)||
       !isfinite(l->shade)||!isfinite(l->scale)||l->scale<=0||
       l->ambient<0||l->ambient>255||l->shade<0||l->shade>255) return -1;
    float cosine=0;
    for(int k=0;k<3;++k) {
        if(!isfinite(normal[k])||!isfinite(l->direction[k])||
           !isfinite(l->color[k])||l->color[k]<0||l->color[k]>1) return -1;
        cosine+=normal[k]*l->direction[k];
    }
    if(l->scale>1) cosine/=l->scale;
    if(!isfinite(cosine)) return -1;
    float illum=l->ambient;
    if(flags&1u) illum+=l->shade*0.8f;
    else {
        cosine=fminf(cosine,1.0f);
        illum+=l->shade;
        cosine=(cosine+(1.4953241f-1.0f))/1.4953241f;
        if(cosine>0) illum-=l->shade*cosine;
        illum=fmaxf(illum,0);
    }
    illum=fminf(fmaxf(illum,0),255);
    unsigned index=(unsigned)(illum*4);
    if(gamma[index]>1023) return -1;
    float value=(flags&4u)?1.0f:gamma[index]/1023.0f;
    uint32_t packed=0xff000000u;
    for(unsigned k=0;k<3;++k)
        packed|=(uint32_t)(fminf(255.0f,fmaxf(0,value*l->color[k]*255.0f)))<<(8*k);
    *rgba=packed;
    return 0;
}
#endif
