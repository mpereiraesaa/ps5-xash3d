/* GPL-3.0-or-later. Engine-thread bridge to pinned FWGS shared lighting.
 * Keep the upstream implementation unchanged; only numeric snapshots cross
 * the immutable RefAGC frame boundary. */
#define REF_DLL 1
#include "ref_common.h"
#include "enginefeatures.h"
#include "ref_agc_studio_lighting.h"
#include <string.h>

ref_api_t gEngfuncs;
ref_client_t *gp_cl;
ref_host_t *gp_host;
struct movevars_s *gp_movevars;
dlight_t *gp_dlights;
int g_lightstylevalue[MAX_LIGHTSTYLES];
DEFINE_ENGINE_SHARED_CVAR_LIST()

/* GCC's optimizer loses the dm != NULL => initialized tbn relationship in
 * this pinned source. Keep the exception scoped to that upstream include. */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#include "../../third_party/xash3d-fwgs/ref/common/ref_light.c"
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

static int sync_engine(const ref_api_t *api)
{
    if(!api||!api->EngineGetParm||!api->pfnGetCvarPointer||
       !api->EV_GetPhysent||!api->Mod_SampleSizeForFace||
       !api->CL_TraceLine||!api->EV_TraceSurface) return -1;
    gEngfuncs=*api;
    gp_cl=(void *)api->EngineGetParm(PARM_GET_CLIENT_PTR,0);
    gp_host=(void *)api->EngineGetParm(PARM_GET_HOST_PTR,0);
    gp_movevars=(void *)api->EngineGetParm(PARM_GET_MOVEVARS_PTR,0);
    gp_dlights=(void *)api->EngineGetParm(PARM_GET_DLIGHTS_PTR,0);
    r_fullbright=api->pfnGetCvarPointer("r_fullbright");
    r_dynamic=api->pfnGetCvarPointer("r_dynamic");
    v_direct=api->pfnGetCvarPointer("direct");
    cl_lightstyle_lerping=api->pfnGetCvarPointer("cl_lightstyle_lerping");
    if(!gp_cl||!gp_host||!gp_movevars||!gp_dlights||!r_fullbright||
       !r_dynamic||!v_direct||!cl_lightstyle_lerping) return -1;
    /* These renderer-local cvars have the upstream defaults. No registration
     * pointers survive module unload. Extended BSP-model light sampling on. */
    r_lighting_extended.value=1;
    r_dlight_virtual_radius.value=3;
    return 0;
}

int PS5_StudioLightStyles(const ref_api_t *api, lightstyle_t *styles)
{
    if(!styles||sync_engine(api)) return -1;
    CL_RunLightStyles(styles);
    return 0;
}

int PS5_StudioCaptureLighting(const ref_api_t *api, cl_entity_t *entity,
    const float origin[3], int draw_world, RefAgcStudioLighting *out,
    uint16_t gamma[1024])
{
    if(!entity||!entity->model||!out||!gamma||sync_engine(api)) return -1;
    const uint16_t *table=(void *)api->EngineGetParm(PARM_GET_LIGHTGAMMATABLE_PTR,0);
    if(!table) return -1;
    for(unsigned i=0;i<1024;++i) {
        gamma[i]=(gp_host->features&ENGINE_LINEAR_GAMMA_SPACE)?i:table[i];
        if(gamma[i]>1023) return -1;
    }
    /* Do not let upstream cvFloorColor bookkeeping mutate an entity borrowed
     * for rendering; sample at the same interpolated origin as its pose. */
    cl_entity_t sample=*entity;
    VectorCopy(origin,sample.origin);
    float direction[3]={0}, spot[3]={0}, deluxe[3]={0};
    alight_t lighting={0};lighting.plightvec=direction;
    R_EntityDynamicLight(&sample,&lighting,draw_world,gp_cl->time,spot,deluxe);
    memset(out,0,sizeof(*out));
    out->ambient=lighting.ambientlight;out->shade=lighting.shadelight;
    out->scale=entity->curstate.scale>0?entity->curstate.scale:1;
    VectorCopy(lighting.color,out->color);VectorCopy(direction,out->direction);
    if(!isfinite(out->ambient)||!isfinite(out->shade)) return -1;
    for(int i=0;i<3;++i)
        if(!isfinite(out->color[i])||!isfinite(out->direction[i])) return -1;
    out->valid=1;
    return 0;
}

int PS5_SpriteCaptureLighting(const ref_api_t *api,const float origin[3],float out[3])
{
    if(!origin||!out||sync_engine(api))return -1;
    for(int k=0;k<3;++k)if(!isfinite(origin[k]))return -1;
    colorVec color=R_LightPoint(origin);
    out[0]=fminf(1,color.r/255.0f);
    out[1]=fminf(1,color.g/255.0f);
    out[2]=fminf(1,color.b/255.0f);
    return 0;
}
