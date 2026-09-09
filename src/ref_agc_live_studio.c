#include "ref_agc_live_studio.h"
#include "bsp_flat_scene.h"
#include "ps5_gfx1013_descriptor.h"
#include "ps5_gpu_span.h"
#include "ps5_transient_table.h"
#include <math.h>
#include <string.h>
#include <stddef.h>

_Static_assert(sizeof(BspBundleVertex)==32 && offsetof(BspBundleVertex,face_id)==28,
    "Studio packed vertex light must match GoldSrc surface attribute 3");
_Static_assert(offsetof(BspResourceConstants,debug_values)+11*sizeof(float)==124,
    "Studio lighting opt-in must match draw_state.debug_values[1].w");

/* Read the engine-decoded v10 layout, never cast unaligned file structures.
 * Texture index is already an engine texture handle, not a file offset. */
typedef struct ModelBytes { const uint8_t *p; size_t n; int bad; } ModelBytes;
static int span(ModelBytes *m, size_t at, size_t n)
{ if (at > m->n || n > m->n-at) { m->bad=1; return 0; } return 1; }
static int32_t i32(ModelBytes *m, size_t at)
{ int32_t v=0; if(span(m,at,4)) memcpy(&v,m->p+at,4); return v; }
static int16_t i16(ModelBytes *m, size_t at)
{ int16_t v=0; if(span(m,at,2)) memcpy(&v,m->p+at,2); return v; }
static float f32(ModelBytes *m, size_t at)
{ float v=0; if(span(m,at,4)) memcpy(&v,m->p+at,4); if(!isfinite(v)) m->bad=1; return v; }
static uint64_t hash(uint64_t h, const void *p, size_t n)
{ const uint8_t *b=p; while(n--) { h^=*b++; h*=UINT64_C(1099511628211); } return h; }
static void chrome_cross(const float a[3],const float b[3],float out[3])
{ out[0]=a[1]*b[2]-a[2]*b[1];out[1]=a[2]*b[0]-a[0]*b[2];out[2]=a[0]*b[1]-a[1]*b[0]; }
static int chrome_normalize(float v[3])
{
    float length=sqrtf(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);
    if(!isfinite(length)) return -1;
    if(length>0) for(int k=0;k<3;++k) v[k]/=length;
    return 0;
}
/* R_StudioSetupChrome from the pinned GL path. Dotting world-normal with
 * world axes equals dotting model-normal with inverse-rotated bone axes.
 * Retain the original normal magnitude, 32-texel scale and zero-vector case. */
static int chrome_uv(const RefAgcLiveView *view,const float bone[3][4],
                     const float normal[3],int width,int height,float uv[2])
{
    float a[3],delta[3],up[3],right[3];
    for(int k=0;k<3;++k) {
        if(!isfinite(view->angles[k])||!isfinite(view->origin[k])) return -1;
        a[k]=view->angles[k]*(3.14159265358979323846f/180.0f);
        delta[k]=bone[k][3]-view->origin[k];
    }
    float sp=sinf(a[0]),sy=sinf(a[1]),cy=cosf(a[1]),sr=sinf(a[2]),cr=cosf(a[2]);
    float view_right[3]={-sr*sp*cy+cr*sy,-sr*sp*sy-cr*cy,-sr*cosf(a[0])};
    if(chrome_normalize(delta)) return -1;
    chrome_cross(delta,view_right,up);
    if(chrome_normalize(up)) return -1;
    chrome_cross(up,delta,right);
    if(chrome_normalize(right)) return -1;
    float s=0,t=0;
    for(int k=0;k<3;++k) { s+=normal[k]*right[k];t+=normal[k]*up[k]; }
    uv[0]=(s+1)*32.0f/width;uv[1]=(t+1)*32.0f/height;
    return isfinite(uv[0])&&isfinite(uv[1]) ? 0 : -1;
}
static void *allocate(Ps5TransientRing *r, uint32_t s, size_t n, size_t align,
                      const void *mapping, size_t bytes)
{
    Ps5TransientSlice v;
    if(ps5_transient_ring_allocate(r,s,n,align,&v) ||
       !ps5_gpu_span_visible(mapping,bytes,v.cpu,n)) return NULL;
    return v.cpu;
}

