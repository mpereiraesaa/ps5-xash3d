#ifndef REF_AGC_LIVE_STUDIO_H
#define REF_AGC_LIVE_STUDIO_H
#include "ref_agc_live_frame.h"
#include "ref_agc_gpu_studio_cache.h"
#include "ref_agc_gpu_texture_cache.h"
#include "bsp_resource_draw.h"
#include "ps5_transient_ring.h"

enum { REF_AGC_STUDIO_DRAW_MAX = 512 };
typedef struct RefAgcLiveStudioDraw {
    const uint32_t *constants, *vertices, *texture;
    const uint16_t *indices;
    uint32_t count, texture_handle, entity, flags;
} RefAgcLiveStudioDraw;
typedef struct RefAgcLiveStudioFrame {
    RefAgcLiveStudioDraw draws[REF_AGC_STUDIO_DRAW_MAX];
    uint32_t count, entities, vertices, indices;
    int32_t failed_entity;
    uint64_t pose_hash;
} RefAgcLiveStudioFrame;

int ref_agc_live_studio_build(RefAgcLiveStudioFrame *out,
    const RefAgcLiveFrame *live, const RefAgcGpuStudioCache *models,
    const RefAgcGpuTextureCache *textures, Ps5TransientRing *ring,
    uint32_t slot, const void *mapping, size_t mapping_bytes,
    const float camera[3], const float forward[3], float aspect);
int ref_agc_live_studio_compose(uint32_t **cursor, uint32_t *end,
    const RefAgcLiveStudioDraw *draw, const void *mapping, size_t bytes,
    uint64_t modifier, BspSetShDirectFn set_sh, BspDrawIndexedFn draw_indexed);
#endif
