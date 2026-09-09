#ifndef PS5_XASH3D_REF_AGC_SKYBOX_H
#define PS5_XASH3D_REF_AGC_SKYBOX_H

#include "bsp_flat_draw.h"
#include "bsp_bundle.h"
#include "ps5_transient_ring.h"
#include "ref_agc_gpu_texture_cache.h"
#include "ref_agc_live_frame.h"

#include <stddef.h>
#include <stdint.h>

enum {
    REF_AGC_SKYBOX_SIDES = REF_AGC_LIVE_SKY_SIDES,
    REF_AGC_SKYBOX_VERTICES = REF_AGC_SKYBOX_SIDES * 4,
    REF_AGC_SKYBOX_INDICES = REF_AGC_SKYBOX_SIDES * 6,
};

enum RefAgcSkyboxResult {
    REF_AGC_SKYBOX_OK = 0,
    REF_AGC_SKYBOX_INACTIVE = 1,
    REF_AGC_SKYBOX_INVALID = -1,
    REF_AGC_SKYBOX_TRANSIENT_EXHAUSTED = -2,
    REF_AGC_SKYBOX_TEXTURE_UNRESOLVED = -3,
    REF_AGC_SKYBOX_TEXTURE_TABLE_FAILED = -4,
    REF_AGC_SKYBOX_VERTEX_DESCRIPTOR_FAILED = -5,
};

typedef struct RefAgcSkyboxFrame {
    const uint32_t *vertex_table;
    const uint32_t *texture_tables[REF_AGC_SKYBOX_SIDES];
    const BspBundleVertex *vertices;
    const uint16_t *indices;
    uint32_t texture_handles[REF_AGC_SKYBOX_SIDES];
    uint64_t sky_revision;
    uint64_t geometry_hash;
    uint64_t texture_hash;
    size_t transient_bytes;
    uint32_t active;
} RefAgcSkyboxFrame;

typedef struct RefAgcSkyboxComposeResult {
    uint32_t draws;
    uint32_t indices;
    uint32_t command_dwords;
} RefAgcSkyboxComposeResult;

int ref_agc_skybox_frame_build(
    RefAgcSkyboxFrame *out, Ps5TransientRing *ring,
    uint32_t slot_index, const void *gpu_mapping,
    size_t gpu_mapping_bytes, const RefAgcLiveSky *sky,
    const RefAgcGpuTextureCache *textures);

int ref_agc_skybox_compose(
    uint32_t **cursor, uint32_t *end, const RefAgcSkyboxFrame *frame,
    const uint32_t *constant_table, const void *gpu_mapping,
    size_t gpu_mapping_bytes, uint64_t modifier,
    BspSetShDirectFn set_sh_direct, BspDrawIndexedFn draw_indexed,
    RefAgcSkyboxComposeResult *result);

#endif