int ref_agc_live_studio_build(RefAgcLiveStudioFrame *out,
    const RefAgcLiveFrame *live, const RefAgcGpuStudioCache *models,
    const RefAgcGpuTextureCache *textures, Ps5TransientRing *ring,
    uint32_t slot, const void *mapping, size_t mapping_bytes,
    const float camera[3], const float forward[3], float aspect)
{
    if(!out||!live||!models||!textures||!ring||slot>=ring->slot_count) return -1;
    memset(out,0,sizeof(*out));
    size_t checkpoint=ring->slots[slot].used;
    int rc=-2;
    float projection[16];
    if(bsp_flat_camera_matrix(projection,camera,forward,aspect)) return -1;
    out->pose_hash=UINT64_C(14695981039346656037);
    out->normal_hash=UINT64_C(14695981039346656037);
    out->light_hash=UINT64_C(14695981039346656037);
    out->chrome_uv_hash=UINT64_C(14695981039346656037);
    out->light_min=255;
    if(live->viewmodel_valid>1u) return -1;
    for(uint32_t ei=0;ei<live->entity_count+live->viewmodel_valid;++ei) {
        const int viewmodel=ei==live->entity_count;
        const RefAgcLiveEntity *e=viewmodel ? &live->viewmodel : &live->entities[ei];
        if(e->model_type!=REF_AGC_LIVE_MODEL_STUDIO) continue;
        out->failed_entity=e->index;
        RefAgcGpuStudioEntry entry;
        const uint8_t *data;
        if(!e->studio_pose||e->studio_pose>live->studio_pose_count||
           e->studio_pose>REF_AGC_LIVE_MAX_STUDIO_POSES||
           ref_agc_gpu_studio_cache_get(models,e->studio_handle,&entry,&data)) goto failed;
        const RefAgcLiveStudioPose *pose=&live->studio_poses[e->studio_pose-1];
        ModelBytes m={data,entry.source_bytes,0};
        if(!span(&m,0,244)||memcmp(data,"IDST",4)||i32(&m,4)!=10||
           i32(&m,72)!=(int32_t)m.n||pose->bones!=(uint32_t)i32(&m,140)||
           !pose->bones||pose->bones>REF_AGC_LIVE_MAX_STUDIO_BONES) goto failed;
        int textures_n=i32(&m,180), tex_at=i32(&m,184);
        int skins=i32(&m,192), families=i32(&m,196), skin_at=i32(&m,200);
        int parts=i32(&m,204), part_at=i32(&m,208);
        if(textures_n<1||textures_n>512||skins<1||skins>512||families<1||families>256||
           parts<1||parts>32||e->body<0||e->skin<0||e->skin>=families||
           !span(&m,tex_at,(size_t)textures_n*80)||!span(&m,skin_at,(size_t)skins*families*2)||
           !span(&m,part_at,(size_t)parts*76)) goto failed;
        out->pose_hash=hash(out->pose_hash,pose->matrices,pose->bones*sizeof(pose->matrices[0]));
        for(int part=0;part<parts;++part) {
            size_t bp=(size_t)part_at+part*76;
            int count=i32(&m,bp+64), base=i32(&m,bp+68), model_at=i32(&m,bp+72);
            if(count<1||count>256||base<1||!span(&m,model_at,(size_t)count*112)) goto failed;
            size_t sub=(size_t)model_at+((e->body/base)%count)*112;
            int meshes=i32(&m,sub+72), mesh_at=i32(&m,sub+76);
            int nv=i32(&m,sub+80), bone_at=i32(&m,sub+84), vertex_at=i32(&m,sub+88);
            int nn=i32(&m,sub+92), normal_bone_at=i32(&m,sub+96), normal_at=i32(&m,sub+100);
            if(meshes==0) continue; /* empty bodygroup */
            if(meshes<0||meshes>128||nv<1||nv>65535||nn<1||nn>65535||
               !span(&m,mesh_at,(size_t)meshes*20)||!span(&m,bone_at,nv)||
               !span(&m,vertex_at,(size_t)nv*12)||!span(&m,normal_bone_at,nn)||
               !span(&m,normal_at,(size_t)nn*12)) goto failed;
            for(int mesh=0;mesh<meshes;++mesh) {
                size_t me=(size_t)mesh_at+mesh*20;
                int triangles=i32(&m,me), tri_at=i32(&m,me+4), skin=i32(&m,me+8);
                if(triangles==0) continue;
                if(triangles<0||triangles>21845||skin<0||skin>=skins||out->count>=REF_AGC_STUDIO_DRAW_MAX) goto failed;
                int tex=i16(&m,(size_t)skin_at+2*((size_t)e->skin*skins+skin));
                if(tex<0||tex>=textures_n) goto failed;
                size_t tx=(size_t)tex_at+tex*80;
                int flags=i16(&m,tx+64), width=i32(&m,tx+68), height=i32(&m,tx+72), handle=i32(&m,tx+76);
                if(flags&2) ++out->chrome_draws;
                RefAgcGpuTextureEntry texture;
                if(width<1||height<1||handle<1||ref_agc_gpu_texture_cache_get(textures,handle,&texture)) goto failed;
                uint32_t vertices=0, indices=0;
                size_t cursor=(size_t)tri_at;
                for(int cmd=0;cmd<=triangles;++cmd) {
                    int n=i16(&m,cursor); cursor+=2;
                    if(m.bad) goto failed;
                    if(!n) break;
                    if(n<0) n=-n;
                    if(n<3||vertices+(uint32_t)n>65535||indices+3u*(n-2)>3u*(uint32_t)triangles||
                       !span(&m,cursor,(size_t)n*8)) goto failed;
                    vertices+=n; indices+=3*(n-2); cursor+=(size_t)n*8;
                }
                if(indices!=3u*(uint32_t)triangles||!vertices||m.bad) goto failed;
                BspBundleVertex *v=allocate(ring,slot,vertices*sizeof(*v),16,mapping,mapping_bytes);
                uint16_t *ix=allocate(ring,slot,indices*sizeof(*ix),2,mapping,mapping_bytes);
                BspResourceConstants *constants=allocate(ring,slot,sizeof(*constants),256,mapping,mapping_bytes);
                Ps5TransientTable ct,vt,tt;
                rc=-3;
                if(!v||!ix||!constants||
                   ps5_transient_table_allocate(ring,slot,4,mapping,mapping_bytes,&ct)||
                   ps5_transient_table_allocate(ring,slot,4,mapping,mapping_bytes,&vt)||
                   ps5_transient_table_allocate(ring,slot,12,mapping,mapping_bytes,&tt)) goto failed;
                rc=-2;
                memset(constants,0,sizeof(*constants)); memcpy(constants->mvp,projection,sizeof(projection));
                /* D3D [0,1] clip depth: equivalent to upstream glDepthRange
                 * (0,0.3). Local matrix change cannot leak into later HUD. */
                if(viewmodel)
                    for(unsigned column=0;column<4;++column)
                        constants->mvp[column*4+2]*=0.3f;
                constants->control[0]=constants->control[1]=constants->control[2]=1;
                constants->control[3]=e->render_mode==0?1:e->render_amount/255.0f;
                /* GoldSrc surface VS: explicit Studio vertex-light opt-in.
                 * debug_values[11] aliases draw_state.debug_values[1].w. */
                constants->debug_values[11]=-1.0f;
                memcpy(tt.words,texture.descriptor,sizeof(texture.descriptor));
                if(ps5_gfx1013_build_constant_vsharp(ct.words,(uintptr_t)constants,sizeof(*constants))||
                   ps5_gfx1013_build_vsharp(vt.words,(uintptr_t)v,sizeof(*v),vertices)) goto failed;
                uint32_t vi=0,ii=0; cursor=(size_t)tri_at;
                while(1) {
                    int command=i16(&m,cursor); cursor+=2;
                    if(!command) break;
                    int n=command<0?-command:command; uint32_t first=vi;
                    for(int j=0;j<n;++j) {
                        int source=i16(&m,cursor), normal=i16(&m,cursor+2);
                        if(source<0||source>=nv||normal<0||normal>=nn||vi>=vertices) goto failed;
                        unsigned bone=data[(size_t)bone_at+source];
                        unsigned normal_bone=data[(size_t)normal_bone_at+normal];
                        if(bone>=pose->bones||normal_bone>=pose->bones) goto failed;
                        /* Studio stores a separate bone index for each normal.
                         * Preserve model-space magnitude for the upstream
                         * lighting formula; translation must never enter a
                         * direction. Pose matrices are the engine's rigid-bone
                         * transforms, not a general inverse-transpose API. */
                        float model_normal[3], world_normal[3]={0};
                        for(int k=0;k<3;++k)
                            model_normal[k]=f32(&m,(size_t)normal_at+normal*12+k*4);
                        for(int k=0;k<3;++k) {
                            for(int l=0;l<3;++l)
                                world_normal[k]+=pose->matrices[normal_bone][k][l]*model_normal[l];
                            if(!isfinite(world_normal[k])) goto failed;
                        }
                        if(m.bad) goto failed;
                        out->normal_hash=hash(out->normal_hash,world_normal,sizeof(world_normal));
                        ++out->normals;
                        float p[3],world[3];
                        for(int k=0;k<3;++k) p[k]=f32(&m,(size_t)vertex_at+source*12+k*4);
                        for(int k=0;k<3;++k) {
                            world[k]=pose->matrices[bone][k][3];
                            for(int l=0;l<3;++l) world[k]+=pose->matrices[bone][k][l]*p[l];
                            if(!isfinite(world[k])) goto failed;
                        }
                        v[vi]=(BspBundleVertex){.position={world[0],world[2],-world[1]},
                            .base_uv={i16(&m,cursor+4)/(float)width,i16(&m,cursor+6)/(float)height}};
                        if(flags&2) {
                            if(chrome_uv(&live->view,pose->matrices[normal_bone],
                                world_normal,width,height,v[vi].base_uv)) goto failed;
                            ++out->chrome_vertices;
                            out->chrome_uv_hash=hash(out->chrome_uv_hash,v[vi].base_uv,sizeof(v[vi].base_uv));
                        }
                        if(ref_agc_studio_vertex_light(&pose->lighting,
                            live->studio_light_gamma,world_normal,(uint32_t)flags,
                            &v[vi].face_id)) goto failed;
                        /* CPU-only diagnostic: leave every shader/constant,
                         * vertex position, UV, alpha and index unchanged. */
                        if(live->view.sampling_probe_mode==4u)
                            v[vi].face_id=UINT32_C(0xffffffff);
                        out->light_hash=hash(out->light_hash,&v[vi].face_id,4);
                        for(unsigned channel=0;channel<3;++channel) {
                            unsigned value=(v[vi].face_id>>(channel*8))&255;
                            if(value<out->light_min) out->light_min=value;
                            if(value>out->light_max) out->light_max=value;
                        }
                        ++vi; cursor+=8;
                    }
                    for(int j=2;j<n;++j) {
                        if(ii+3>indices) goto failed;
                        ix[ii++]=first+(command<0?0:(j%2?j-1:j-2));
                        ix[ii++]=first+(command<0?j-1:(j%2?j-2:j-1));
                        ix[ii++]=first+j;
                    }
                }
                if(m.bad||ii!=indices||vi!=vertices) goto failed;
                out->draws[out->count++]=(RefAgcLiveStudioDraw){ct.words,vt.words,tt.words,ix,indices,(uint32_t)handle,viewmodel?UINT32_MAX:ei,(uint32_t)flags};
                if(viewmodel) { ++out->viewmodel_draws;out->viewmodel_vertices+=vertices; }
                out->vertices+=vertices; out->indices+=indices;
            }
        }
        ++out->entities;
    }
    out->failed_entity=-1;
    return 0;
failed:
    ring->slots[slot].used=checkpoint;
    out->count=0;
    out->normals=0;
    out->normal_hash=0;
    out->light_hash=0;
    out->chrome_draws=out->chrome_vertices=0;
    out->chrome_uv_hash=0;
    out->viewmodel_draws=out->viewmodel_vertices=0;
    return rc;
}

