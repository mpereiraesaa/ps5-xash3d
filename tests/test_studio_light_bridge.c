/* Exercise the unchanged upstream BSP/dynamic-light code through our bridge. */
#include "../xash/platform_ps5/studio_light_ps5.c"
#include <assert.h>
#include <stdio.h>
static ref_client_t client;
static ref_host_t host;
static movevars_t movevars;
static dlight_t dynamic[MAX_DLIGHTS];
static lightstyle_t styles[MAX_LIGHTSTYLES];
static uint16_t gamma_table[1024];
static cvar_t full, dyn, direct, lerp;
static physent_t physical;
static intptr_t parameter(int p,int arg) {
    (void)arg;
    switch(p) {
    case PARM_GET_CLIENT_PTR:return (intptr_t)&client;
    case PARM_GET_HOST_PTR:return (intptr_t)&host;
    case PARM_GET_MOVEVARS_PTR:return (intptr_t)&movevars;
    case PARM_GET_DLIGHTS_PTR:return (intptr_t)dynamic;
    case PARM_GET_LIGHTGAMMATABLE_PTR:return (intptr_t)gamma_table;
    default:return 0;
    }
}
static cvar_t *cvar(const char *name) {
    if(!strcmp(name,"r_fullbright")) return &full;
    if(!strcmp(name,"r_dynamic")) return &dyn;
    if(!strcmp(name,"direct")) return &direct;
    if(!strcmp(name,"cl_lightstyle_lerping")) return &lerp;
    return NULL;
}
static physent_t *phys(int i) {return i==0?&physical:NULL;}
static int sample_size(const msurface_t *s) {(void)s;return 16;}
static pmtrace_t trace(vec3_t a,vec3_t b,int flags) {
    (void)a;(void)b;(void)flags;return (pmtrace_t){0};
}
static msurface_t *surface(int i,float *a,float *b) {(void)i;(void)a;(void)b;return NULL;}
int main(void) {
    for(unsigned i=0;i<1024;++i) gamma_table[i]=i;
    ref_api_t api={.EngineGetParm=parameter,.pfnGetCvarPointer=cvar,
        .EV_GetPhysent=phys,.Mod_SampleSizeForFace=sample_size,
        .CL_TraceLine=trace,.EV_TraceSurface=surface};
    model_t world={0},model={.type=mod_studio};
    mnode_t root={0},leaf={.contents=-1};
    mplane_t plane={.normal={0,0,1},.type=2};
    mextrasurf_t info={.lightmapmins={-64,-64},.lightextents={128,128},
        .lmvecs={{1,0,0,0},{0,1,0,0}}};
    color24 samples[81];for(unsigned i=0;i<81;++i) samples[i]=(color24){100,80,60};
    msurface_t surf={.plane=&plane,.info=&info,.samples=samples,.styles={0,255,255,255}};
    root.plane=&plane;root.children_[0]=root.children_[1]=&leaf;root.numsurfaces_0=1;
    world.type=mod_brush;world.nodes=&root;world.surfaces=&surf;world.lightdata=(void *)samples;
    client.models[1]=&world;client.time=10;physical.model=&world;
    direct.value=.9f;dyn.value=1;
    cl_entity_t entity={.model=&model,.origin={0,0,24}};
    RefAgcStudioLighting out;uint16_t table[1024];
    assert(!PS5_StudioLightStyles(&api,styles));
    assert(!PS5_StudioCaptureLighting(&api,&entity,entity.origin,1,&out,table));
    assert(out.valid&&out.ambient==10&&out.shade==90);
    assert(fabsf(out.color[1]-.8f)<.001f&&out.direction[2]==-1);
    assert(entity.cvFloorColor.r==0); /* borrowed entity not modified */
    float sprite_light[3];
    assert(!PS5_SpriteCaptureLighting(&api,entity.origin,sprite_light));
    assert(fabsf(sprite_light[0]-100/255.0f)<.001f);
    assert(fabsf(sprite_light[1]-80/255.0f)<.001f);
    assert(fabsf(sprite_light[2]-60/255.0f)<.001f);
    const float invalid_origin[3]={NAN,0,0};
    assert(PS5_SpriteCaptureLighting(&api,invalid_origin,sprite_light)<0);
    dynamic[0].die=20;dynamic[0].radius=64;dynamic[0].color.r=255;
    VectorCopy(entity.origin,dynamic[0].origin);
    assert(!PS5_StudioCaptureLighting(&api,&entity,entity.origin,1,&out,table));
    assert(out.ambient>10&&out.color[1]<.8f);
    full.value=1;
    assert(!PS5_StudioCaptureLighting(&api,&entity,entity.origin,1,&out,table));
    assert(out.ambient==192&&out.shade==0&&out.color[0]==1);
    gamma_table[100]=777;assert(!PS5_StudioCaptureLighting(&api,&entity,entity.origin,1,&out,table));
    assert(table[100]==777);
    host.features=ENGINE_LINEAR_GAMMA_SPACE;
    assert(!PS5_StudioCaptureLighting(&api,&entity,entity.origin,1,&out,table)&&table[100]==100);
    api.EV_GetPhysent=NULL;assert(PS5_StudioCaptureLighting(&api,&entity,entity.origin,1,&out,table)<0);
    puts("Studio upstream BSP, dynamic, fullbright, gamma and snapshot bridge tests passed");
}
