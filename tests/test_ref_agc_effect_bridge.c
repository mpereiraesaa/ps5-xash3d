/* Execute the production owner-thread bridge against a synthetic engine. */
#include <string.h>
#include <math.h>
#include <assert.h>
#include <stdio.h>
#include "xash3d_types.h"
#include "const.h"
#include "cvardef.h"
#include "xash3d_mathlib.h"
#include "ref_api.h"
#include "ref_params.h"
#include "ref_agc_live_frame.h"
#include "ref_agc_texture_store.h"
static ref_api_t ref_agc_engine;
static RefAgcLiveStore ref_agc_live;
static RefAgcTextureStore ref_agc_textures;
static uint64_t ref_agc_begin_calls;
static int RefAgcFindTexture(const char *name){(void)name;return 1;}
static int RefAgcBrushVertexIndex(const model_t *m,int edge,uint32_t *out)
{if(edge<0||edge>=m->numvertexes)return -1;*out=edge;return 0;}
int ref_agc_texture_store_get(RefAgcTextureStore *store,uint32_t handle,RefAgcTextureView *out)
{
    (void)store;if(handle!=1)return -1;
    *out=(RefAgcTextureView){.handle=1,.width=4,.height=4,.name="decals/{shot1.wad"};return 0;
}
#include "../xash/platform_ps5/effect_bridge.h"
static ref_client_t client;
static ref_host_t host;
static movevars_t movevars;
static color24 palette[256];
static unsigned thinks,efx_calls;
static double last_dt;
static particle_t particle;
static intptr_t parm(int p,int arg)
{
    (void)arg;
    switch(p) {
    case PARM_GET_CLIENT_PTR:return (intptr_t)&client;
    case PARM_GET_HOST_PTR:return (intptr_t)&host;
    case PARM_GET_MOVEVARS_PTR:return (intptr_t)&movevars;
    case PARM_GET_PALETTE_PTR:return (intptr_t)palette;
    default:return 0;
    }
}
static void log_message(const char *fmt,...){(void)fmt;}
static void think(double dt,particle_t *p){++thinks;last_dt=dt;p->org[0]+=dt*p->vel[0];}
static void efx(float dt,qboolean trans){assert(trans);++efx_calls;RefAgcParticles(dt,&particle,1);}
static model_t *default_sprite(enum ref_defaultsprite_e id){(void)id;return NULL;}
static cvar_t *cvar(const char *name){(void)name;return NULL;}
int main(void)
{
    ref_agc_engine=(ref_api_t){.EngineGetParm=parm,.Con_Printf=log_message,
        .CL_ThinkParticle=think,.CL_DrawEFX=efx,.GetDefaultSprite=default_sprite,.pfnGetCvarPointer=cvar};
    mvertex_t vertices[4]={{{-10,-10,0}},{{10,-10,0}},{{10,10,0}},{{-10,10,0}}};
    mplane_t plane={.normal={0,0,1}};
    msurface_t surface={.plane=&plane,.numedges=4};
    model_t model={.type=mod_brush,.vertexes=vertices,.numvertexes=4,.surfaces=&surface,
        .numsurfaces=1,.nummodelsurfaces=1};
    client.models[1]=&model;
    cl_entity_t entities[2]={0};entities[1].model=&model;entities[1].curstate.modelindex=1;
    RefAgcProcessEntities(true,entities,2);
    vec3_t impact={0,0,0};RefAgcDecalShoot(1,0,0,impact,0,1);
    assert(ref_agc_decals[0].count==4);
    RefAgcCaptureDecals();assert(ref_agc_live.building.effects.decals==1);
    decallist_t list[REF_AGC_DECALS];assert(RefAgcCreateDecalList(list)==1);
    assert(!strcmp(list[0].name,"{shot1") && (list[0].flags&FDECAL_LOCAL_SPACE));
    RefAgcDecalRemove(0);assert(!ref_agc_decals[0].count);
    RefAgcClearDecals();entities[1].origin[0]=100;impact[0]=100;
    RefAgcDecalShoot(1,1,0,impact,0,1);assert(ref_agc_decals[0].count==4);
    assert(ref_agc_decals[0].center[0]==0);
    entities[1].origin[0]=150;
    memset(&ref_agc_live.building.effects,0,sizeof(ref_agc_live.building.effects));
    RefAgcCaptureDecals();assert(ref_agc_live.building.effects.vertices[0].position[0]>147);
    RefAgcClearDecals();impact[0]=0;
    RefAgcDecalShoot(1,0,1,impact,FDECAL_PERMANENT,1);
    RefAgcDecalRemove(0);assert(ref_agc_decals[0].count==4);
    for(unsigned i=0;i<REF_AGC_DECALS+2;++i)RefAgcDecalShoot(1,0,1,impact,0,1);
    assert(ref_agc_decals[0].flags&FDECAL_PERMANENT);
    assert(RefAgcCreateDecalList(list)==REF_AGC_DECALS);
    RefAgcClearDecals();assert(RefAgcCreateDecalList(list)==0);
    memset(&ref_agc_live.building.effects,0,sizeof(ref_agc_live.building.effects));
    palette[7]=(color24){100,20,5};particle=(particle_t){.color=7,.die=10,.type=pt_static,.vel={10,0,0}};
    host.frametime=.1;ref_agc_live.building.view.flags=RF_DRAW_WORLD;ref_agc_begin_calls=1;
    RefAgcCaptureEffects(&client);assert(thinks==1 && efx_calls==1 && fabs(particle.org[0]-1)<.00001);
    assert(ref_agc_live.building.effects.particles==1);
    assert(ref_agc_live.building.effects.vertices[0].face_id==0xff051464);
    RefAgcCaptureEffects(&client);assert(thinks==1); /* once per frame */
    ++ref_agc_begin_calls;client.paused=true;RefAgcCaptureEffects(&client);assert(thinks==2&&last_dt==0);
    particle.type=pt_blob;particle.unused=0;
    unsigned count=ref_agc_live.building.effects.particles;RefAgcParticles(.1,&particle,1);
    assert(thinks==3&&ref_agc_live.building.effects.particles==count); /* invisible particles still simulate */
    movevars.gravity=800;particle.type=pt_grav;particle.vel[2]=0;RefAgcTracers(.1,&particle);
    assert(particle.vel[2]==-80); /* tracer integration even without texture */
    RefAgcProcessEntities(false,NULL,0);assert(!ref_agc_effect_entities&&RefAgcCreateDecalList(list)==0);
    puts("effect bridge lifecycle, motion, serialization and simulation tests passed");return 0;
}
