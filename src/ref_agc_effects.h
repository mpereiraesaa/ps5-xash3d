#ifndef REF_AGC_EFFECTS_H
#define REF_AGC_EFFECTS_H
#include <stdint.h>
#include "bsp_bundle.h"
enum { REF_AGC_EFFECT_VERTICES=32768, REF_AGC_EFFECT_POLYS=4608,
       REF_AGC_DECAL_VERTICES=32, REF_AGC_DECALS=256 };
typedef struct RefAgcEffectPoly {
    uint32_t first,count,texture,mode;
    float alpha;
} RefAgcEffectPoly;
typedef struct RefAgcEffects {
    BspBundleVertex vertices[REF_AGC_EFFECT_VERTICES];
    RefAgcEffectPoly polys[REF_AGC_EFFECT_POLYS];
    uint32_t vertices_count,count,particles,tracers,decals,dropped;
} RefAgcEffects;
/* Convex BSP polygon -> decal UV square, in engine-local coordinates.
 * Returns vertex count, zero for no intersection, -1 for invalid/overflow. */
int ref_agc_decal_clip(const float (*polygon)[3],uint32_t count,
    const float center[3],const float normal[3],float width,float height,
    BspBundleVertex out[REF_AGC_DECAL_VERTICES]);
int ref_agc_effect_append(RefAgcEffects *,const BspBundleVertex *,uint32_t,
    uint32_t texture,uint32_t mode,float alpha);
#endif