int ref_agc_live_studio_compose(uint32_t **cursor, uint32_t *end,
    const RefAgcLiveStudioDraw *d, const void *mapping, size_t bytes,
    uint64_t modifier, BspSetShDirectFn set_sh, BspDrawIndexedFn draw_indexed)
{
    if(!cursor||!*cursor||!d||!set_sh||!draw_indexed||end<*cursor||end-*cursor<32||
       !ps5_gpu_span_visible(mapping,bytes,d->constants,16)||
       !ps5_gpu_span_visible(mapping,bytes,d->vertices,16)||
       !ps5_gpu_span_visible(mapping,bytes,d->texture,48)||
       !ps5_gpu_span_visible(mapping,bytes,d->indices,d->count*sizeof(uint16_t))) return -1;
    uint32_t gs[2]={(uint32_t)(uintptr_t)d->constants,(uint32_t)(uintptr_t)d->vertices};
    uint32_t ps=(uint32_t)(uintptr_t)d->texture;
    return set_sh(cursor,(uint32_t)(end-*cursor),BSP_RESOURCE_GS_SH_OFFSET,gs,2)||
           set_sh(cursor,(uint32_t)(end-*cursor),BSP_RESOURCE_PS_SH_OFFSET,&ps,1)||
           draw_indexed(cursor,(uint32_t)(end-*cursor),d->count,d->indices,mapping,bytes,modifier)?-1:0;
}
