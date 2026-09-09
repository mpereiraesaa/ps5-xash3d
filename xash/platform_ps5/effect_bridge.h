/* Engine-thread effects bridge. No engine pointers enter the published frame.
 * Particle integration follows pinned ref/gl/gl_rpart.c and CL_ThinkParticle. */
#include "pmove.h"
static cl_entity_t *ref_agc_effect_entities;
static unsigned ref_agc_effect_entity_count;
static uint64_t ref_agc_effect_serial;
typedef struct RefAgcDecal {
    BspBundleVertex vertices[REF_AGC_DECAL_VERTICES];
    vec3_t center,normal;
    uint32_t count,texture,shot;
    int entity,model,flags;
    float scale;
} RefAgcDecal;
static RefAgcDecal ref_agc_decals[REF_AGC_DECALS];
static uint32_t ref_agc_decal_next,ref_agc_decal_shot;
static void RefAgcClearDecals(void)
{
    memset(ref_agc_decals,0,sizeof(ref_agc_decals));
    ref_agc_decal_next=ref_agc_decal_shot=0;
}
static void RefAgcProcessEntities(qboolean allocate,cl_entity_t *entities,unsigned count)
{
    ref_agc_effect_entities=allocate?entities:NULL;
    ref_agc_effect_entity_count=allocate?count:0;
    RefAgcClearDecals();ref_agc_effect_serial=0;
}
static cl_entity_t *RefAgcEffectEntity(int index)
{
    return index>=0 && (unsigned)index<ref_agc_effect_entity_count && ref_agc_effect_entities ?
        &ref_agc_effect_entities[index]:NULL;
}
static void RefAgcDecalRemove(int texture)
{
    if(texture<0)return;
    for(unsigned i=0;i<REF_AGC_DECALS;++i)
        if(!(ref_agc_decals[i].flags&FDECAL_PERMANENT) &&
           (!texture||(int)ref_agc_decals[i].texture==texture))ref_agc_decals[i].count=0;
}
static void RefAgcDecalShoot(int texture,int entity,int modelindex,vec3_t pos,int flags,float scale)
{
    const ref_client_t *cl=(const ref_client_t *)ref_agc_engine.EngineGetParm(PARM_GET_CLIENT_PTR,0);
    cl_entity_t *ent=entity>0?RefAgcEffectEntity(entity):NULL;
    if(!cl||texture<=0||entity<0||(entity>0&&!ent)||!isfinite(scale))return;
    if(modelindex<=0)modelindex=ent?ent->curstate.modelindex:1;
    if(modelindex<1||modelindex>MAX_MODELS)return;
    const model_t *model=cl->models[modelindex];RefAgcTextureView tex;
    if(!model||model->type!=mod_brush||!model->surfaces||
       ref_agc_texture_store_get(&ref_agc_textures,texture,&tex))return;
    vec3_t center;VectorCopy(pos,center);
    for(int k=0;k<3;++k)if(!isfinite(center[k]))return;
    if(ent && !(flags&FDECAL_LOCAL_SPACE)) {
        matrix4x4 matrix;Matrix4x4_CreateFromEntity(matrix,ent->angles,ent->origin,1);
        Matrix4x4_VectorITransform(matrix,pos,center);
    }
    flags|=FDECAL_LOCAL_SPACE;
    if(!(model->flags&MODEL_HAS_ORIGIN))flags|=FDECAL_USE_LANDMARK;
    scale=fmaxf(.01f,fminf(16,scale));
    const uint32_t shot=++ref_agc_decal_shot;
    int first=model->firstmodelsurface,count=model->nummodelsurfaces;
    if(first<0||count<0||first>model->numsurfaces||count>model->numsurfaces-first)return;
    unsigned made=0;
    for(int s=first;s<first+count;++s) {
        const msurface_t *surface=&model->surfaces[s];
        if(!surface->plane||surface->numedges<3||surface->numedges>REF_AGC_DECAL_VERTICES||
           surface->flags&(SURF_DRAWTURB|SURF_DRAWSKY|SURF_CONVEYOR|SURF_TRANSPARENT))continue;
        float distance=DotProduct(center,surface->plane->normal)-surface->plane->dist;
        if(fabsf(distance)>4)continue;
        float polygon[REF_AGC_DECAL_VERTICES][3];int valid=1;
        for(int k=0;k<surface->numedges;++k) {
            uint32_t vertex;
            if(RefAgcBrushVertexIndex(model,surface->firstedge+k,&vertex)) {valid=0;break;}
            VectorCopy(model->vertexes[vertex].position,polygon[k]);
        }
        if(!valid)continue;
        vec3_t normal;VectorCopy(surface->plane->normal,normal);
        if(surface->flags&SURF_PLANEBACK)VectorNegate(normal,normal);
        BspBundleVertex clipped[REF_AGC_DECAL_VERTICES];
        int n=ref_agc_decal_clip(polygon,surface->numedges,center,normal,
            tex.width/scale,tex.height/scale,clipped);
        if(n<=0)continue;
        unsigned slot=ref_agc_decal_next,tries=0;
        while(tries<REF_AGC_DECALS && ref_agc_decals[slot].count &&
              (ref_agc_decals[slot].flags&FDECAL_PERMANENT)) {slot=(slot+1)%REF_AGC_DECALS;++tries;}
        if(tries==REF_AGC_DECALS)break;
        RefAgcDecal *d=&ref_agc_decals[slot];memset(d,0,sizeof(*d));
        memcpy(d->vertices,clipped,n*sizeof(*clipped));d->count=n;d->texture=texture;
        d->entity=entity;d->model=modelindex;d->flags=flags;d->scale=scale;d->shot=shot;
        VectorCopy(center,d->center);VectorCopy(normal,d->normal);
        ref_agc_decal_next=(slot+1)%REF_AGC_DECALS;++made;
    }
    if(shot<=32||shot%64==0)ref_agc_engine.Con_Printf(
        "REF_AGC_DECAL_SHOOT schema=1 shot=%u texture=%d entity=%d model=%d fragments=%u capacity=%u\n",
        shot,texture,entity,modelindex,made,REF_AGC_DECALS);
}
static int RefAgcCreateDecalList(decallist_t *list)
{
    if(!list)return 0;
    int count=0;
    uint32_t seen[REF_AGC_DECALS];
    for(unsigned age=0;age<REF_AGC_DECALS;++age) {
        unsigned i=(ref_agc_decal_next+age)%REF_AGC_DECALS;
        const RefAgcDecal *d=&ref_agc_decals[i];RefAgcTextureView tex;
        if(!d->count||(d->flags&(FDECAL_CUSTOM|FDECAL_DONTSAVE))||
           ref_agc_texture_store_get(&ref_agc_textures,d->texture,&tex))continue;
        int duplicate=0;for(int j=0;j<count;++j)
            if(seen[j]==d->shot)duplicate=1;
        if(duplicate)continue;
        seen[count]=d->shot;
        decallist_t *out=&list[count++];memset(out,0,sizeof(*out));
        out->depth=(byte)(count-1);
        VectorCopy(d->center,out->position);VectorCopy(d->normal,out->impactPlaneNormal);
        const char *name=strrchr(tex.name,'/');name=name?name+1:tex.name;
        strncpy(out->name,name,sizeof(out->name)-1);
        char *extension=strrchr(out->name,'.');if(extension)*extension=0;
        out->entityIndex=d->entity;
        out->flags=d->flags;out->scale=d->scale;
    }
    return count;
}
static void RefAgcCaptureDecals(void)
{
    RefAgcEffects *fx=&ref_agc_live.building.effects;
    for(unsigned age=0;age<REF_AGC_DECALS;++age) {
        unsigned i=(ref_agc_decal_next+age)%REF_AGC_DECALS;
        RefAgcDecal *d=&ref_agc_decals[i];if(!d->count)continue;
        cl_entity_t *ent=d->entity>0?RefAgcEffectEntity(d->entity):NULL;
        if(d->entity>0 && (!ent||ent->curstate.modelindex!=d->model))continue;
        RefAgcTextureView tex;if(ref_agc_texture_store_get(&ref_agc_textures,d->texture,&tex)) {d->count=0;continue;}
        BspBundleVertex vertices[REF_AGC_DECAL_VERTICES];memcpy(vertices,d->vertices,d->count*sizeof(*vertices));
        if(ent) {
            matrix4x4 matrix;Matrix4x4_CreateFromEntity(matrix,ent->angles,ent->origin,1);
            for(unsigned j=0;j<d->count;++j)Matrix4x4_VectorTransform(matrix,d->vertices[j].position,vertices[j].position);
        }
        if(!ref_agc_effect_append(fx,vertices,d->count,d->texture,kRenderTransTexture,1))++fx->decals;
    }
}
static uint32_t RefAgcEffectRGB(color24 color)
{ return color.r|((uint32_t)color.g<<8)|((uint32_t)color.b<<16)|UINT32_C(0xff000000); }
static void RefAgcParticles(double dt,particle_t *particles,float partsize)
{
    const RefAgcLiveView *view=&ref_agc_live.building.view;
    RefAgcEffects *fx=&ref_agc_live.building.effects;
    const color24 *palette=(const color24 *)ref_agc_engine.EngineGetParm(PARM_GET_PALETTE_PTR,0);
    int texture=RefAgcFindTexture(REF_PARTICLE_TEXTURE);
    vec3_t forward,right,up;AngleVectors(view->angles,forward,right,up);
    for(particle_t *p=particles;p;) {
        particle_t *next=p->next;
        if(texture>0 && palette && (p->type!=pt_blob||p->unused==255)) {
            vec3_t delta;VectorSubtract(p->org,view->origin,delta);
            float size=partsize+DotProduct(delta,forward);
            size=size<20?partsize:partsize+size*.002f;
            int index=bound(0,p->color,255);p->color=index;
            float alpha=p->type==pt_static?1:fmaxf(0,fminf(1,(p->die-view->time_seconds)*16));
            BspBundleVertex v[4];memset(v,0,sizeof(v));
            const float uv[4][2]={{0,1},{0,0},{1,0},{1,1}};
            for(int j=0;j<4;++j) {
                for(int k=0;k<3;++k)v[j].position[k]=p->org[k]+size*(right[k]*((j==0||j==3)?-1:1)+up[k]*(j<2?1:-1));
                memcpy(v[j].base_uv,uv[j],sizeof(uv[j]));v[j].face_id=RefAgcEffectRGB(palette[index]);
            }
            if(alpha>0&&!ref_agc_effect_append(fx,v,4,texture,kRenderTransTexture,alpha))++fx->particles;
        }
        ref_agc_engine.CL_ThinkParticle(dt,p);p=next;
    }
}
static void RefAgcTracers(double dt,particle_t *particles)
{
    static const color24 colors[]={{255,255,255},{255,0,0},{0,255,0},{0,0,255},
        {255,255,255},{255,167,17},{255,130,90},{55,60,144},{255,130,90},{255,140,90},{200,130,90},{255,120,70}};
    const RefAgcLiveView *view=&ref_agc_live.building.view;
    RefAgcEffects *fx=&ref_agc_live.building.effects;
    const movevars_t *mv=(const movevars_t *)ref_agc_engine.EngineGetParm(PARM_GET_MOVEVARS_PTR,0);
    model_t *dot=ref_agc_engine.GetDefaultSprite(REF_DOT_SPRITE);
    mspriteframe_t *frame=dot?ref_agc_engine.R_GetSpriteFrame(dot,0,0):NULL;
    float gravity=(mv?mv->gravity:800)*dt,drag=fmaxf(0,1-dt*.9);
    for(particle_t *p=particles;p;p=p->next) {
        vec3_t delta,end,sight,normal;
        float atten=fmaxf(0,fminf(.1f,p->die-view->time_seconds));
        VectorScale(p->vel,p->ramp*atten,delta);VectorAdd(p->org,delta,end);
        VectorSubtract(p->org,view->origin,sight);CrossProduct(delta,sight,normal);
        float len=VectorLength(normal);
        if(frame && len>.0001f) {
            float width=p->type==0?1.5f:p->type==1?.5f:1;
            VectorScale(normal,width/len,normal);
            int ci=p->color>=0&&p->color<(int)(sizeof(colors)/sizeof(colors[0]))?p->color:4;
            color24 color=colors[ci];
            if(ci==4 && ref_agc_engine.pfnGetCvarPointer("tracerred")) {
                float alpha=ref_agc_engine.pfnGetCvarFloat("traceralpha");
                color.r=bound(0,ref_agc_engine.pfnGetCvarFloat("tracerred")*alpha*255,255);
                color.g=bound(0,ref_agc_engine.pfnGetCvarFloat("tracergreen")*alpha*255,255);
                color.b=bound(0,ref_agc_engine.pfnGetCvarFloat("tracerblue")*alpha*255,255);
            }
            BspBundleVertex v[4];memset(v,0,sizeof(v));
            const float uv[4][2]={{0,.8f},{1,.8f},{1,0},{0,0}};
            for(int j=0;j<4;++j) {
                for(int k=0;k<3;++k)v[j].position[k]=(j<2?end[k]:p->org[k])+normal[k]*((j==0||j==3)?-1:1);
                memcpy(v[j].base_uv,uv[j],sizeof(uv[j]));v[j].face_id=RefAgcEffectRGB(color);
            }
            if(!ref_agc_effect_append(fx,v,4,frame->gl_texturenum,kRenderTransAdd,bound(0,p->unused,255)/255.0f))++fx->tracers;
        }
        VectorMA(p->org,dt,p->vel,p->org);
        if(p->type==pt_grav) {
            p->vel[0]*=drag;p->vel[1]*=drag;p->vel[2]-=gravity;
            p->unused=bound(0,255*(p->die-view->time_seconds)*2,255);
        } else if(p->type==pt_slowgrav)p->vel[2]=gravity*.05f;
    }
}
static void RefAgcCaptureEffects(const ref_client_t *client)
{
    if(!client||!(ref_agc_live.building.view.flags&RF_DRAW_WORLD)||
       ref_agc_effect_serial==ref_agc_begin_calls)return;
    ref_agc_effect_serial=ref_agc_begin_calls;
    RefAgcCaptureDecals();
    const ref_host_t *host=(const ref_host_t *)ref_agc_engine.EngineGetParm(PARM_GET_HOST_PTR,0);
    double dt=client->paused||!host?0:host->frametime;
    if(!isfinite(dt)||dt<0)dt=0;
    ref_agc_engine.CL_DrawEFX(dt,true);
}
