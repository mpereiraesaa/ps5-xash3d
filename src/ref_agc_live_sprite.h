#ifndef REF_AGC_LIVE_SPRITE_H
#define REF_AGC_LIVE_SPRITE_H
#include "ref_agc_live_studio.h"
#include "bsp_bundle.h"
typedef struct RefAgcLiveSpriteFrame {
    RefAgcLiveStudioDraw draws[REF_AGC_LIVE_MAX_ENTITIES];
    uint32_t count, additive, masked, viewmodel;
} RefAgcLiveSpriteFrame;
typedef struct RefAgcLiveEffectFrame {
    RefAgcLiveStudioDraw draws[REF_AGC_EFFECT_POLYS];
    uint32_t count;
} RefAgcLiveEffectFrame;
int ref_agc_live_effect_build(RefAgcLiveEffectFrame *,const RefAgcEffects *,
    const RefAgcGpuTextureCache *,Ps5TransientRing *,uint32_t,const void *,size_t,
    const float[3],const float[3],float);
int ref_agc_sprite_quad(const RefAgcLiveEntity *, const RefAgcLiveView *, BspBundleVertex[4]);
int ref_agc_live_sprite_build(RefAgcLiveSpriteFrame *,const RefAgcLiveFrame *,
    const RefAgcGpuTextureCache *,Ps5TransientRing *,uint32_t,const void *,size_t,
    const float[3],const float[3],float);
#endif
