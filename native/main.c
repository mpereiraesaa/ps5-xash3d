#include "../include/ps5_agc.h"
#include "../include/ps5_agc_driver.h"
#include "../include/ps5_platform.h"
#include "../src/gears_frame_runner.h"
#include "../src/gears_mesh.h"
#include "../src/gears_renderer.h"
#include "../src/gears_rt_clear.h"
#include "../src/goldsrc_pipeline_cache.h"
#include "../src/goldsrc_shader_catalog.h"
#include "../src/goldsrc_state_matrix.h"
#include "../src/goldsrc_2d.h"
#include "../src/goldsrc_lightmap_lighting.h"
#include "../src/goldsrc_sprite_particles.h"
#include "../src/goldsrc_studio_bundle.h"
#include "../src/goldsrc_studio_model.h"
#include "../src/goldsrc_brush_entities.h"
#include "../src/goldsrc_visibility.h"
#include "../src/bsp_bundle.h"
#include "../src/bsp_command_plan.h"
#include "../src/bsp_flat_scene.h"
#include "../src/bsp_noclip.h"
#include "../src/bsp_runtime_plan.h"
#include "../src/bsp_texture_descriptor.h"
#include "../src/bsp_textured_draw.h"
#include "../src/bsp_resource_draw.h"
#include "../src/bsp_resource_frame.h"
#include "../src/bsp_dynamic_lightmap.h"
#include "../src/bsp_alpha_test.h"
#include "../src/bsp_sky.h"
#include "../src/bsp_texture_accounting.h"
#include "../src/ps5_agc_submit.h"
#include "../src/ps5_cache_contract.h"
#include "../src/ps5_color_target.h"
#include "../src/ps5_depth_target.h"
#include "../src/ps5_event_adapter.h"
#include "../src/ps5_gfx1013_descriptor.h"
#include "../src/ps5_gpu_flip_timing.h"
#include "../src/ps5_pipeline.h"
#include "../src/ps5_resource_pool.h"
#include "../src/ps5_shader_header.h"
#include "../src/ps5_shader_pipeline_slot.h"
#include "../src/ps5_goldsrc_pipeline_runtime.h"
#include "../src/ps5_submission.h"
#include "../src/ps5_transient_ring.h"
#include "../src/ps5_surface.h"
#include "../src/ps5_videoout.h"
#include "../src/ps5_viewport_scissor.h"
#include "ps5_agc_native.h"
#include "ps5log/ps5log.h"
#include "gears_shader_metadata.h"

#define PS5_XASH3D_TITLE_ID "PPSA99996"
#define PS5_XASH3D_APP_NAME "ps5-xash3d"
#define PS5_LIVE_CAMERA_SETTLE_NS 10000000L
#define PS5_LIVE_CAMERA_SETTLE_TELEMETRY " live_camera_settle_ns=10000000"

#ifdef PS5_BSP_VIEWER
#include "bsp_build_metadata.h"
#include "bsp_flat_shader_metadata.h"
#ifdef PS5_BSP_TEXTURED
#include "bsp_textured_shader_metadata.h"
#ifdef PS5_RESOURCE_FOUNDATION
#include "bsp_resource_shader_metadata.h"
#include "bsp_alpha_test_shader_metadata.h"

#include "bsp_sky_shader_metadata.h"
#include "bsp_overlay_shader_metadata.h"
#include "pipeline_permutations.h"
#ifdef PS5_GOLDSRC_PHASE4
#include "goldsrc_shader_catalog_generated.h"
#ifdef PS5_GOLDSRC_STUDIO_GATE
#include "studio_build_metadata.h"
#endif
#endif
#endif
#endif
#endif

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <sys/stat.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#ifdef PS5_REF_AGC_MODULE
#include <pthread.h>
#include "ref_agc_live_frame.h"
#include "ref_agc_gpu_texture_cache.h"
#include "ref_agc_gpu_world_cache.h"
#include "ref_agc_gpu_world_draw.h"

void PS5_RefAgcRuntimeReady(void);
void PS5_RefAgcRuntimeComplete(uint64_t frames, uint64_t frame_hash,
                               uint64_t bright_pixels, int teardown_result);
void PS5_RefAgcRuntimeFailed(int result);
void PS5_RefAgcRuntimeLiveStats(uint64_t consumed_frames,
                               uint64_t consumed_serial,
                               uint64_t consumed_view_frames,
                               uint64_t camera_hash,
                               uint64_t camera_changes);
int PS5_RefAgcWaitLiveFrame(uint64_t after_serial, RefAgcLiveFrame *out);
int PS5_RefAgcConsumeLiveFrame(uint64_t serial);
int PS5_RefAgcVisitTextures(uint64_t after_revision,
                            RefAgcTextureVisitor visitor, void *user,
                            uint64_t *out_revision);
int PS5_RefAgcVisitWorld(uint64_t after_revision,
                         RefAgcWorldVisitor visitor, void *user,
                         uint64_t *out_revision);
#endif

#ifdef PS5_REF_AGC_LIVE_PHASE7
static int ps5_live_camera_settle(void)
{
    struct timespec delay = {
        .tv_sec = 0,
        .tv_nsec = PS5_LIVE_CAMERA_SETTLE_NS,
    };
    while (nanosleep(&delay, &delay) != 0) {
        if (errno != EINTR)
            return -1;
    }
    return 0;
}
#endif

#if defined(PS5_TEXTURE_ACCOUNTING_GATE) || \
    defined(PS5_TEXTURE_FINAL_GATE)
#define PS5_TEXTURE_ACCOUNTING_ENABLED 1
#endif

extern const uint8_t ps5_gears_gs_start[], ps5_gears_gs_end[];
extern const uint8_t ps5_gears_ps_start[], ps5_gears_ps_end[];
#ifdef PS5_BSP_VIEWER
extern const uint8_t ps5_bsp_flat_gs_start[], ps5_bsp_flat_gs_end[];
extern const uint8_t ps5_bsp_flat_ps_start[], ps5_bsp_flat_ps_end[];
#ifdef PS5_BSP_TEXTURED
extern const uint8_t ps5_bsp_textured_gs_start[], ps5_bsp_textured_gs_end[];
extern const uint8_t ps5_bsp_textured_ps_start[], ps5_bsp_textured_ps_end[];
#ifdef PS5_RESOURCE_FOUNDATION
extern const uint8_t ps5_bsp_resource_gs_start[], ps5_bsp_resource_gs_end[];
extern const uint8_t ps5_bsp_resource_ps_start[], ps5_bsp_resource_ps_end[];
extern const uint8_t ps5_bsp_alpha_test_gs_start[];
extern const uint8_t ps5_bsp_alpha_test_gs_end[];
extern const uint8_t ps5_bsp_alpha_test_ps_start[];
extern const uint8_t ps5_bsp_alpha_test_ps_end[];
extern const uint8_t ps5_bsp_sky_gs_start[], ps5_bsp_sky_gs_end[];
extern const uint8_t ps5_bsp_sky_ps_start[], ps5_bsp_sky_ps_end[];
extern const uint8_t ps5_bsp_overlay_gs_start[], ps5_bsp_overlay_gs_end[];
extern const uint8_t ps5_bsp_overlay_ps_start[], ps5_bsp_overlay_ps_end[];
#endif
#endif
#endif

enum {
    AGC_MODULE = 0x80000094u,
    COMMAND_BYTES = 0x20000u,
    COMMAND_ALIGNMENT = 0x10000u,
    COMMAND_SLOT_BYTES = 0x1000u,
    FENCE0_OFFSET = 0x1100u,
    FENCE1_OFFSET = 0x1200u,
    SHADER_BYTES = 0x40000u,
    SHADER_ALIGNMENT = 0x4000u,
    GS_HEADER_OFFSET = 0x0000u,
    PS_HEADER_OFFSET = 0x0200u,
    GS_CODE_OFFSET = 0x1000u,
    PS_CODE_OFFSET = 0x1200u,
    LINKED_CX_OFFSET = 0x2000u,
    LINKED_UC_OFFSET = 0x2200u,
    PIPELINE_OFFSET = 0x3000u,
    DEPTH_REGISTERS_OFFSET = 0x3e00u,
#ifdef PS5_RESOURCE_FOUNDATION
    OVERLAY_GS_HEADER_OFFSET = 0x4000u,
    OVERLAY_PS_HEADER_OFFSET = 0x4200u,
    OVERLAY_GS_CODE_OFFSET = 0x5000u,
    OVERLAY_PS_CODE_OFFSET = 0x5200u,
    OVERLAY_LINKED_CX_OFFSET = 0x6000u,
    OVERLAY_LINKED_UC_OFFSET = 0x6200u,
    OVERLAY_PIPELINE_OFFSET = 0x7000u,
    OVERLAY_DEPTH_DISABLED_OFFSET = 0x7800u,
    ALPHA_GS_HEADER_OFFSET = 0x8000u,
    ALPHA_PS_HEADER_OFFSET = 0x8200u,
    ALPHA_GS_CODE_OFFSET = 0x9000u,
    ALPHA_PS_CODE_OFFSET = 0x9200u,
    ALPHA_LINKED_CX_OFFSET = 0xa000u,
    ALPHA_LINKED_UC_OFFSET = 0xa200u,
    ALPHA_PIPELINE_OFFSET = 0xb000u,
    SKY_GS_HEADER_OFFSET = 0xc000u,
    SKY_PS_HEADER_OFFSET = 0xc200u,
    SKY_GS_CODE_OFFSET = 0xd000u,
    SKY_PS_CODE_OFFSET = 0xd200u,
    SKY_LINKED_CX_OFFSET = 0xe000u,
    SKY_LINKED_UC_OFFSET = 0xe200u,
    SKY_PIPELINE_OFFSET = 0xf000u,
#ifdef PS5_GOLDSRC_PHASE4
    GOLDSRC_SHADER_SLOTS_OFFSET = 0x10000u,
    GOLDSRC_STATE_REGISTERS_OFFSET = 0x34000u,
#ifdef PS5_GOLDSRC_VIEWPORT_GATE
    GOLDSRC_VIEWPORT_INSET_OFFSET = 0x35000u,
    GOLDSRC_VIEWPORT_FULL_OFFSET = 0x35040u,
#endif
#endif
    RESOURCE_TRANSIENT_BYTES = 0x40000u,
    RESOURCE_HEAP_ALIGNMENT = 0x10000u,
#ifdef PS5_REF_AGC_LIVE_PHASE7
    REF_AGC_GPU_TEXTURE_ARENA_BYTES = 64u * 1024u * 1024u,
    REF_AGC_GPU_WORLD_ARENA_BYTES = 32u * 1024u * 1024u,
#endif
#endif
    GEAR0_OFFSET = 0x4000u,
    GEAR1_OFFSET = 0x10000u,
    GEAR2_OFFSET = 0x16000u,
    GEAR_SRD_OFFSET = 0x1c000u,
    CLEAR_SRD_OFFSET = 0x1c100u,
    CLEAR_VERTEX_OFFSET = 0x1c200u,
    SHADER_FOOTER_BYTES = 0x30u,
    DEPTH_BYTES = 0x870000u,
    DEPTH_ALLOCATION_BYTES = 0x900000u,
    DEPTH_ALIGNMENT = 0x10000u,
    GUARD_WORD = 0x51a6c3d9u,
    BSP_FIXED_COMMAND_DWORDS = 4096u,
    BSP_MAX_BUNDLE_BYTES = 64u * 1024u * 1024u,
    STUDIO_MAX_BUNDLE_BYTES = 16u * 1024u * 1024u,
#ifdef PS5_REF_AGC_MODULE
    /* The PRX integration gate proves ten seconds of live ownership.  The
     * standalone Phase 4 soak remains the 60,000-frame durability evidence. */
    BSP_GATE_FRAME_COUNT = 600u,
#elif defined(PS5_GOLDSRC_PHASE4_FINAL_GATE)
    BSP_GATE_FRAME_COUNT = 60000u,
    BSP_NOCLIP_MIN_MOVING_FRAMES = 600u,
    BSP_NOCLIP_MIN_LOOKING_FRAMES = 120u,
#elif defined(PS5_TEXTURE_FINAL_GATE)
    BSP_GATE_FRAME_COUNT = 60000u,
    BSP_NOCLIP_MIN_MOVING_FRAMES = 600u,
    BSP_NOCLIP_MIN_LOOKING_FRAMES = 120u,
#elif defined(PS5_TEXTURE_PATH)
    BSP_GATE_FRAME_COUNT = 10000u,
    BSP_NOCLIP_MIN_MOVING_FRAMES = 600u,
    BSP_NOCLIP_MIN_LOOKING_FRAMES = 120u,
#elif defined(PS5_BSP_TEXTURED)
    BSP_GATE_FRAME_COUNT = 60000u,
    BSP_NOCLIP_MIN_MOVING_FRAMES = 600u,
    BSP_NOCLIP_MIN_LOOKING_FRAMES = 120u,
#elif defined(PS5_BSP_NOCLIP)
    BSP_GATE_FRAME_COUNT = 10000u,
    BSP_NOCLIP_MIN_MOVING_FRAMES = 600u,
    BSP_NOCLIP_MIN_LOOKING_FRAMES = 120u,
#else
    BSP_GATE_FRAME_COUNT = 600u,
#endif
};

#ifdef PS5_REF_AGC_MODULE
#define PS5_REF_AGC_FEATURE_FRAME(index) ((uint64_t)(index) + UINT64_C(59400))
#else
#define PS5_REF_AGC_FEATURE_FRAME(index) ((uint64_t)(index))
#endif

#ifdef PS5_REF_AGC_LIVE_PHASE7
#define PS5_BSP_FINAL_WINDOW(index) 0
#else
#define PS5_BSP_FINAL_WINDOW(index) \
    ((uint64_t)(index) + 2u >= BSP_GATE_FRAME_COUNT)
#endif

struct native_resources {
    uint64_t agc_state;
    void *command;
    int64_t command_offset;
    void *framebuffer;
    int64_t framebuffer_offset;
    void *shader;
    int64_t shader_offset;
    void *depth;
    int64_t depth_offset;
    void *bsp;
    int64_t bsp_offset;
#ifdef PS5_GOLDSRC_STUDIO_GATE
    void *studio;
    size_t studio_bytes;
    size_t studio_offset;
#endif
#ifdef PS5_RESOURCE_FOUNDATION
    void *resource_heap;
    int64_t resource_heap_offset;
    size_t resource_heap_bytes;
    void *transient;
    Ps5ResourcePool resource_pool;
    Ps5ResourceAllocation bsp_allocation;
    Ps5ResourceAllocation shader_allocation;
    Ps5ResourceAllocation depth_allocation;
    Ps5ResourceAllocation transient_allocation;
#ifdef PS5_REF_AGC_LIVE_PHASE7
    void *live_texture_arena;
    Ps5ResourceAllocation live_texture_allocation;
    void *live_world_arena;
    Ps5ResourceAllocation live_world_allocation;
#endif
#ifdef PS5_TEXTURE_PATH
    void *dynamic_lightmaps[2];
    Ps5ResourceAllocation dynamic_lightmap_allocations[2];
#endif
#endif
    size_t command_bytes;
    size_t bsp_bytes;
    int32_t pad_handle;
    struct ps5_videoout video;
    struct ps5_surface_plan surface;
    int agc_loaded;
    int command_reserved;
    int command_allocated;
    int command_mapped;
    int framebuffer_allocated;
    int framebuffer_mapped;
    int shader_allocated;
    int shader_mapped;
    int depth_allocated;
    int depth_mapped;
    int bsp_allocated;
    int bsp_mapped;
#ifdef PS5_RESOURCE_FOUNDATION
    int resource_heap_allocated;
    int resource_heap_mapped;
#endif
    int pad_opened;
};

struct native_renderer {
    uint32_t *commands[2];
    uint32_t *cursor[2];
    volatile uint64_t *fences[2];
#ifdef PS5_GPU_FLIP_TIMING_GATE
    volatile uint64_t *gpu_eop_timestamps[2];
    uint64_t gpu_flip_submit_ns[2];
    uint64_t gpu_flip_gpu_observed_ns[2];
    Ps5GpuFlipTimingAccumulator gpu_flip_timing;
#endif
    struct ps5_pipeline_registers *pipelines[2];
#ifdef PS5_RESOURCE_FOUNDATION
    struct ps5_pipeline_registers *overlay_pipelines[2];
    struct ps5_pipeline_registers *alpha_test_pipelines[2];
    struct ps5_pipeline_registers *sky_pipelines[2];
#ifdef PS5_GOLDSRC_PHASE4
    Ps5ShaderPipelineSlotResult
        goldsrc_shader_slots[GOLDSRC_SHADER_VARIANT_COUNT];
    GoldSrcPipelineCache goldsrc_pipeline_cache;
    Ps5GoldSrcPipelineRuntime goldsrc_pipeline_runtime;
#ifdef PS5_GOLDSRC_STATE_MATRIX_GATE
    uint64_t goldsrc_matrix_hashes[GOLDSRC_STATE_MATRIX_CASE_COUNT][2];
    uint64_t goldsrc_matrix_bright[GOLDSRC_STATE_MATRIX_CASE_COUNT][2];
    uint8_t goldsrc_matrix_seen[GOLDSRC_STATE_MATRIX_CASE_COUNT][2];
#endif
#ifdef PS5_GOLDSRC_2D_GATE
    GoldSrc2DFrame goldsrc_2d_frames[2];
#endif
#ifdef PS5_GOLDSRC_LIGHTING_GATE
    GoldSrcLightmapLightingPlan goldsrc_lighting_plan;
    GoldSrcLightmapLightingUpdate goldsrc_lighting_updates[2];
    uint64_t goldsrc_lighting_hashes[GOLDSRC_LIGHTING_MODE_COUNT][2];
    uint64_t goldsrc_lighting_bright[GOLDSRC_LIGHTING_MODE_COUNT][2];
    uint8_t goldsrc_lighting_seen[GOLDSRC_LIGHTING_MODE_COUNT][2];
#endif
#ifdef PS5_GOLDSRC_SPRITE_PARTICLE_GATE
    GoldSrcSpriteParticleFrame goldsrc_effect_frames[2];
    uint64_t goldsrc_effect_hashes[GOLDSRC_EFFECT_MODE_COUNT][2];
    uint64_t goldsrc_effect_bright[GOLDSRC_EFFECT_MODE_COUNT][2];
    uint8_t goldsrc_effect_seen[GOLDSRC_EFFECT_MODE_COUNT][2];
#endif
#ifdef PS5_GOLDSRC_STUDIO_GATE
    GoldSrcStudioBundleView goldsrc_studio_bundle;
    GoldSrcStudioFrame goldsrc_studio_frames[2];
    uint64_t goldsrc_studio_hashes[GOLDSRC_STUDIO_MODE_COUNT][2];
    uint64_t goldsrc_studio_bright[GOLDSRC_STUDIO_MODE_COUNT][2];
    uint8_t goldsrc_studio_seen[GOLDSRC_STUDIO_MODE_COUNT][2];
    uint64_t goldsrc_studio_first_pose_hash;
    uint64_t goldsrc_studio_last_pose_hash;
#endif
#ifdef PS5_GOLDSRC_BRUSH_GATE
    GoldSrcBrushPlan goldsrc_brush_plan;
    GoldSrcBrushFrame goldsrc_brush_frames[2];
    uint64_t goldsrc_brush_hashes[GOLDSRC_BRUSH_MODE_COUNT][2];
    uint64_t goldsrc_brush_bright[GOLDSRC_BRUSH_MODE_COUNT][2];
    uint8_t goldsrc_brush_seen[GOLDSRC_BRUSH_MODE_COUNT][2];
    uint64_t goldsrc_brush_first_transform_hash;
    uint64_t goldsrc_brush_last_transform_hash;
#endif
#ifdef PS5_GOLDSRC_VISIBILITY_GATE
    GoldSrcVisibilityPlan goldsrc_visibility_plan;
    GoldSrcVisibilityFrame goldsrc_visibility_frames[2];
    uint64_t goldsrc_visibility_hashes[GOLDSRC_VISIBILITY_MODE_COUNT][2];
    uint64_t goldsrc_visibility_bright[GOLDSRC_VISIBILITY_MODE_COUNT][2];
    uint8_t goldsrc_visibility_seen[GOLDSRC_VISIBILITY_MODE_COUNT][2];
    uint32_t goldsrc_visibility_draw_counts[GOLDSRC_VISIBILITY_MODE_COUNT];
    uint32_t goldsrc_visibility_leaf_counts[GOLDSRC_VISIBILITY_MODE_COUNT];
    uint8_t goldsrc_visibility_counts_seen[GOLDSRC_VISIBILITY_MODE_COUNT];
#endif
#ifdef PS5_GOLDSRC_PHASE4_FINAL_GATE
    uint64_t goldsrc_phase4_final_hashes[2];
    uint64_t goldsrc_phase4_final_bright[2];
    uint8_t goldsrc_phase4_final_seen[2];
#endif
#ifdef PS5_GOLDSRC_VIEWPORT_GATE
    ps5_agc_register *goldsrc_viewport_inset;
    ps5_agc_register *goldsrc_viewport_full;
#endif
#endif
    BspAlphaTestPlan alpha_test_plan;
    BspSkyPlan sky_plan;
    Ps5TransientRing transient_ring;
    BspResourceFrame resource_frames[2];
    Ps5CpuToGpuPlan cache_plans[2];
#ifdef PS5_TEXTURE_PATH
    BspDynamicLightmapLayout dynamic_lightmap_layout;
    BspDynamicLightmapSlot dynamic_lightmap_slots[2];
    BspDynamicLightmapUpdate dynamic_lightmap_updates[2];
    Ps5CpuToGpuPlan dynamic_lightmap_cache_plans[2];
    BspTextureAccounting texture_accounting;
#endif
    uint64_t completed_tokens[2];
    uint64_t last_completed_token;
    uint64_t overlay_draw_modifier;
    ps5_agc_register *overlay_depth_disabled;
    BspBundleVertex *clear_vertices;
    uint16_t *clear_indices;
#endif
    ps5_agc_register *depth_registers;
    GearsSceneDraw clear_draw;
    uint32_t srd_tables[GEARS_SCENE_DRAW_COUNT];
    uint32_t vertex_counts[GEARS_SCENE_DRAW_COUNT];
    uint64_t draw_modifier;
    size_t command_slot_bytes;
#ifdef PS5_BSP_VIEWER
    BspBundleView bsp_bundle;
    BspRuntimePlan bsp_plan;
    BspFlatDraw *bsp_draws;
    uint32_t bsp_draw_count;
#ifdef PS5_BSP_TEXTURED
    const uint32_t **bsp_texture_bindings;
#endif
#ifdef PS5_BSP_NOCLIP
    BspNoclipCamera noclip;
    float last_camera_time;
    uint64_t pad_read_errors;
#endif
#endif
    struct ps5_agc_submit_context submit;
    struct native_resources *resources;
    int transaction_started;
#ifdef PS5_REF_AGC_LIVE_PHASE7
    RefAgcLiveFrame live_frame;
    const char *live_compose_stage;
    float live_aspect_ratio;
    uint64_t live_consumed_frames;
    uint64_t live_view_frames;
    uint64_t live_last_serial;
    uint64_t live_camera_hash;
    uint64_t live_camera_changes;
    RefAgcGpuTextureCache live_texture_cache;
    uint64_t live_texture_revision;
    RefAgcGpuWorldCache live_world_cache;
    uint64_t live_world_revision;
    Ps5CpuToGpuPlan live_texture_cache_plan;
    Ps5CpuToGpuPlan live_world_cache_plan;
    int live_frame_valid;
#endif
};

static struct native_resources resources;
static struct native_renderer renderer;

#ifdef PS5_REF_AGC_LIVE_PHASE7
struct live_texture_sync_context {
    RefAgcGpuTextureCache *cache;
    int prior_use_retired;
    int cache_result;
};

struct live_world_sync_context {
    RefAgcGpuWorldCache *cache;
    const RefAgcGpuTextureCache *textures;
    int prior_use_retired;
    int cache_result;
};

static int sync_live_texture(const RefAgcTextureView *view, void *user)
{
    struct live_texture_sync_context *context = user;
    context->cache_result = ref_agc_gpu_texture_cache_apply(
        context->cache, view, context->prior_use_retired);
    return context->cache_result == REF_AGC_GPU_TEXTURE_OK ? 0 : -1;
}

static int sync_live_world(const RefAgcWorldView *view, void *user)
{
    struct live_world_sync_context *context = user;
    context->cache_result = ref_agc_gpu_world_cache_apply(
        context->cache, view, context->textures,
        context->prior_use_retired);
    return context->cache_result == REF_AGC_GPU_WORLD_OK ? 0 : -1;
}
#endif

static uint64_t now_ns(void *unused)
{
    struct timespec value = {0, 0};
    (void)unused;
    (void)clock_gettime(CLOCK_MONOTONIC, &value);
    return (uint64_t)value.tv_sec * UINT64_C(1000000000) +
           (uint64_t)value.tv_nsec;
}

static void log_result(const char *name, int result)
{
    (void)ps5log_printf(result == 0 ? PS5LOG_INFO : PS5LOG_ERR,
                        "%s=0x%08x", name, (uint32_t)result);
}

static int allocate_direct(void **address, int64_t *offset, size_t bytes,
                           size_t alignment)
{
    int result = sceKernelAllocateMainDirectMemory(bytes, alignment, 0x0c,
                                                    offset);
    if (result != 0)
        return result;
    result = sceKernelMapDirectMemory(address, bytes, 0x33, 0, *offset,
                                      alignment);
    return result != 0 || !*address ? (result != 0 ? result : -1) : 0;
}

static int map_command(void)
{
    int result = sceKernelReserveVirtualRange(&resources.command,
                                               resources.command_bytes, 0,
                                               COMMAND_ALIGNMENT);
    if (result != 0 || !resources.command)
        return result != 0 ? result : -1;
    resources.command_reserved = 1;
    result = sceKernelAllocateMainDirectMemory(resources.command_bytes,
                                                COMMAND_ALIGNMENT, 0x0c,
                                                &resources.command_offset);
    if (result != 0)
        return result;
    resources.command_allocated = 1;
    struct ps5_batch_map_entry entry = {
        resources.command, resources.command_offset, resources.command_bytes,
        0xf2, 0x0c, 0, 0
    };
    int processed = -1;
    result = sceKernelBatchMap(&entry, 1, &processed);
    if (result != 0 || processed != 1)
        return result != 0 ? result : -1;
    resources.command_mapped = 1;
    memset(resources.command, 0, resources.command_bytes);
    return 0;
}

#ifdef PS5_RESOURCE_FOUNDATION
#ifdef PS5_REF_AGC_LIVE_PHASE7
static void live_texture_flush(const void *memory, size_t bytes, void *user)
{
    (void)user;
    ps5_native_cache_flush(memory, bytes);
}
#endif

static int add_aligned(size_t *total, size_t bytes, size_t alignment)
{
    if (!total || !bytes || !alignment ||
        (alignment & (alignment - 1u)) != 0u ||
        *total > SIZE_MAX - (alignment - 1u))
        return -1;
    const size_t aligned = (*total + alignment - 1u) & ~(alignment - 1u);
    if (bytes > SIZE_MAX - aligned)
        return -1;
    *total = aligned + bytes;
    return 0;
}

static int init_resource_heap(size_t bsp_bytes, size_t source_file_bytes)
{
#ifndef PS5_TEXTURE_PATH
    (void)source_file_bytes;
#endif
    size_t heap_bytes = 0u;
    if (add_aligned(&heap_bytes, bsp_bytes,
                    BSP_RUNTIME_ALLOCATION_ALIGNMENT) != 0 ||
        add_aligned(&heap_bytes, SHADER_BYTES, SHADER_ALIGNMENT) != 0 ||
        add_aligned(&heap_bytes, DEPTH_ALLOCATION_BYTES,
                    DEPTH_ALIGNMENT) != 0 ||
        add_aligned(&heap_bytes, RESOURCE_TRANSIENT_BYTES,
                    RESOURCE_HEAP_ALIGNMENT) != 0 ||
#ifdef PS5_REF_AGC_LIVE_PHASE7
        add_aligned(&heap_bytes, REF_AGC_GPU_TEXTURE_ARENA_BYTES,
                    RESOURCE_HEAP_ALIGNMENT) != 0 ||
        add_aligned(&heap_bytes, REF_AGC_GPU_WORLD_ARENA_BYTES,
                    RESOURCE_HEAP_ALIGNMENT) != 0 ||
#endif
#ifdef PS5_TEXTURE_PATH
        add_aligned(
            &heap_bytes,
            (source_file_bytes < 2048u * 2048u * 4u
                 ? source_file_bytes
                 : 2048u * 2048u * 4u) +
                2u * BSP_DYNAMIC_LIGHTMAP_GUARD_BYTES,
            RESOURCE_HEAP_ALIGNMENT) != 0 ||
        add_aligned(
            &heap_bytes,
            (source_file_bytes < 2048u * 2048u * 4u
                 ? source_file_bytes
                 : 2048u * 2048u * 4u) +
                2u * BSP_DYNAMIC_LIGHTMAP_GUARD_BYTES,
            RESOURCE_HEAP_ALIGNMENT) != 0 ||
#endif
        add_aligned(&heap_bytes, 64u, RESOURCE_HEAP_ALIGNMENT) != 0)
        return -1;
    heap_bytes = (heap_bytes + RESOURCE_HEAP_ALIGNMENT - 1u) &
                 ~(size_t)(RESOURCE_HEAP_ALIGNMENT - 1u);
    int result = allocate_direct(&resources.resource_heap,
                                 &resources.resource_heap_offset,
                                 heap_bytes, RESOURCE_HEAP_ALIGNMENT);
    if (result != 0)
        return result;
    resources.resource_heap_allocated = resources.resource_heap_mapped = 1;
    resources.resource_heap_bytes = heap_bytes;
    memset(resources.resource_heap, 0, heap_bytes);
    if (ps5_resource_pool_init(&resources.resource_pool,
                               resources.resource_heap, heap_bytes) != 0 ||
        ps5_resource_pool_allocate(
            &resources.resource_pool, bsp_bytes,
            BSP_RUNTIME_ALLOCATION_ALIGNMENT,
            &resources.bsp_allocation) != 0 ||
        ps5_resource_pool_allocate(
            &resources.resource_pool, SHADER_BYTES, SHADER_ALIGNMENT,
            &resources.shader_allocation) != 0 ||
        ps5_resource_pool_allocate(
            &resources.resource_pool, DEPTH_ALLOCATION_BYTES,
            DEPTH_ALIGNMENT, &resources.depth_allocation) != 0 ||
        ps5_resource_pool_allocate(
            &resources.resource_pool, RESOURCE_TRANSIENT_BYTES,
            RESOURCE_HEAP_ALIGNMENT,
            &resources.transient_allocation) != 0
#ifdef PS5_REF_AGC_LIVE_PHASE7
        || ps5_resource_pool_allocate(
            &resources.resource_pool, REF_AGC_GPU_TEXTURE_ARENA_BYTES,
            RESOURCE_HEAP_ALIGNMENT,
            &resources.live_texture_allocation) != 0
        || ps5_resource_pool_allocate(
            &resources.resource_pool, REF_AGC_GPU_WORLD_ARENA_BYTES,
            RESOURCE_HEAP_ALIGNMENT,
            &resources.live_world_allocation) != 0
#endif
        )
        return -2;
    resources.bsp = ps5_resource_pool_pointer(
        &resources.resource_pool, &resources.bsp_allocation);
    resources.shader = ps5_resource_pool_pointer(
        &resources.resource_pool, &resources.shader_allocation);
    resources.depth = ps5_resource_pool_pointer(
        &resources.resource_pool, &resources.depth_allocation);
    resources.transient = ps5_resource_pool_pointer(
        &resources.resource_pool, &resources.transient_allocation);
#ifdef PS5_REF_AGC_LIVE_PHASE7
    resources.live_texture_arena = ps5_resource_pool_pointer(
        &resources.resource_pool, &resources.live_texture_allocation);
    resources.live_world_arena = ps5_resource_pool_pointer(
        &resources.resource_pool, &resources.live_world_allocation);
#endif
    if (!resources.bsp || !resources.shader || !resources.depth ||
        !resources.transient ||
#ifdef PS5_REF_AGC_LIVE_PHASE7
        !resources.live_texture_arena || !resources.live_world_arena ||
#endif
        ((uintptr_t)resources.resource_heap >> 32) != UINT64_C(2) ||
        ps5_transient_ring_init(
            &renderer.transient_ring, resources.transient,
            RESOURCE_TRANSIENT_BYTES, 2u, 256u) != 0)
        return -3;
#ifdef PS5_REF_AGC_LIVE_PHASE7
    if (ref_agc_gpu_texture_cache_init(
            &renderer.live_texture_cache, resources.live_texture_arena,
            (uintptr_t)resources.live_texture_arena,
            REF_AGC_GPU_TEXTURE_ARENA_BYTES,
            live_texture_flush, NULL) != REF_AGC_GPU_TEXTURE_OK)
        return -4;
    if (ref_agc_gpu_world_cache_init(
            &renderer.live_world_cache, resources.live_world_arena,
            (uintptr_t)resources.live_world_arena,
            REF_AGC_GPU_WORLD_ARENA_BYTES,
            live_texture_flush, NULL) != REF_AGC_GPU_WORLD_OK)
        return -5;
#endif
    resources.bsp_bytes = bsp_bytes;
    return 0;
}

#ifdef PS5_TEXTURE_PATH
static int init_dynamic_lightmaps(void)
{
#ifdef PS5_GOLDSRC_LIGHTING_GATE
    if (goldsrc_lightmap_lighting_plan(
            &renderer.bsp_bundle, 65536u,
            &renderer.goldsrc_lighting_plan) != 0)
        return -1;
    renderer.dynamic_lightmap_layout =
        renderer.goldsrc_lighting_plan.layout;
#else
    if (bsp_dynamic_lightmap_select(
            &renderer.bsp_bundle,
            &renderer.dynamic_lightmap_layout) != 0)
        return -1;
#endif
    size_t allocation_bytes = 0u;
    if (bsp_dynamic_lightmap_allocation_bytes(
            &renderer.dynamic_lightmap_layout, &allocation_bytes) != 0)
        return -1;
    for (uint32_t slot = 0u; slot < 2u; ++slot) {
        if (ps5_resource_pool_allocate(
                &resources.resource_pool, allocation_bytes,
                RESOURCE_HEAP_ALIGNMENT,
                &resources.dynamic_lightmap_allocations[slot]) !=
                PS5_RESOURCE_POOL_OK)
            return -2;
        resources.dynamic_lightmaps[slot] = ps5_resource_pool_pointer(
            &resources.resource_pool,
            &resources.dynamic_lightmap_allocations[slot]);
        if (!resources.dynamic_lightmaps[slot] ||
            bsp_dynamic_lightmap_slot_init(
                &renderer.dynamic_lightmap_slots[slot],
                resources.dynamic_lightmaps[slot], allocation_bytes,
                renderer.bsp_bundle.lightmap_pixels,
                renderer.dynamic_lightmap_layout.image_bytes,
                &renderer.dynamic_lightmap_layout) != 0)
            return -3;
    }
    if (resources.dynamic_lightmap_allocations[0].bytes !=
            resources.dynamic_lightmap_allocations[1].bytes)
        return -4;
    const BspTextureResidencyInput accounting_input = {
        resources.resource_heap_bytes,
        resources.bsp_allocation.bytes,
        resources.shader_allocation.bytes,
        resources.depth_allocation.bytes,
        resources.transient_allocation.bytes,
        resources.dynamic_lightmap_allocations[0].bytes,
        renderer.bsp_bundle.texture_pixel_bytes,
        (uint64_t)renderer.bsp_bundle.lightmap_pixel_count * 4u,
        renderer.dynamic_lightmap_layout.image_bytes,
        2u,
    };
    if (bsp_texture_accounting_init(
            &renderer.texture_accounting, &accounting_input) != 0)
        return -4;
    return 0;
}
#endif
#endif

#ifdef PS5_BSP_VIEWER
static int read_exact(int fd, void *buffer, size_t bytes)
{
    uint8_t *cursor = buffer;
    while (bytes != 0u) {
        const ssize_t got = read(fd, cursor, bytes);
        if (got <= 0)
            return -1;
        cursor += (size_t)got;
        bytes -= (size_t)got;
    }
    return 0;
}

static int load_bsp_bundle(void)
{
    const int fd = open("/app0/map.ps5bsp", O_RDONLY);
    if (fd < 0)
        return -1;
    struct stat status;
    if (fstat(fd, &status) != 0 || status.st_size <= 0 ||
        (uint64_t)status.st_size > BSP_MAX_BUNDLE_BYTES ||
        (uint64_t)status.st_size != PS5_BSP_BUNDLE_BYTES) {
        (void)close(fd);
        return -2;
    }
    const size_t file_bytes = (size_t)status.st_size;
#ifdef PS5_GOLDSRC_STUDIO_GATE
    const int studio_fd = open("/app0/model.ps5mdl", O_RDONLY);
    struct stat studio_status;
    if (studio_fd < 0 || fstat(studio_fd, &studio_status) != 0 ||
        studio_status.st_size <= 0 ||
        (uint64_t)studio_status.st_size > STUDIO_MAX_BUNDLE_BYTES ||
        (uint64_t)studio_status.st_size != PS5_STUDIO_BUNDLE_BYTES) {
        (void)close(fd);
        if (studio_fd >= 0)
            (void)close(studio_fd);
        return -2;
    }
    const size_t studio_file_bytes = (size_t)studio_status.st_size;
#endif
    BspRuntimePlan upper;
    const uint32_t maximum_draws =
        (uint32_t)(file_bytes / sizeof(BspBundleDraw));
#ifdef PS5_BSP_TEXTURED
    const uint32_t maximum_textures =
        (uint32_t)(file_bytes / sizeof(BspBundleTexture));
    const int upper_result = bsp_runtime_plan_textured(
        file_bytes, maximum_draws, maximum_textures, &upper);
#else
    const int upper_result = bsp_runtime_plan(file_bytes, maximum_draws,
                                               &upper);
#endif
    if (upper_result != 0) {
        (void)close(fd);
#ifdef PS5_GOLDSRC_STUDIO_GATE
        (void)close(studio_fd);
#endif
        return -3;
    }
    size_t allocation_bytes = upper.allocation_bytes;
#ifdef PS5_GOLDSRC_STUDIO_GATE
    if (allocation_bytes > SIZE_MAX - 255u) {
        (void)close(fd);
        (void)close(studio_fd);
        return -3;
    }
    resources.studio_offset = (allocation_bytes + 255u) & ~(size_t)255u;
    if (studio_file_bytes > SIZE_MAX - resources.studio_offset) {
        (void)close(fd);
        (void)close(studio_fd);
        return -3;
    }
    allocation_bytes = resources.studio_offset + studio_file_bytes;
#endif
    int result;
#ifdef PS5_RESOURCE_FOUNDATION
    result = init_resource_heap(allocation_bytes, file_bytes);
#else
    result = allocate_direct(&resources.bsp, &resources.bsp_offset,
                             allocation_bytes,
                             BSP_RUNTIME_ALLOCATION_ALIGNMENT);
#endif
    if (result != 0) {
        (void)close(fd);
#ifdef PS5_GOLDSRC_STUDIO_GATE
        (void)close(studio_fd);
#endif
        return result;
    }
#ifndef PS5_RESOURCE_FOUNDATION
    resources.bsp_allocated = resources.bsp_mapped = 1;
    resources.bsp_bytes = upper.allocation_bytes;
#endif
    memset(resources.bsp, 0, resources.bsp_bytes);
    result = read_exact(fd, resources.bsp, file_bytes);
    const int close_result = close(fd);
    if (result != 0 || close_result != 0)
        return -4;
#ifdef PS5_GOLDSRC_STUDIO_GATE
    resources.studio = (uint8_t *)resources.bsp + resources.studio_offset;
    resources.studio_bytes = studio_file_bytes;
    result = read_exact(studio_fd, resources.studio, studio_file_bytes);
    const int studio_close_result = close(studio_fd);
    if (result != 0 || studio_close_result != 0 ||
        goldsrc_studio_bundle_open(
            resources.studio, resources.studio_bytes,
            &renderer.goldsrc_studio_bundle) !=
            GOLDSRC_STUDIO_BUNDLE_OK)
        return -4;
#endif
    if (bsp_bundle_open(resources.bsp, file_bytes,
                        &renderer.bsp_bundle) != BSP_BUNDLE_OK)
        return -5;
#ifdef PS5_BSP_TEXTURED
    result = bsp_runtime_plan_textured(
        file_bytes, renderer.bsp_bundle.draw_count,
        renderer.bsp_bundle.texture_count, &renderer.bsp_plan);
#else
    result = bsp_runtime_plan(file_bytes, renderer.bsp_bundle.draw_count,
                              &renderer.bsp_plan);
#endif
    if (result != 0 ||
        renderer.bsp_plan.allocation_bytes > resources.bsp_bytes)
        return -5;
#ifdef PS5_TEXTURE_PATH
    if (init_dynamic_lightmaps() != 0)
        return -6;
#endif
    return 0;
}

static int write_vertex_srd(uint32_t table[4], const void *vertices,
                            uint32_t vertex_count)
{
    if (!table || !vertices || vertex_count == 0u ||
        ((uintptr_t)table >> 32) != UINT64_C(2))
        return -1;
    return ps5_gfx1013_build_vsharp(table, (uintptr_t)vertices,
                                    sizeof(BspBundleVertex),
                                    vertex_count);
}

static int prepare_bsp_scene(void)
{
    uint8_t *const base = resources.bsp;
    uint32_t *const srds = (uint32_t *)(base +
        renderer.bsp_plan.vertex_srds_offset);
    BspBundleVertex *const clear_vertices = (BspBundleVertex *)(base +
        renderer.bsp_plan.clear_vertices_offset);
    uint16_t *const clear_indices = (uint16_t *)(base +
        renderer.bsp_plan.clear_indices_offset);
#ifdef PS5_RESOURCE_FOUNDATION
    renderer.clear_vertices = clear_vertices;
    renderer.clear_indices = clear_indices;
#endif
    BspFlatDraw *const draws = (BspFlatDraw *)(base +
        renderer.bsp_plan.scene_draws_offset);
    if (write_vertex_srd(srds, renderer.bsp_bundle.vertices,
                         renderer.bsp_bundle.vertex_count) != 0 ||
        write_vertex_srd(srds + 4u, clear_vertices, 3u) != 0 ||
        bsp_flat_build_clear(&draws[0], clear_vertices, clear_indices,
                             (uint32_t)(uintptr_t)(srds + 4u)) != 0 ||
        bsp_flat_build_scene(draws + 1u, renderer.bsp_bundle.draw_count,
                             &renderer.bsp_bundle,
                             (uint32_t)(uintptr_t)srds,
                             (float)resources.surface.width /
                                 (float)resources.surface.height) != 0)
        return -1;
#if defined(PS5_BSP_TEXTURED) && !defined(PS5_RESOURCE_FOUNDATION)
    uint32_t *const descriptor_tables = (uint32_t *)(base +
        renderer.bsp_plan.descriptor_tables_offset);
    uint32_t written_dwords = 0u;
    const uintptr_t descriptor_address = (uintptr_t)descriptor_tables;
    if ((descriptor_address >> 32) != UINT64_C(2) ||
        bsp_texture_build_tables(
            descriptor_tables, renderer.bsp_plan.descriptor_table_dwords,
            &renderer.bsp_bundle,
            (uintptr_t)renderer.bsp_bundle.texture_pixels,
            (uintptr_t)renderer.bsp_bundle.lightmap_pixels,
            BSP_TEXTURE_FILTER_TRILINEAR,
            &written_dwords) != 0 ||
        written_dwords != renderer.bsp_plan.descriptor_table_dwords)
        return -2;
    const uint32_t **const bindings = (const uint32_t **)(base +
        renderer.bsp_plan.texture_bindings_offset);
    bindings[0] = descriptor_tables;
    for (uint32_t index = 0; index < renderer.bsp_bundle.draw_count; ++index) {
        const uint32_t texture =
            renderer.bsp_bundle.draws[index].base_texture;
        if (texture >= renderer.bsp_bundle.texture_count)
            return -3;
        bindings[index + 1u] = descriptor_tables +
            texture * BSP_TEXTURE_TABLE_DWORDS;
    }
    renderer.bsp_texture_bindings = bindings;
#endif
    renderer.bsp_draws = draws;
    renderer.bsp_draw_count = renderer.bsp_plan.scene_draw_count;
    volatile uint32_t *guard = (volatile uint32_t *)(base +
        renderer.bsp_plan.guard_offset);
    for (unsigned word = 0; word < 16u; ++word)
        guard[word] = GUARD_WORD;
    ps5_native_cache_flush(resources.bsp, resources.bsp_bytes);
    return 0;
}

#ifdef PS5_BSP_NOCLIP
static int open_noclip_pad(void)
{
    int32_t user_id = -1;
    int result = sceUserServiceInitialize(0);
    if (result != 0)
        return result;
    result = sceUserServiceGetForegroundUser(&user_id);
    if (result != 0 || user_id == -1)
        return result != 0 ? result : -1;
    result = scePadInit();
    if (result != 0)
        return result;
    resources.pad_handle = scePadOpen(user_id, 0, 0, 0);
    if (resources.pad_handle <= 0)
        return resources.pad_handle != 0 ? resources.pad_handle : -1;
    resources.pad_opened = 1;
    return bsp_noclip_init(&renderer.noclip,
                           renderer.bsp_bundle.camera_position,
                           renderer.bsp_bundle.camera_forward);
}

static int update_noclip_camera(struct native_renderer *state,
                                const GearsAnimationFrame *frame)
{
    struct ps5_pad_data pad;
    memset(&pad, 0, sizeof(pad));
    const int result = scePadReadState(state->resources->pad_handle, &pad);
    if (result != 0)
        ++state->pad_read_errors;
    const BspNoclipInput input = {
        pad.left_stick.x, pad.left_stick.y,
        pad.right_stick.x, pad.right_stick.y,
        pad.l2, pad.r2, result == 0 && pad.connected != 0,
    };
    const float delta = frame->frame_index == 0u ? 1.0f / 60.0f
        : frame->time_seconds - state->last_camera_time;
    state->last_camera_time = frame->time_seconds;
    if (bsp_noclip_step(&state->noclip, &input, delta) != 0
#ifndef PS5_RESOURCE_FOUNDATION
        ||
        bsp_flat_update_camera(state->bsp_draws + 1u,
            state->bsp_draw_count - 1u, state->noclip.position,
            state->noclip.forward,
            (float)state->resources->surface.width /
                (float)state->resources->surface.height) != 0
#endif
        )
        return -1;
    if ((frame->frame_index + 1u) % 600u == 0u)
        (void)ps5log_printf(PS5LOG_MARK,
            "BSP_NOCLIP_INPUT frame=%llu connected=%llu moving=%llu "
            "looking=%llu distance_milli=%llu changes=%llu hash=%016llx "
            "read_errors=%llu",
            (unsigned long long)frame->frame_index,
            (unsigned long long)state->noclip.connected_frames,
            (unsigned long long)state->noclip.moving_frames,
            (unsigned long long)state->noclip.looking_frames,
            (unsigned long long)(state->noclip.distance_travelled * 1000.0f),
            (unsigned long long)state->noclip.input_changes,
            (unsigned long long)state->noclip.input_hash,
            (unsigned long long)state->pad_read_errors);
    return 0;
}
#endif
#endif

static const struct ps5_videoout_ops video_ops = {
    sceVideoOutOpen, sceVideoOutClose, sceVideoOutSetFlipRate,
    sceKernelCreateEqueue, sceKernelDeleteEqueue,
    sceVideoOutAddFlipEvent, sceVideoOutDeleteFlipEvent,
    sceVideoOutSetBufferAttribute2, sceVideoOutRegisterBuffers2,
    sceVideoOutUnregisterBuffers
};

static uint64_t readback_hash(const void *data, size_t bytes);
static uint64_t bright_pixel_count(const void *data, size_t bytes);

#ifdef PS5_RESOURCE_FOUNDATION
static int resource_compose_fail(struct native_renderer *state,
                                 uint32_t slot, int result)
{
#ifdef PS5_REF_AGC_LIVE_PHASE7
    (void)ps5log_printf(PS5LOG_ERR,
        "REF_AGC_LIVE_COMPOSE_FAILURE stage=%s slot=%u result=%d",
        state && state->live_compose_stage ? state->live_compose_stage :
            "unknown",
        slot, result);
#endif
    (void)ps5_transient_ring_abort_unsubmitted(&state->transient_ring, slot);
    return result;
}

#ifdef PS5_REF_AGC_LIVE_PHASE7
static int bind_native_pipeline(
    struct native_renderer *state, uint32_t **cursor, uint32_t *end,
    const struct ps5_pipeline_registers *pipeline)
{
    if (!state || !cursor || !*cursor || !end || *cursor > end || !pipeline)
        return -1;
    int result = ps5_native_set_indirect(
        cursor, (uint32_t)(end - *cursor), pipeline->cx,
        PS5_PIPELINE_CX_REGISTERS, state->resources->shader,
        SHADER_BYTES, PS5_NATIVE_REGISTERS_CX);
    if (result == 0)
        result = ps5_native_set_indirect(
            cursor, (uint32_t)(end - *cursor), pipeline->uc,
            PS5_PIPELINE_UC_REGISTERS, state->resources->shader,
            SHADER_BYTES, PS5_NATIVE_REGISTERS_UC);
    if (result == 0)
        result = ps5_native_set_indirect(
            cursor, (uint32_t)(end - *cursor), pipeline->sh,
            PS5_PIPELINE_SH_REGISTERS, state->resources->shader,
            SHADER_BYTES, PS5_NATIVE_REGISTERS_SH);
    return result;
}
#endif

#ifdef PS5_GOLDSRC_PHASE4
static int bind_goldsrc_pipeline(
    struct native_renderer *state, uint32_t **cursor, uint32_t *end,
    const GoldSrcRenderState *render_state, uint32_t framebuffer_slot,
    Ps5GoldSrcPipelineBinding *binding)
{
    if (!state || !cursor || !*cursor || !end || *cursor > end ||
        ps5_goldsrc_pipeline_runtime_bind(
            &state->goldsrc_pipeline_runtime, render_state,
            framebuffer_slot, binding) != 0)
        return -1;
    int result = ps5_native_set_indirect(
        cursor, (uint32_t)(end - *cursor), binding->pipeline->cx,
        PS5_PIPELINE_CX_REGISTERS, state->resources->shader,
        SHADER_BYTES, PS5_NATIVE_REGISTERS_CX);
    if (result == 0)
        result = ps5_native_set_indirect(
            cursor, (uint32_t)(end - *cursor), binding->pipeline->uc,
            PS5_PIPELINE_UC_REGISTERS, state->resources->shader,
            SHADER_BYTES, PS5_NATIVE_REGISTERS_UC);
    if (result == 0)
        result = ps5_native_set_indirect(
            cursor, (uint32_t)(end - *cursor), binding->pipeline->sh,
            PS5_PIPELINE_SH_REGISTERS, state->resources->shader,
            SHADER_BYTES, PS5_NATIVE_REGISTERS_SH);
    if (result == 0)
        result = ps5_native_set_indirect(
            cursor, (uint32_t)(end - *cursor), binding->dynamic_cx,
            PS5_GOLDSRC_RENDER_REGISTER_COUNT, state->resources->shader,
            SHADER_BYTES, PS5_NATIVE_REGISTERS_CX);
    return result;
}
#endif
#endif

static int frame_compose(const GearsAnimationFrame *frame, void *opaque)
{
    struct native_renderer *state = opaque;
    if (!frame || !state || frame->buffer >= 2u)
        return -1;
#ifdef PS5_REF_AGC_MODULE
    /* Reuse the already-proven final 600-frame composition window without
     * repeating the standalone 60,000-frame durability soak in every engine
     * integration run. Ownership tokens keep their original 0..599 sequence. */
    const uint64_t feature_frame_index =
        PS5_REF_AGC_FEATURE_FRAME(frame->frame_index);
#else
    const uint64_t feature_frame_index = frame->frame_index;
#endif
#ifdef PS5_BSP_NOCLIP
#ifdef PS5_REF_AGC_LIVE_PHASE7
    state->live_compose_stage = "camera";
    if (!state->live_frame_valid)
        return -9;
    if (ref_agc_live_world_view_ready(&state->live_frame) &&
        ref_agc_live_view_camera(&state->live_frame.view,
                                 state->noclip.position,
                                 state->noclip.forward,
                                 &state->live_aspect_ratio) != 0)
        return -9;
#else
    if (update_noclip_camera(state, frame) != 0)
        return -9;
#ifdef PS5_GOLDSRC_PHASE4_FINAL_GATE
    memcpy(state->noclip.position, state->bsp_bundle.camera_position,
           sizeof(state->noclip.position));
    memcpy(state->noclip.forward, state->bsp_bundle.camera_forward,
           sizeof(state->noclip.forward));
#elif defined(PS5_GOLDSRC_LIGHTING_GATE)
    memcpy(state->noclip.position,
           state->goldsrc_lighting_plan.target_position,
           sizeof(state->noclip.position));
    memcpy(state->noclip.forward,
           state->goldsrc_lighting_plan.target_forward,
           sizeof(state->noclip.forward));
#elif defined(PS5_GOLDSRC_SPRITE_PARTICLE_GATE) || \
      defined(PS5_GOLDSRC_STUDIO_GATE) || \
      defined(PS5_GOLDSRC_BRUSH_GATE) || \
      defined(PS5_GOLDSRC_VISIBILITY_GATE)
    memcpy(state->noclip.position, state->bsp_bundle.camera_position,
           sizeof(state->noclip.position));
    memcpy(state->noclip.forward, state->bsp_bundle.camera_forward,
           sizeof(state->noclip.forward));
#endif
#endif
#endif
#ifdef PS5_RESOURCE_FOUNDATION
    const uint32_t resource_slot = frame->buffer;
    const uint64_t completed = state->completed_tokens[resource_slot];
#ifdef PS5_REF_AGC_LIVE_PHASE7
    state->live_compose_stage = "transient-begin";
#endif
    if (ps5_transient_ring_begin(
            &state->transient_ring, resource_slot, completed,
            completed != 0u) != PS5_TRANSIENT_OK)
        return -8;
#ifdef PS5_TEXTURE_PATH
#ifdef PS5_REF_AGC_LIVE_PHASE7
    state->live_compose_stage = "dynamic-lightmap";
#endif
#ifdef PS5_GOLDSRC_LIGHTING_GATE
    const uint32_t lighting_mode =
        goldsrc_lighting_mode(feature_frame_index);
    if (goldsrc_lightmap_lighting_update(
            &state->goldsrc_lighting_plan,
            &state->dynamic_lightmap_slots[resource_slot],
            &state->transient_ring, resource_slot, frame->frame_index,
            lighting_mode,
            &state->dynamic_lightmap_updates[resource_slot],
            &state->goldsrc_lighting_updates[resource_slot]) != 0)
        return resource_compose_fail(state, resource_slot, -9);
#elif defined(PS5_TEXTURE_MIP_GATE) || defined(PS5_TEXTURE_ALPHA_GATE) || \
    defined(PS5_TEXTURE_SKY_GATE) || defined(PS5_GOLDSRC_VISIBILITY_GATE)
    if (bsp_dynamic_lightmap_update_pattern(
            &state->dynamic_lightmap_slots[resource_slot],
            &state->dynamic_lightmap_layout, &state->transient_ring,
            resource_slot, frame->frame_index,
            (uint32_t)(
#ifdef PS5_GOLDSRC_VISIBILITY_GATE
                0u
#else
                (frame->frame_index >> 1) & 1u
#endif
            ),
            &state->dynamic_lightmap_updates[resource_slot]) != 0)
        return resource_compose_fail(state, resource_slot, -9);
#else
    if (bsp_dynamic_lightmap_update(
            &state->dynamic_lightmap_slots[resource_slot],
            &state->dynamic_lightmap_layout, &state->transient_ring,
            resource_slot, frame->frame_index,
            &state->dynamic_lightmap_updates[resource_slot]) != 0)
        return resource_compose_fail(state, resource_slot, -9);
#endif
    const uint64_t lightmap_gpu_address = (uintptr_t)
        state->dynamic_lightmap_slots[resource_slot].pixels;
#else
    const uint64_t lightmap_gpu_address =
        (uintptr_t)state->bsp_bundle.lightmap_pixels;
#endif
    enum ps5_gfx1013_filter base_filter =
        PS5_GFX1013_FILTER_ANISOTROPIC_4X;
#ifdef PS5_TEXTURE_MIP_GATE
    base_filter = (frame->frame_index & 1u)
        ? PS5_GFX1013_FILTER_ANISOTROPIC_4X
        : PS5_GFX1013_FILTER_TRILINEAR;
#endif
#ifdef PS5_GOLDSRC_STATE_MATRIX_GATE
    const uint32_t matrix_index =
        goldsrc_state_matrix_index(frame->frame_index);
    const GoldSrcStateMatrixCase *const matrix_case =
        goldsrc_state_matrix_case(matrix_index);
    if (!matrix_case)
        return resource_compose_fail(state, resource_slot, -9);
    const BspResourceGoldSrcConstants matrix_constants = {
        {matrix_case->render_color[0], matrix_case->render_color[1],
         matrix_case->render_color[2], matrix_case->render_color[3]},
        {matrix_case->fog_color_density[0],
         matrix_case->fog_color_density[1],
         matrix_case->fog_color_density[2],
         matrix_case->fog_color_density[3]},
    };
    const int resource_frame_result = bsp_resource_frame_build_configured(
#else
#ifdef PS5_REF_AGC_LIVE_PHASE7
    state->live_compose_stage = "world-frame";
#endif
    const int resource_frame_result = bsp_resource_frame_build(
#endif
            &state->resource_frames[resource_slot],
            &state->transient_ring, resource_slot,
            state->resources->resource_heap,
            state->resources->resource_heap_bytes,
            &state->bsp_bundle, lightmap_gpu_address,
            state->clear_vertices,
            state->clear_indices, state->noclip.position,
            state->noclip.forward,
#ifdef PS5_REF_AGC_LIVE_PHASE7
            state->live_aspect_ratio,
#else
                (float)state->resources->surface.width /
                (float)state->resources->surface.height,
#endif
            feature_frame_index,
            base_filter
#ifdef PS5_GOLDSRC_STATE_MATRIX_GATE
            , &matrix_constants
#endif
            );
    if (resource_frame_result != 0) {
#ifdef PS5_REF_AGC_LIVE_PHASE7
        (void)ps5log_printf(PS5LOG_ERR,
            "REF_AGC_LIVE_WORLD_FRAME_FAILURE detail=%d "
            "aspect_milli=%d transient_used=%llu transient_capacity=%llu",
            resource_frame_result,
            (int)(state->live_aspect_ratio * 1000.0f),
            (unsigned long long)
                state->transient_ring.slots[resource_slot].used,
            (unsigned long long)
                state->transient_ring.slots[resource_slot].bytes);
#endif
        return resource_compose_fail(state, resource_slot, -9);
    }
#ifdef PS5_GOLDSRC_2D_GATE
#ifdef PS5_REF_AGC_LIVE_PHASE7
    state->live_compose_stage = "screen-2d-frame";
#endif
    if (goldsrc_2d_frame_build(
            &state->goldsrc_2d_frames[resource_slot],
            &state->transient_ring, resource_slot,
            state->resources->resource_heap,
            state->resources->resource_heap_bytes,
            state->resources->surface.width,
            state->resources->surface.height,
            feature_frame_index) != 0)
        return resource_compose_fail(state, resource_slot, -9);
    state->resource_frames[resource_slot].transient_bytes =
        state->transient_ring.slots[resource_slot].used;
#endif
#ifdef PS5_GOLDSRC_SPRITE_PARTICLE_GATE
#ifdef PS5_REF_AGC_LIVE_PHASE7
    state->live_compose_stage = "sprite-particle-frame";
#endif
    if (goldsrc_sprite_particle_frame_build(
            &state->goldsrc_effect_frames[resource_slot],
            &state->transient_ring, resource_slot,
            state->resources->resource_heap,
            state->resources->resource_heap_bytes,
            state->noclip.position, state->noclip.forward,
            (float)state->resources->surface.width /
                (float)state->resources->surface.height,
            feature_frame_index) != 0)
        return resource_compose_fail(state, resource_slot, -9);
    state->resource_frames[resource_slot].transient_bytes =
        state->transient_ring.slots[resource_slot].used;
#endif
#ifdef PS5_GOLDSRC_STUDIO_GATE
#ifdef PS5_REF_AGC_LIVE_PHASE7
    state->live_compose_stage = "studio-frame";
#endif
    if (goldsrc_studio_frame_build(
            &state->goldsrc_studio_frames[resource_slot],
            &state->goldsrc_studio_bundle,
            &state->transient_ring, resource_slot,
            state->resources->resource_heap,
            state->resources->resource_heap_bytes,
            state->noclip.position, state->noclip.forward,
            (float)state->resources->surface.width /
                (float)state->resources->surface.height,
            feature_frame_index) != 0)
        return resource_compose_fail(state, resource_slot, -9);
    state->resource_frames[resource_slot].transient_bytes =
        state->transient_ring.slots[resource_slot].used;
    if (frame->frame_index == 0u)
        state->goldsrc_studio_first_pose_hash =
            state->goldsrc_studio_frames[resource_slot].pose_hash;
    state->goldsrc_studio_last_pose_hash =
        state->goldsrc_studio_frames[resource_slot].pose_hash;
#endif
#ifdef PS5_GOLDSRC_BRUSH_GATE
#ifdef PS5_REF_AGC_LIVE_PHASE7
    state->live_compose_stage = "brush-frame";
#endif
    if (goldsrc_brush_frame_build(
            &state->goldsrc_brush_frames[resource_slot],
            &state->goldsrc_brush_plan, &state->bsp_bundle,
            &state->transient_ring, resource_slot,
            state->resources->resource_heap,
            state->resources->resource_heap_bytes,
            state->noclip.position, state->noclip.forward,
            (float)state->resources->surface.width /
                (float)state->resources->surface.height,
            feature_frame_index) != 0)
        return resource_compose_fail(state, resource_slot, -9);
    state->resource_frames[resource_slot].transient_bytes =
        state->transient_ring.slots[resource_slot].used;
    if (frame->frame_index == 0u)
        state->goldsrc_brush_first_transform_hash =
            state->goldsrc_brush_frames[resource_slot].transform_hash;
    state->goldsrc_brush_last_transform_hash =
        state->goldsrc_brush_frames[resource_slot].transform_hash;
#endif
#ifdef PS5_GOLDSRC_VISIBILITY_GATE
#ifdef PS5_REF_AGC_LIVE_PHASE7
    state->live_compose_stage = "visibility-frame";
#endif
    const int visibility_frame_result = goldsrc_visibility_frame_build(
            &state->goldsrc_visibility_frames[resource_slot],
            &state->goldsrc_visibility_plan, &state->bsp_bundle,
            &state->transient_ring, resource_slot,
            state->noclip.position, state->noclip.forward,
            (float)state->resources->surface.width /
                (float)state->resources->surface.height,
            feature_frame_index);
    if (visibility_frame_result != 0) {
#ifdef PS5_REF_AGC_LIVE_PHASE7
        (void)ps5log_printf(PS5LOG_ERR,
            "REF_AGC_LIVE_VISIBILITY_FAILURE detail=%d "
            "position_milli=%d,%d,%d forward_milli=%d,%d,%d "
            "map_serial=%llu world_surfaces=%u view_flags=%u",
            visibility_frame_result,
            (int)(state->noclip.position[0] * 1000.0f),
            (int)(state->noclip.position[1] * 1000.0f),
            (int)(state->noclip.position[2] * 1000.0f),
            (int)(state->noclip.forward[0] * 1000.0f),
            (int)(state->noclip.forward[1] * 1000.0f),
            (int)(state->noclip.forward[2] * 1000.0f),
            (unsigned long long)state->live_frame.map_serial,
            state->live_frame.world.surfaces,
            state->live_frame.view.flags);
#endif
        return resource_compose_fail(state, resource_slot, -9);
    }
    state->resource_frames[resource_slot].transient_bytes =
        state->transient_ring.slots[resource_slot].used;
#endif
    Ps5TransientSlot *const transient_slot =
        &state->transient_ring.slots[resource_slot];
    const void *const transient_begin = state->transient_ring.base +
        transient_slot->offset;
#ifdef PS5_REF_AGC_LIVE_PHASE7
    state->live_compose_stage = "transient-cache-plan";
#endif
    if (ps5_cache_cpu_to_gpu_plan(
            state->resources->resource_heap,
            state->resources->resource_heap_bytes, transient_begin,
            transient_slot->used,
            &state->cache_plans[resource_slot]) != 0) {
        return resource_compose_fail(state, resource_slot, -9);
    }
#ifdef PS5_TEXTURE_PATH
#ifdef PS5_REF_AGC_LIVE_PHASE7
    state->live_compose_stage = "lightmap-cache-plan";
#endif
    if (ps5_cache_cpu_to_gpu_plan(
            state->resources->resource_heap,
            state->resources->resource_heap_bytes,
            state->dynamic_lightmap_updates[resource_slot].written_address,
            state->dynamic_lightmap_updates[resource_slot]
                .written_span_bytes,
            &state->dynamic_lightmap_cache_plans[resource_slot]) != 0)
        return resource_compose_fail(state, resource_slot, -9);
#endif
    ps5_native_cache_flush(
        state->cache_plans[resource_slot].flush_address,
        state->cache_plans[resource_slot].flush_bytes);
#ifdef PS5_TEXTURE_PATH
    ps5_native_cache_flush(
        state->dynamic_lightmap_cache_plans[resource_slot].flush_address,
        state->dynamic_lightmap_cache_plans[resource_slot].flush_bytes);
    BspTextureUploadFrame texture_upload;
#ifdef PS5_REF_AGC_LIVE_PHASE7
    state->live_compose_stage = "texture-accounting";
#endif
    if (bsp_texture_accounting_record(
            &state->texture_accounting, frame->frame_index,
            transient_slot->used,
            state->dynamic_lightmap_updates[resource_slot].uploaded_bytes,
            state->dynamic_lightmap_updates[resource_slot].first_upload,
            &texture_upload) != 0)
        return resource_compose_fail(state, resource_slot, -9);
    if (frame->frame_index < 2u ||
        PS5_BSP_FINAL_WINDOW(frame->frame_index))
        (void)ps5log_printf(PS5LOG_MARK,
            "DYNAMIC_LIGHTMAP_FRAME frame=%llu slot=%u pattern=%u "
            "patch_hash=%016llx first_upload=%s patch_bytes=%llu "
            "lightmap_upload_bytes=%llu dirty_span_bytes=%llu "
            "acquire_bytes=%llu "
            "resident_bytes=%llu uploaded_bytes=%llu "
            "transient_upload_bytes=%llu total_uploaded_bytes=%llu",
            (unsigned long long)frame->frame_index, resource_slot,
            state->dynamic_lightmap_updates[resource_slot].pattern,
            (unsigned long long)
                state->dynamic_lightmap_updates[resource_slot].patch_hash,
            state->dynamic_lightmap_updates[resource_slot].first_upload
                ? "true" : "false",
            (unsigned long long)state->dynamic_lightmap_layout.patch_bytes,
            (unsigned long long)
                state->dynamic_lightmap_updates[resource_slot].uploaded_bytes,
            (unsigned long long)
                state->dynamic_lightmap_updates[resource_slot]
                    .written_span_bytes,
            (unsigned long long)
                state->dynamic_lightmap_cache_plans[resource_slot]
                    .acquire_bytes,
            (unsigned long long)
                state->texture_accounting.residency.pool_resident_bytes,
            (unsigned long long)texture_upload.total_bytes,
            (unsigned long long)transient_slot->used,
            (unsigned long long)texture_upload.cumulative_total_bytes);
#ifdef PS5_GOLDSRC_LIGHTING_GATE
    if (frame->frame_index % GOLDSRC_LIGHTING_HOLD_FRAMES < 2u ||
        PS5_BSP_FINAL_WINDOW(frame->frame_index)) {
        const GoldSrcLightmapLightingUpdate *const lighting =
            &state->goldsrc_lighting_updates[resource_slot];
        (void)ps5log_printf(PS5LOG_MARK,
            "GOLDSRC_LIGHTING_FRAME schema=1 frame=%llu slot=%u "
            "mode=%u name=%s style_tick=%u animated_scale=%u "
            "dynamic_luxels=%u dynamic_center=%u,%u "
            "style_hash=%016llx patch_hash=%016llx "
            "patch_bytes=%llu upload_bytes=%llu "
            "dirty_span_bytes=%llu first_upload=%s",
            (unsigned long long)frame->frame_index, resource_slot,
            lighting->mode, goldsrc_lighting_mode_name(lighting->mode),
            lighting->style_tick, lighting->animated_style_scale,
            lighting->dynamic_luxels, lighting->dynamic_center_x,
            lighting->dynamic_center_y,
            (unsigned long long)lighting->style_state_hash,
            (unsigned long long)
                state->dynamic_lightmap_updates[resource_slot].patch_hash,
            (unsigned long long)state->dynamic_lightmap_layout.patch_bytes,
            (unsigned long long)
                state->dynamic_lightmap_updates[resource_slot].uploaded_bytes,
            (unsigned long long)
                state->dynamic_lightmap_updates[resource_slot]
                    .written_span_bytes,
            state->dynamic_lightmap_updates[resource_slot].first_upload
                ? "true" : "false");
    }
#endif
#ifdef PS5_GOLDSRC_SPRITE_PARTICLE_GATE
    if (frame->frame_index % GOLDSRC_EFFECT_HOLD_FRAMES < 2u ||
        PS5_BSP_FINAL_WINDOW(frame->frame_index)) {
        const GoldSrcSpriteParticleFrame *const effects =
            &state->goldsrc_effect_frames[resource_slot];
        (void)ps5log_printf(PS5LOG_MARK,
            "GOLDSRC_SPRITE_PARTICLE_FRAME schema=1 frame=%llu "
            "slot=%u mode=%u name=%s sprite_quads=%u "
            "alpha_particles=%u additive_particles=%u "
            "sprite_indices=%u alpha_particle_indices=%u "
            "additive_particle_indices=%u atlas_hash=%016llx "
            "layout_hash=%016llx transient_bytes=%llu "
            "geometry=per-frame-transient",
            (unsigned long long)frame->frame_index, resource_slot,
            effects->mode,
            goldsrc_sprite_particle_mode_name(effects->mode),
            effects->sprite_quads, effects->alpha_particles,
            effects->additive_particles, effects->sprite_index_count,
            effects->alpha_particle_index_count,
            effects->additive_particle_index_count,
            (unsigned long long)effects->atlas_hash,
            (unsigned long long)effects->layout_hash,
            (unsigned long long)effects->transient_bytes);
    }
#endif
#ifdef PS5_GOLDSRC_STUDIO_GATE
    if (frame->frame_index % GOLDSRC_STUDIO_HOLD_FRAMES < 2u ||
        PS5_BSP_FINAL_WINDOW(frame->frame_index)) {
        const GoldSrcStudioFrame *const studio =
            &state->goldsrc_studio_frames[resource_slot];
        (void)ps5log_printf(PS5LOG_MARK,
            "GOLDSRC_STUDIO_FRAME schema=1 frame=%llu slot=%u "
            "mode=%u name=%s animation=%u,%u blend_milli=%u "
            "pose_hash=%016llx skinned_hash=%016llx "
            "vertices_per_instance=%u indices_per_instance=%u "
            "draws_per_instance=%u textures=%u transient_bytes=%llu "
            "skinning=cpu geometry=per-frame-transient",
            (unsigned long long)frame->frame_index, resource_slot,
            studio->mode, goldsrc_studio_mode_name(studio->mode),
            studio->frame0, studio->frame1,
            (uint32_t)(studio->blend*1000.0f+0.5f),
            (unsigned long long)studio->pose_hash,
            (unsigned long long)studio->skinned_hash,
            studio->vertices_per_instance, studio->indices_per_instance,
            studio->draw_count, studio->texture_count,
            (unsigned long long)studio->transient_bytes);
    }
#endif
#ifdef PS5_GOLDSRC_BRUSH_GATE
    if (frame->frame_index % GOLDSRC_BRUSH_HOLD_FRAMES < 2u ||
        PS5_BSP_FINAL_WINDOW(frame->frame_index)) {
        const GoldSrcBrushFrame *const brush =
            &state->goldsrc_brush_frames[resource_slot];
        (void)ps5log_printf(PS5LOG_MARK,
            "GOLDSRC_BRUSH_FRAME schema=1 frame=%llu slot=%u "
            "mode=%u name=%s transform_hash=%016llx "
            "transient_bytes=%llu transforms=independent "
            "geometry=real-bsp-submodels",
            (unsigned long long)frame->frame_index, resource_slot,
            brush->mode, goldsrc_brush_mode_name(brush->mode),
            (unsigned long long)brush->transform_hash,
            (unsigned long long)brush->transient_bytes);
    }
#endif
#ifdef PS5_GOLDSRC_VISIBILITY_GATE
    if (frame->frame_index % GOLDSRC_VISIBILITY_HOLD_FRAMES < 2u ||
        PS5_BSP_FINAL_WINDOW(frame->frame_index)) {
        const GoldSrcVisibilityFrame *const visibility =
            &state->goldsrc_visibility_frames[resource_slot];
        (void)ps5log_printf(PS5LOG_MARK,
            "GOLDSRC_VISIBILITY_FRAME schema=1 frame=%llu slot=%u "
            "mode=%u name=%s camera_leaf=%u visible_leaves=%u "
            "world_draws=%u selected_draws=%u opaque=%u alpha=%u sky=%u "
            "pvs_culled=%u frustum_culled=%u mask_hash=%016llx "
            "transient_bytes=%llu",
            (unsigned long long)frame->frame_index, resource_slot,
            visibility->mode,
            goldsrc_visibility_mode_name(visibility->mode),
            visibility->camera_leaf, visibility->visible_leaves,
            visibility->world_draws, visibility->selected_draws,
            visibility->opaque_draws, visibility->alpha_draws,
            visibility->sky_draws, visibility->pvs_culled,
            visibility->frustum_culled,
            (unsigned long long)visibility->mask_hash,
            (unsigned long long)visibility->transient_bytes);
    }
#endif
#ifdef PS5_GOLDSRC_PHASE4_FINAL_GATE
    if (feature_frame_index == 59400u || feature_frame_index == 59401u ||
        PS5_BSP_FINAL_WINDOW(frame->frame_index)) {
        const GoldSrcLightmapLightingUpdate *const lighting =
            &state->goldsrc_lighting_updates[resource_slot];
        const GoldSrcSpriteParticleFrame *const effects =
            &state->goldsrc_effect_frames[resource_slot];
        const GoldSrcStudioFrame *const studio =
            &state->goldsrc_studio_frames[resource_slot];
        const GoldSrcBrushFrame *const brush =
            &state->goldsrc_brush_frames[resource_slot];
        const GoldSrcVisibilityFrame *const visibility =
            &state->goldsrc_visibility_frames[resource_slot];
        const GoldSrc2DFrame *const screen =
            &state->goldsrc_2d_frames[resource_slot];
        (void)ps5log_printf(PS5LOG_MARK,
            "GOLDSRC_PHASE4_FINAL_FRAME schema=1 frame=%llu slot=%u "
            "lighting=%u effects=%u studio=%u brush=%u visibility=%u "
            "world_draws=%u brush_draws=69 studio_draws=12 "
            "effect_draws=3 screen_draws=2 dynamic_luxels=%u "
            "pose_hash=%016llx transform_hash=%016llx "
            "screen_layout_hash=%016llx transient_bytes=%llu",
            (unsigned long long)frame->frame_index, resource_slot,
            lighting->mode, effects->mode, studio->mode, brush->mode,
            visibility->mode, visibility->selected_draws,
            lighting->dynamic_luxels,
            (unsigned long long)studio->pose_hash,
            (unsigned long long)brush->transform_hash,
            (unsigned long long)screen->layout_hash,
            (unsigned long long)
                state->transient_ring.slots[resource_slot].used);
    }
#endif
#ifdef PS5_TEXTURE_ACCOUNTING_ENABLED
    if (frame->frame_index < 2u ||
        PS5_BSP_FINAL_WINDOW(frame->frame_index))
        (void)ps5log_printf(PS5LOG_MARK,
            "TEXTURE_UPLOAD_FRAME schema=1 frame=%llu slot=%u "
            "first_upload=%s transient_bytes=%llu lightmap_bytes=%llu "
            "frame_bytes=%llu cumulative_transient_bytes=%llu "
            "cumulative_lightmap_bytes=%llu cumulative_bytes=%llu "
            "sequence_hash=%016llx accounting=checked-u64",
            (unsigned long long)texture_upload.frame, resource_slot,
            texture_upload.first_upload ? "true" : "false",
            (unsigned long long)texture_upload.transient_bytes,
            (unsigned long long)texture_upload.lightmap_bytes,
            (unsigned long long)texture_upload.total_bytes,
            (unsigned long long)
                texture_upload.cumulative_transient_bytes,
            (unsigned long long)
                texture_upload.cumulative_lightmap_bytes,
            (unsigned long long)texture_upload.cumulative_total_bytes,
            (unsigned long long)texture_upload.sequence_hash);
#endif
#ifdef PS5_TEXTURE_MIP_GATE
    if (frame->frame_index < 2u ||
        PS5_BSP_FINAL_WINDOW(frame->frame_index)) {
        const uint32_t *const sampler =
            state->resource_frames[resource_slot].texture_tables +
            BSP_GFX1013_IMAGE_DWORDS;
        (void)ps5log_printf(PS5LOG_MARK,
            "MIP_SAMPLER_FRAME frame=%llu slot=%u filter=%s "
            "lightmap_pattern=%u s0=%08x s1=%08x s2=%08x s3=%08x",
            (unsigned long long)frame->frame_index, resource_slot,
            (frame->frame_index & 1u) ? "anisotropic4x" : "trilinear",
            state->dynamic_lightmap_updates[resource_slot].pattern,
            sampler[0], sampler[1], sampler[2], sampler[3]);
    }
#endif
#endif
    if (frame->frame_index == 0u ||
        frame->frame_index + 1u == BSP_GATE_FRAME_COUNT)
        (void)ps5log_printf(PS5LOG_MARK,
            "RESOURCE_FRAME_READY frame=%llu slot=%u transient_bytes=%llu "
            "constant_dwords=%u texture_descriptor_dwords=%u "
            "overlay_vertices=%u overlay_source=constant-vsharp "
            "overlay_indices=%u acquire_engine=%u "
            "gcr=%08x poll_cycles=%x "
            "tables_gpu_span=true",
            (unsigned long long)frame->frame_index, resource_slot,
            (unsigned long long)
                state->resource_frames[resource_slot].transient_bytes,
            BSP_RESOURCE_CONSTANT_DWORDS,
            state->resource_frames[resource_slot].texture_table_dwords,
            BSP_RESOURCE_OVERLAY_VERTICES,
            BSP_RESOURCE_OVERLAY_INDICES,
            state->cache_plans[resource_slot].engine,
            state->cache_plans[resource_slot].gcr_control,
            state->cache_plans[resource_slot].poll_cycles);
#endif
    uint32_t *const begin = state->commands[frame->buffer];
    uint32_t *const end = begin +
        state->command_slot_bytes / sizeof(uint32_t);
    uint32_t *cursor = begin;
    memset(begin, 0, state->command_slot_bytes);
    int result = ps5_native_wait_rendering(
        &cursor, (uint32_t)(end - cursor), 0u,
        state->resources->video.handle, (int32_t)frame->buffer);
    if (result != 0)
#ifdef PS5_RESOURCE_FOUNDATION
        return resource_compose_fail(state, resource_slot, -10);
#else
        return -10;
#endif
#ifdef PS5_RESOURCE_FOUNDATION
    result = ps5_native_acquire_mem(
        &cursor, (uint32_t)(end - cursor),
        state->cache_plans[resource_slot].acquire_base,
        state->cache_plans[resource_slot].acquire_bytes,
        state->cache_plans[resource_slot].engine,
        state->cache_plans[resource_slot].gcr_control,
        state->cache_plans[resource_slot].poll_cycles,
        state->resources->resource_heap,
        state->resources->resource_heap_bytes);
    if (result != 0)
        return resource_compose_fail(state, resource_slot, -10);
#ifdef PS5_REF_AGC_LIVE_PHASE7
    if (state->live_texture_revision != 0u) {
        result = ps5_native_acquire_mem(
            &cursor, (uint32_t)(end - cursor),
            state->live_texture_cache_plan.acquire_base,
            state->live_texture_cache_plan.acquire_bytes,
            state->live_texture_cache_plan.engine,
            state->live_texture_cache_plan.gcr_control,
            state->live_texture_cache_plan.poll_cycles,
            state->resources->resource_heap,
            state->resources->resource_heap_bytes);
        if (result != 0)
            return resource_compose_fail(state, resource_slot, -10);
    }
    if (state->live_world_revision != 0u) {
        result = ps5_native_acquire_mem(
            &cursor, (uint32_t)(end - cursor),
            state->live_world_cache_plan.acquire_base,
            state->live_world_cache_plan.acquire_bytes,
            state->live_world_cache_plan.engine,
            state->live_world_cache_plan.gcr_control,
            state->live_world_cache_plan.poll_cycles,
            state->resources->resource_heap,
            state->resources->resource_heap_bytes);
        if (result != 0)
            return resource_compose_fail(state, resource_slot, -10);
    }
#endif
#ifdef PS5_TEXTURE_PATH
    result = ps5_native_acquire_mem(
        &cursor, (uint32_t)(end - cursor),
        state->dynamic_lightmap_cache_plans[resource_slot].acquire_base,
        state->dynamic_lightmap_cache_plans[resource_slot].acquire_bytes,
        state->dynamic_lightmap_cache_plans[resource_slot].engine,
        state->dynamic_lightmap_cache_plans[resource_slot].gcr_control,
        state->dynamic_lightmap_cache_plans[resource_slot].poll_cycles,
        state->resources->resource_heap,
        state->resources->resource_heap_bytes);
    if (result != 0)
        return resource_compose_fail(state, resource_slot, -10);
#endif
#endif
    result = ps5_native_fill_depth(
        &cursor, (uint32_t)(end - cursor), state->resources->depth,
        UINT32_C(0x3f800000), DEPTH_BYTES, state->resources->depth,
        DEPTH_ALLOCATION_BYTES);
    if (result != 0)
#ifdef PS5_RESOURCE_FOUNDATION
        return resource_compose_fail(state, resource_slot, -11);
#else
        return -11;
#endif
    result = ps5_native_set_indirect(
        &cursor, (uint32_t)(end - cursor), state->depth_registers,
        PS5_DEPTH_REGISTER_COUNT, state->resources->shader, SHADER_BYTES,
        PS5_NATIVE_REGISTERS_CX);
    if (result != 0)
#ifdef PS5_RESOURCE_FOUNDATION
        return resource_compose_fail(state, resource_slot, -12);
#else
        return -12;
#endif
    struct ps5_pipeline_registers *pipeline = state->pipelines[frame->buffer];
    result = ps5_native_set_indirect(
        &cursor, (uint32_t)(end - cursor), pipeline->cx,
        PS5_PIPELINE_CX_REGISTERS, state->resources->shader, SHADER_BYTES,
        PS5_NATIVE_REGISTERS_CX);
    if (result != 0)
#ifdef PS5_RESOURCE_FOUNDATION
        return resource_compose_fail(state, resource_slot, -13);
#else
        return -13;
#endif
    result = ps5_native_set_indirect(
        &cursor, (uint32_t)(end - cursor), pipeline->uc,
        PS5_PIPELINE_UC_REGISTERS, state->resources->shader, SHADER_BYTES,
        PS5_NATIVE_REGISTERS_UC);
    if (result != 0)
#ifdef PS5_RESOURCE_FOUNDATION
        return resource_compose_fail(state, resource_slot, -14);
#else
        return -14;
#endif
    result = ps5_native_set_indirect(
        &cursor, (uint32_t)(end - cursor), pipeline->sh,
        PS5_PIPELINE_SH_REGISTERS, state->resources->shader, SHADER_BYTES,
        PS5_NATIVE_REGISTERS_SH);
    if (result != 0)
#ifdef PS5_RESOURCE_FOUNDATION
        return resource_compose_fail(state, resource_slot, -15);
#else
        return -15;
#endif
#ifdef PS5_BSP_VIEWER
#ifdef PS5_RESOURCE_FOUNDATION
    BspResourceComposeResult resource_composed = {0};
#ifdef PS5_REF_AGC_LIVE_PHASE7
    state->live_compose_stage = "live-world-clear";
    result = bsp_resource_compose_clear(
        &cursor, end, &state->resource_frames[resource_slot],
        state->clear_indices, state->resources->resource_heap,
        state->resources->resource_heap_bytes, state->draw_modifier,
        ps5_native_set_sh_direct, ps5_native_draw_index,
        &resource_composed);
    RefAgcGpuWorldStats live_world_stats = {0};
    if (result == 0 && ref_agc_gpu_world_cache_stats(
            &state->live_world_cache, &live_world_stats) !=
        REF_AGC_GPU_WORLD_OK)
        result = -1;
    RefAgcGpuWorldComposeResult live_opaque = {0};
    RefAgcGpuWorldComposeResult live_opaque_unlit = {0};
    RefAgcGpuWorldComposeResult live_alpha = {0};
    RefAgcGpuWorldComposeResult live_alpha_unlit = {0};
    if (result == 0 && live_world_stats.active) {
        state->live_compose_stage = "live-world-opaque-pipeline";
        result = bind_native_pipeline(
            state, &cursor, end, state->pipelines[frame->buffer]);
        if (result == 0) {
            state->live_compose_stage = "live-world-opaque-draw";
            result = ref_agc_gpu_world_compose(
                &cursor, end, &state->live_world_cache,
                REF_AGC_WORLD_DRAW_ALPHA_TEST |
                    REF_AGC_WORLD_DRAW_LIGHTMAP,
                REF_AGC_WORLD_DRAW_LIGHTMAP,
                state->resource_frames[resource_slot].map_constant_table,
                state->resources->resource_heap,
                state->resources->resource_heap_bytes,
                state->draw_modifier,
                ps5_native_set_sh_direct, ps5_native_draw_index,
                &live_opaque);
        }
        const GoldSrcRenderState live_opaque_unlit_state = {
            GOLDSRC_BLEND_OPAQUE, GOLDSRC_CULL_NONE, 1u, 0u, 0u, 0u,
        };
        Ps5GoldSrcPipelineBinding live_opaque_unlit_binding;
        if (result == 0) {
            state->live_compose_stage = "live-world-opaque-unlit-pipeline";
            result = bind_goldsrc_pipeline(
                state, &cursor, end, &live_opaque_unlit_state,
                frame->buffer, &live_opaque_unlit_binding);
        }
        if (result == 0) {
            state->live_compose_stage = "live-world-opaque-unlit-draw";
            result = ref_agc_gpu_world_compose(
                &cursor, end, &state->live_world_cache,
                REF_AGC_WORLD_DRAW_ALPHA_TEST |
                    REF_AGC_WORLD_DRAW_LIGHTMAP,
                0u,
                state->resource_frames[resource_slot].map_constant_table,
                state->resources->resource_heap,
                state->resources->resource_heap_bytes,
                live_opaque_unlit_binding.draw_modifier,
                ps5_native_set_sh_direct, ps5_native_draw_index,
                &live_opaque_unlit);
        }
        if (result == 0) {
            state->live_compose_stage = "live-world-alpha-pipeline";
            result = bind_native_pipeline(
                state, &cursor, end,
                state->alpha_test_pipelines[frame->buffer]);
        }
        if (result == 0) {
            state->live_compose_stage = "live-world-alpha-draw";
            result = ref_agc_gpu_world_compose(
                &cursor, end, &state->live_world_cache,
                REF_AGC_WORLD_DRAW_ALPHA_TEST |
                    REF_AGC_WORLD_DRAW_LIGHTMAP,
                REF_AGC_WORLD_DRAW_ALPHA_TEST |
                    REF_AGC_WORLD_DRAW_LIGHTMAP,
                state->resource_frames[resource_slot].map_constant_table,
                state->resources->resource_heap,
                state->resources->resource_heap_bytes,
                state->draw_modifier,
                ps5_native_set_sh_direct, ps5_native_draw_index,
                &live_alpha);
        }
        const GoldSrcRenderState live_alpha_unlit_state = {
            GOLDSRC_BLEND_ALPHA_TEST, GOLDSRC_CULL_NONE, 1u, 0u, 0u, 0u,
        };
        Ps5GoldSrcPipelineBinding live_alpha_unlit_binding;
        if (result == 0) {
            state->live_compose_stage = "live-world-alpha-unlit-pipeline";
            result = bind_goldsrc_pipeline(
                state, &cursor, end, &live_alpha_unlit_state,
                frame->buffer, &live_alpha_unlit_binding);
        }
        if (result == 0) {
            state->live_compose_stage = "live-world-alpha-unlit-draw";
            result = ref_agc_gpu_world_compose(
                &cursor, end, &state->live_world_cache,
                REF_AGC_WORLD_DRAW_ALPHA_TEST |
                    REF_AGC_WORLD_DRAW_LIGHTMAP,
                REF_AGC_WORLD_DRAW_ALPHA_TEST,
                state->resource_frames[resource_slot].map_constant_table,
                state->resources->resource_heap,
                state->resources->resource_heap_bytes,
                live_alpha_unlit_binding.draw_modifier,
                ps5_native_set_sh_direct, ps5_native_draw_index,
                &live_alpha_unlit);
        }
        if (result == 0 &&
            (live_opaque.draws + live_opaque_unlit.draws +
                 live_alpha.draws + live_alpha_unlit.draws !=
                 live_world_stats.draw_count ||
             live_opaque.indices + live_opaque_unlit.indices +
                 live_alpha.indices + live_alpha_unlit.indices !=
                 live_world_stats.index_count))
            result = -2;
    }
    if (result != 0)
        return resource_compose_fail(state, resource_slot, -16);
#else
    const uint8_t *map_draw_mask = 0;
    uint32_t map_draw_mask_bytes = 0u;
#ifdef PS5_GOLDSRC_VISIBILITY_GATE
    map_draw_mask =
        state->goldsrc_visibility_frames[resource_slot].draw_mask;
    map_draw_mask_bytes =
        state->goldsrc_visibility_frames[resource_slot].draw_mask_bytes;
#endif
#ifdef PS5_GOLDSRC_PHASE4
    result = bsp_resource_compose_clear(
        &cursor, end, &state->resource_frames[resource_slot],
        state->clear_indices, state->resources->resource_heap,
        state->resources->resource_heap_bytes, state->draw_modifier,
        ps5_native_set_sh_direct, ps5_native_draw_index,
        &resource_composed);
    const GoldSrcRenderState opaque_state =
#ifdef PS5_GOLDSRC_STATE_MATRIX_GATE
        matrix_case->state;
#else
    {
        GOLDSRC_BLEND_OPAQUE, GOLDSRC_CULL_NONE, 1u, 0u, 1u, 0u,
    };
#endif
    Ps5GoldSrcPipelineBinding opaque_binding;
    if (result == 0)
        result = bind_goldsrc_pipeline(
            state, &cursor, end, &opaque_state, frame->buffer,
            &opaque_binding);
#ifdef PS5_GOLDSRC_VIEWPORT_GATE
    if (result == 0)
        result = ps5_native_set_indirect(
            &cursor, (uint32_t)(end - cursor),
            state->goldsrc_viewport_inset,
            PS5_VIEWPORT_SCISSOR_REGISTER_COUNT,
            state->resources->shader, SHADER_BYTES,
            PS5_NATIVE_REGISTERS_CX);
#endif
    if (result == 0)
        result = bsp_resource_compose_map_pass_filtered(
            &cursor, end, &state->resource_frames[resource_slot],
            &state->bsp_bundle, state->clear_indices,
            BSP_RESOURCE_DRAW_OPAQUE, 0,
            map_draw_mask, map_draw_mask_bytes,
            state->resources->resource_heap,
            state->resources->resource_heap_bytes,
            opaque_binding.draw_modifier, ps5_native_set_sh_direct,
            ps5_native_draw_index, &resource_composed);
#else
    result = bsp_resource_compose_map_pass_filtered(
        &cursor, end, &state->resource_frames[resource_slot],
        &state->bsp_bundle, state->clear_indices,
        BSP_RESOURCE_DRAW_OPAQUE, 1,
        map_draw_mask, map_draw_mask_bytes,
        state->resources->resource_heap,
        state->resources->resource_heap_bytes, state->draw_modifier,
        ps5_native_set_sh_direct, ps5_native_draw_index,
        &resource_composed);
#endif
#ifdef PS5_GOLDSRC_PHASE4
    const GoldSrcRenderState alpha_state = {
        GOLDSRC_BLEND_ALPHA_TEST, GOLDSRC_CULL_NONE, 1u, 0u, 1u, 0u,
    };
    Ps5GoldSrcPipelineBinding alpha_binding;
    if (result == 0)
        result = bind_goldsrc_pipeline(
            state, &cursor, end, &alpha_state, frame->buffer,
            &alpha_binding);
#ifdef PS5_GOLDSRC_VIEWPORT_GATE
    if (result == 0)
        result = ps5_native_set_indirect(
            &cursor, (uint32_t)(end - cursor),
            state->goldsrc_viewport_inset,
            PS5_VIEWPORT_SCISSOR_REGISTER_COUNT,
            state->resources->shader, SHADER_BYTES,
            PS5_NATIVE_REGISTERS_CX);
#endif
#else
    struct ps5_pipeline_registers *alpha_test =
        state->alpha_test_pipelines[frame->buffer];
#ifdef PS5_TEXTURE_ALPHA_GATE
    const int alpha_test_enabled =
        frame->frame_index != BSP_GATE_FRAME_COUNT - 2u;
    if (!alpha_test_enabled)
        alpha_test = state->pipelines[frame->buffer];
#endif
    if (result == 0)
        result = ps5_native_set_indirect(
            &cursor, (uint32_t)(end - cursor), alpha_test->cx,
            PS5_PIPELINE_CX_REGISTERS, state->resources->shader,
            SHADER_BYTES, PS5_NATIVE_REGISTERS_CX);
    if (result == 0)
        result = ps5_native_set_indirect(
            &cursor, (uint32_t)(end - cursor), alpha_test->uc,
            PS5_PIPELINE_UC_REGISTERS, state->resources->shader,
            SHADER_BYTES, PS5_NATIVE_REGISTERS_UC);
    if (result == 0)
        result = ps5_native_set_indirect(
            &cursor, (uint32_t)(end - cursor), alpha_test->sh,
            PS5_PIPELINE_SH_REGISTERS, state->resources->shader,
            SHADER_BYTES, PS5_NATIVE_REGISTERS_SH);
#endif
    if (result == 0)
        result = bsp_resource_compose_map_pass_filtered(
            &cursor, end, &state->resource_frames[resource_slot],
            &state->bsp_bundle, state->clear_indices,
            BSP_RESOURCE_DRAW_ALPHA_TEST, 0,
            map_draw_mask, map_draw_mask_bytes,
            state->resources->resource_heap,
            state->resources->resource_heap_bytes,
#ifdef PS5_GOLDSRC_PHASE4
            alpha_binding.draw_modifier,
#else
            state->draw_modifier,
#endif
            ps5_native_set_sh_direct, ps5_native_draw_index,
            &resource_composed);
#ifdef PS5_GOLDSRC_VIEWPORT_GATE
    if (result == 0)
        result = ps5_native_set_indirect(
            &cursor, (uint32_t)(end - cursor),
            state->goldsrc_viewport_full,
            PS5_VIEWPORT_SCISSOR_REGISTER_COUNT,
            state->resources->shader, SHADER_BYTES,
            PS5_NATIVE_REGISTERS_CX);
    if (result == 0 && frame->frame_index == 0u)
        (void)ps5log_printf(PS5LOG_MARK,
            "GOLDSRC_VIEWPORT_FRAME schema=1 frame=%llu slot=%u "
            "sequence=full-clear,inset-opaque,inset-alpha,full-restore "
            "inset_scissor_tl=%08x inset_scissor_br=%08x "
            "full_scissor_tl=%08x full_scissor_br=%08x",
            (unsigned long long)frame->frame_index, resource_slot,
            state->goldsrc_viewport_inset[6].value,
            state->goldsrc_viewport_inset[7].value,
            state->goldsrc_viewport_full[6].value,
            state->goldsrc_viewport_full[7].value);
#endif
#ifdef PS5_GOLDSRC_PHASE4
    if (result == 0 && frame->frame_index == 0u)
        (void)ps5log_printf(PS5LOG_MARK,
            "GOLDSRC_STATE_FRAME schema=1 frame=%llu slot=%u "
            "opaque_key=%u opaque_pass=%u opaque_shader=%s "
            "opaque_blend=%08x opaque_depth=%08x opaque_raster=%08x "
            "alpha_key=%u alpha_pass=%u alpha_shader=%s "
            "alpha_blend=%08x alpha_depth=%08x alpha_raster=%08x",
            (unsigned long long)frame->frame_index, resource_slot,
            opaque_binding.permutation->key,
            opaque_binding.permutation->pass,
            goldsrc_pipeline_shader_variant_name(
                opaque_binding.permutation->shader),
            opaque_binding.dynamic_cx[0].value,
            opaque_binding.dynamic_cx[1].value,
            opaque_binding.dynamic_cx[2].value,
            alpha_binding.permutation->key,
            alpha_binding.permutation->pass,
            goldsrc_pipeline_shader_variant_name(
                alpha_binding.permutation->shader),
            alpha_binding.dynamic_cx[0].value,
            alpha_binding.dynamic_cx[1].value,
            alpha_binding.dynamic_cx[2].value);
#ifdef PS5_GOLDSRC_STATE_MATRIX_GATE
    if (result == 0 &&
        frame->frame_index % GOLDSRC_STATE_MATRIX_HOLD_FRAMES == 0u)
        (void)ps5log_printf(PS5LOG_MARK,
            "GOLDSRC_STATE_MATRIX_FRAME schema=1 frame=%llu slot=%u "
            "case=%u name=%s key=%u blend=%u depth_write=%u cull=%u "
            "fog=%u lightmap=%u shader=%s blend_cx=%08x "
            "depth_cx=%08x raster_cx=%08x",
            (unsigned long long)frame->frame_index, resource_slot,
            matrix_index, matrix_case->name,
            opaque_binding.permutation->key,
            matrix_case->state.blend, matrix_case->state.depth_write,
            matrix_case->state.cull, matrix_case->state.fog,
            matrix_case->state.lightmap,
            goldsrc_pipeline_shader_variant_name(
                opaque_binding.permutation->shader),
            opaque_binding.dynamic_cx[0].value,
            opaque_binding.dynamic_cx[1].value,
            opaque_binding.dynamic_cx[2].value);
#endif
#endif
#ifdef PS5_TEXTURE_ALPHA_GATE
    if (result == 0 &&
        (frame->frame_index == 0u ||
         PS5_BSP_FINAL_WINDOW(frame->frame_index)))
        (void)ps5log_printf(PS5LOG_MARK,
            "ALPHA_TEST_FRAME frame=%llu slot=%u mode=%s "
            "opaque_draws=%u alpha_test_draws=%u sampler=anisotropic4x "
            "lightmap_pattern=%u",
            (unsigned long long)frame->frame_index, resource_slot,
            alpha_test_enabled ? "alpha-test" : "opaque-control",
            resource_composed.opaque_draws,
            resource_composed.alpha_test_draws,
            state->dynamic_lightmap_updates[resource_slot].pattern);
#endif
    const int sky_enabled =
#ifdef PS5_TEXTURE_SKY_GATE
        frame->frame_index != BSP_GATE_FRAME_COUNT - 2u;
#else
        1;
#endif
    struct ps5_pipeline_registers *sky =
        state->sky_pipelines[frame->buffer];
    if (result == 0 && sky_enabled)
        result = ps5_native_set_indirect(
            &cursor, (uint32_t)(end - cursor), sky->cx,
            PS5_PIPELINE_CX_REGISTERS, state->resources->shader,
            SHADER_BYTES, PS5_NATIVE_REGISTERS_CX);
    if (result == 0 && sky_enabled)
        result = ps5_native_set_indirect(
            &cursor, (uint32_t)(end - cursor), sky->uc,
            PS5_PIPELINE_UC_REGISTERS, state->resources->shader,
            SHADER_BYTES, PS5_NATIVE_REGISTERS_UC);
    if (result == 0 && sky_enabled)
        result = ps5_native_set_indirect(
            &cursor, (uint32_t)(end - cursor), sky->sh,
            PS5_PIPELINE_SH_REGISTERS, state->resources->shader,
            SHADER_BYTES, PS5_NATIVE_REGISTERS_SH);
    if (result == 0 && sky_enabled)
        result = bsp_resource_compose_map_pass_filtered(
            &cursor, end, &state->resource_frames[resource_slot],
            &state->bsp_bundle, state->clear_indices,
            BSP_RESOURCE_DRAW_SKY, 0,
            map_draw_mask, map_draw_mask_bytes,
            state->resources->resource_heap,
            state->resources->resource_heap_bytes, state->draw_modifier,
            ps5_native_set_sh_direct, ps5_native_draw_index,
            &resource_composed);
#ifdef PS5_GOLDSRC_VISIBILITY_GATE
    if (result == 0 &&
        (frame->frame_index % GOLDSRC_VISIBILITY_HOLD_FRAMES == 0u ||
         frame->frame_index + 1u == BSP_GATE_FRAME_COUNT)) {
        const GoldSrcVisibilityFrame *const visibility =
            &state->goldsrc_visibility_frames[resource_slot];
        if (resource_composed.map_draws != visibility->selected_draws ||
            resource_composed.opaque_draws != visibility->opaque_draws ||
            resource_composed.alpha_test_draws != visibility->alpha_draws ||
            resource_composed.sky_draws != visibility->sky_draws)
            result = -18;
        else
            (void)ps5log_printf(PS5LOG_MARK,
                "GOLDSRC_VISIBILITY_DRAW schema=1 frame=%llu slot=%u "
                "mode=%u name=%s draws=%u opaque=%u alpha=%u sky=%u "
                "pvs=%s frustum=%s ownership=fence+videoout",
                (unsigned long long)frame->frame_index, resource_slot,
                visibility->mode,
                goldsrc_visibility_mode_name(visibility->mode),
                resource_composed.map_draws,
                resource_composed.opaque_draws,
                resource_composed.alpha_test_draws,
                resource_composed.sky_draws,
                (visibility->mode == GOLDSRC_VISIBILITY_MODE_PVS ||
                 visibility->mode == GOLDSRC_VISIBILITY_MODE_COMBINED)
                    ? "on" : "off",
                (visibility->mode == GOLDSRC_VISIBILITY_MODE_FRUSTUM ||
                 visibility->mode == GOLDSRC_VISIBILITY_MODE_COMBINED)
                    ? "on" : "off");
    }
#endif
#ifdef PS5_TEXTURE_SKY_GATE
    if (result == 0 &&
        (frame->frame_index == 0u ||
         PS5_BSP_FINAL_WINDOW(frame->frame_index)))
        (void)ps5log_printf(PS5LOG_MARK,
            "SKY_PASS_FRAME frame=%llu slot=%u mode=%s "
            "sky_draws=%u expected_sky_draws=%u sampler=anisotropic4x "
            "lightmap_pattern=%u",
            (unsigned long long)frame->frame_index, resource_slot,
            sky_enabled ? "sky-pass" : "skip-control",
            sky_enabled ? resource_composed.sky_draws : 0u,
            state->sky_plan.draw_count,
            state->dynamic_lightmap_updates[resource_slot].pattern);
#endif
#ifdef PS5_GOLDSRC_SPRITE_PARTICLE_GATE
    GoldSrcSpriteParticleComposeResult effect_composed = {0};
    Ps5GoldSrcPipelineBinding effect_alpha_binding;
    Ps5GoldSrcPipelineBinding effect_additive_binding;
    const GoldSrcRenderState effect_alpha_state = {
        GOLDSRC_BLEND_ALPHA, GOLDSRC_CULL_NONE, 0u, 0u, 0u, 0u,
    };
    const GoldSrcRenderState effect_additive_state = {
        GOLDSRC_BLEND_ADDITIVE, GOLDSRC_CULL_NONE, 0u, 0u, 0u, 0u,
    };
    GoldSrcSpriteParticleFrame *const effects =
        &state->goldsrc_effect_frames[resource_slot];
    const int effect_has_sprite =
        effects->mode == GOLDSRC_EFFECT_MODE_SPRITE ||
        effects->mode == GOLDSRC_EFFECT_MODE_COMBINED;
    const int effect_has_particles =
        effects->mode == GOLDSRC_EFFECT_MODE_PARTICLES ||
        effects->mode == GOLDSRC_EFFECT_MODE_COMBINED;
    if (result == 0 && (effect_has_sprite || effect_has_particles))
        result = bind_goldsrc_pipeline(
            state, &cursor, end, &effect_alpha_state, frame->buffer,
            &effect_alpha_binding);
    if (result == 0 && effect_has_sprite)
        result = goldsrc_sprite_particle_compose_range(
            &cursor, end, effects, effects->sprite_first_index,
            effects->sprite_index_count,
            state->resources->resource_heap,
            state->resources->resource_heap_bytes,
            effect_alpha_binding.draw_modifier, ps5_native_set_sh_direct,
            ps5_native_draw_index, &effect_composed);
    if (result == 0 && effect_has_particles)
        result = goldsrc_sprite_particle_compose_range(
            &cursor, end, effects, effects->alpha_particle_first_index,
            effects->alpha_particle_index_count,
            state->resources->resource_heap,
            state->resources->resource_heap_bytes,
            effect_alpha_binding.draw_modifier, ps5_native_set_sh_direct,
            ps5_native_draw_index, &effect_composed);
    if (result == 0 && effect_has_particles)
        result = bind_goldsrc_pipeline(
            state, &cursor, end, &effect_additive_state, frame->buffer,
            &effect_additive_binding);
    if (result == 0 && effect_has_particles)
        result = goldsrc_sprite_particle_compose_range(
            &cursor, end, effects, effects->additive_particle_first_index,
            effects->additive_particle_index_count,
            state->resources->resource_heap,
            state->resources->resource_heap_bytes,
            effect_additive_binding.draw_modifier,
            ps5_native_set_sh_direct, ps5_native_draw_index,
            &effect_composed);
    if (result == 0 &&
        (frame->frame_index % GOLDSRC_EFFECT_HOLD_FRAMES == 0u ||
         frame->frame_index + 1u == BSP_GATE_FRAME_COUNT))
        (void)ps5log_printf(PS5LOG_MARK,
            "GOLDSRC_SPRITE_PARTICLE_DRAW schema=1 frame=%llu slot=%u "
            "mode=%u name=%s alpha_key=%u additive_key=%u shader=%s "
            "draws=%u indices=%u sprite_draws=%u particle_draws=%u "
            "depth_write=false cull=none lightmap=false "
            "ownership=fence+videoout",
            (unsigned long long)frame->frame_index, resource_slot,
            effects->mode,
            goldsrc_sprite_particle_mode_name(effects->mode),
            (effect_has_sprite || effect_has_particles)
                ? effect_alpha_binding.permutation->key : 0u,
            effect_has_particles
                ? effect_additive_binding.permutation->key : 0u,
            (effect_has_sprite || effect_has_particles)
                ? goldsrc_pipeline_shader_variant_name(
                      effect_alpha_binding.permutation->shader)
                : "none",
            effect_composed.draws, effect_composed.indices,
            effect_has_sprite ? 1u : 0u,
            effect_has_particles ? 2u : 0u);
#endif
#ifdef PS5_GOLDSRC_STUDIO_GATE
    GoldSrcStudioComposeResult studio_composed = {0};
    Ps5GoldSrcPipelineBinding studio_opaque_binding;
    Ps5GoldSrcPipelineBinding studio_additive_binding;
    const GoldSrcRenderState studio_opaque_state = {
        GOLDSRC_BLEND_OPAQUE, GOLDSRC_CULL_NONE, 1u, 0u, 0u, 0u,
    };
    const GoldSrcRenderState studio_additive_state = {
        GOLDSRC_BLEND_ADDITIVE, GOLDSRC_CULL_NONE, 0u, 0u, 0u, 0u,
    };
    GoldSrcStudioFrame *const studio =
        &state->goldsrc_studio_frames[resource_slot];
    const int studio_has_textured =
        studio->mode == GOLDSRC_STUDIO_MODE_TEXTURED ||
        studio->mode == GOLDSRC_STUDIO_MODE_COMBINED;
    const int studio_has_chrome =
        studio->mode == GOLDSRC_STUDIO_MODE_CHROME ||
        studio->mode == GOLDSRC_STUDIO_MODE_COMBINED;
    const int studio_has_additive =
        studio->mode == GOLDSRC_STUDIO_MODE_ADDITIVE ||
        studio->mode == GOLDSRC_STUDIO_MODE_COMBINED;
    if (result == 0 && (studio_has_textured || studio_has_chrome))
        result = bind_goldsrc_pipeline(
            state, &cursor, end, &studio_opaque_state, frame->buffer,
            &studio_opaque_binding);
    if (result == 0 && studio_has_textured)
        result = goldsrc_studio_compose_instance(
            &cursor, end, studio, &state->goldsrc_studio_bundle,
            GOLDSRC_STUDIO_INSTANCE_TEXTURED,
            state->resources->resource_heap,
            state->resources->resource_heap_bytes,
            studio_opaque_binding.draw_modifier,
            ps5_native_set_sh_direct, ps5_native_draw_index,
            &studio_composed);
    if (result == 0 && studio_has_chrome)
        result = goldsrc_studio_compose_instance(
            &cursor, end, studio, &state->goldsrc_studio_bundle,
            GOLDSRC_STUDIO_INSTANCE_CHROME,
            state->resources->resource_heap,
            state->resources->resource_heap_bytes,
            studio_opaque_binding.draw_modifier,
            ps5_native_set_sh_direct, ps5_native_draw_index,
            &studio_composed);
    if (result == 0 && studio_has_additive)
        result = bind_goldsrc_pipeline(
            state, &cursor, end, &studio_additive_state, frame->buffer,
            &studio_additive_binding);
    if (result == 0 && studio_has_additive)
        result = goldsrc_studio_compose_instance(
            &cursor, end, studio, &state->goldsrc_studio_bundle,
            GOLDSRC_STUDIO_INSTANCE_ADDITIVE,
            state->resources->resource_heap,
            state->resources->resource_heap_bytes,
            studio_additive_binding.draw_modifier,
            ps5_native_set_sh_direct, ps5_native_draw_index,
            &studio_composed);
    if (result == 0 &&
        (frame->frame_index % GOLDSRC_STUDIO_HOLD_FRAMES == 0u ||
         frame->frame_index + 1u == BSP_GATE_FRAME_COUNT))
        (void)ps5log_printf(PS5LOG_MARK,
            "GOLDSRC_STUDIO_DRAW schema=1 frame=%llu slot=%u "
            "mode=%u name=%s opaque_key=%u additive_key=%u shader=%s "
            "instances=%u draws=%u indices=%u texture_binds=%u "
            "depth_write=opaque-only cull=none lightmap=false "
            "ownership=fence+videoout",
            (unsigned long long)frame->frame_index, resource_slot,
            studio->mode, goldsrc_studio_mode_name(studio->mode),
            (studio_has_textured || studio_has_chrome)
                ? studio_opaque_binding.permutation->key : 0u,
            studio_has_additive
                ? studio_additive_binding.permutation->key : 0u,
            (studio_has_textured || studio_has_chrome ||
             studio_has_additive) ? "surface" : "none",
            (uint32_t)(studio_has_textured + studio_has_chrome +
                       studio_has_additive),
            studio_composed.draws, studio_composed.indices,
            studio_composed.textures);
#endif
#ifdef PS5_GOLDSRC_BRUSH_GATE
    GoldSrcBrushComposeResult brush_composed = {0};
    Ps5GoldSrcPipelineBinding brush_opaque_binding;
    Ps5GoldSrcPipelineBinding brush_alpha_binding;
    Ps5GoldSrcPipelineBinding brush_additive_binding;
    const GoldSrcRenderState brush_opaque_state = {
        GOLDSRC_BLEND_OPAQUE, GOLDSRC_CULL_NONE, 1u, 0u, 0u, 0u,
    };
    const GoldSrcRenderState brush_alpha_state = {
        GOLDSRC_BLEND_ALPHA, GOLDSRC_CULL_NONE, 0u, 0u, 0u, 0u,
    };
    const GoldSrcRenderState brush_additive_state = {
        GOLDSRC_BLEND_ADDITIVE, GOLDSRC_CULL_NONE, 0u, 0u, 0u, 0u,
    };
    GoldSrcBrushFrame *const brush =
        &state->goldsrc_brush_frames[resource_slot];
    const int brush_has_opaque =
        brush->mode == GOLDSRC_BRUSH_MODE_OPAQUE ||
        brush->mode == GOLDSRC_BRUSH_MODE_COMBINED;
    const int brush_has_alpha =
        brush->mode == GOLDSRC_BRUSH_MODE_ALPHA ||
        brush->mode == GOLDSRC_BRUSH_MODE_COMBINED;
    const int brush_has_additive =
        brush->mode == GOLDSRC_BRUSH_MODE_ADDITIVE ||
        brush->mode == GOLDSRC_BRUSH_MODE_COMBINED;
    if (result == 0 && brush_has_opaque)
        result = bind_goldsrc_pipeline(
            state, &cursor, end, &brush_opaque_state, frame->buffer,
            &brush_opaque_binding);
    if (result == 0 && brush_has_opaque)
        result = goldsrc_brush_compose_instance(
            &cursor, end, brush, &state->goldsrc_brush_plan,
            &state->resource_frames[resource_slot], &state->bsp_bundle,
            GOLDSRC_BRUSH_INSTANCE_OPAQUE,
            state->resources->resource_heap,
            state->resources->resource_heap_bytes,
            brush_opaque_binding.draw_modifier,
            ps5_native_set_sh_direct, ps5_native_draw_index,
            &brush_composed);
    if (result == 0 && brush_has_alpha)
        result = bind_goldsrc_pipeline(
            state, &cursor, end, &brush_alpha_state, frame->buffer,
            &brush_alpha_binding);
    if (result == 0 && brush_has_alpha)
        result = goldsrc_brush_compose_instance(
            &cursor, end, brush, &state->goldsrc_brush_plan,
            &state->resource_frames[resource_slot], &state->bsp_bundle,
            GOLDSRC_BRUSH_INSTANCE_ALPHA,
            state->resources->resource_heap,
            state->resources->resource_heap_bytes,
            brush_alpha_binding.draw_modifier,
            ps5_native_set_sh_direct, ps5_native_draw_index,
            &brush_composed);
#ifdef PS5_GOLDSRC_PHASE4_FINAL_GATE
    if (result == 0 && brush->mode == GOLDSRC_BRUSH_MODE_COMBINED)
        result = goldsrc_brush_compose_instance(
            &cursor, end, brush, &state->goldsrc_brush_plan,
            &state->resource_frames[resource_slot], &state->bsp_bundle,
            GOLDSRC_BRUSH_INSTANCE_WATER,
            state->resources->resource_heap,
            state->resources->resource_heap_bytes,
            brush_alpha_binding.draw_modifier,
            ps5_native_set_sh_direct, ps5_native_draw_index,
            &brush_composed);
    if (result == 0 && brush->mode == GOLDSRC_BRUSH_MODE_COMBINED)
        result = goldsrc_brush_compose_instance(
            &cursor, end, brush, &state->goldsrc_brush_plan,
            &state->resource_frames[resource_slot], &state->bsp_bundle,
            GOLDSRC_BRUSH_INSTANCE_GLASS,
            state->resources->resource_heap,
            state->resources->resource_heap_bytes,
            brush_alpha_binding.draw_modifier,
            ps5_native_set_sh_direct, ps5_native_draw_index,
            &brush_composed);
#endif
    if (result == 0 && brush_has_additive)
        result = bind_goldsrc_pipeline(
            state, &cursor, end, &brush_additive_state, frame->buffer,
            &brush_additive_binding);
    if (result == 0 && brush_has_additive)
        result = goldsrc_brush_compose_instance(
            &cursor, end, brush, &state->goldsrc_brush_plan,
            &state->resource_frames[resource_slot], &state->bsp_bundle,
            GOLDSRC_BRUSH_INSTANCE_ADDITIVE,
            state->resources->resource_heap,
            state->resources->resource_heap_bytes,
            brush_additive_binding.draw_modifier,
            ps5_native_set_sh_direct, ps5_native_draw_index,
            &brush_composed);
    if (result == 0 &&
        (frame->frame_index % GOLDSRC_BRUSH_HOLD_FRAMES == 0u ||
         frame->frame_index + 1u == BSP_GATE_FRAME_COUNT))
        (void)ps5log_printf(PS5LOG_MARK,
            "GOLDSRC_BRUSH_DRAW schema=1 frame=%llu slot=%u "
            "mode=%u name=%s opaque_key=%u alpha_key=%u additive_key=%u "
            "instances=%u draws=%u indices=%u texture_binds=%u "
            "depth_write=opaque-only cull=none lightmap=false "
            "ownership=fence+videoout",
            (unsigned long long)frame->frame_index, resource_slot,
            brush->mode, goldsrc_brush_mode_name(brush->mode),
            brush_has_opaque
                ? brush_opaque_binding.permutation->key : 0u,
            brush_has_alpha
                ? brush_alpha_binding.permutation->key : 0u,
            brush_has_additive
                ? brush_additive_binding.permutation->key : 0u,
            brush_composed.instances, brush_composed.draws,
            brush_composed.indices, brush_composed.texture_binds);
#endif
    struct ps5_pipeline_registers *overlay =
        state->overlay_pipelines[resource_slot];
    if (result == 0)
        result = ps5_native_set_indirect(
            &cursor, (uint32_t)(end - cursor), overlay->cx,
            PS5_PIPELINE_CX_REGISTERS, state->resources->shader,
            SHADER_BYTES, PS5_NATIVE_REGISTERS_CX);
    if (result == 0)
        result = ps5_native_set_indirect(
            &cursor, (uint32_t)(end - cursor), overlay->uc,
            PS5_PIPELINE_UC_REGISTERS, state->resources->shader,
            SHADER_BYTES, PS5_NATIVE_REGISTERS_UC);
    if (result == 0)
        result = ps5_native_set_indirect(
            &cursor, (uint32_t)(end - cursor), overlay->sh,
            PS5_PIPELINE_SH_REGISTERS, state->resources->shader,
            SHADER_BYTES, PS5_NATIVE_REGISTERS_SH);
    if (result == 0)
        result = ps5_native_set_indirect(
            &cursor, (uint32_t)(end - cursor),
            state->overlay_depth_disabled,
            PS5_DEPTH_DISABLED_REGISTER_COUNT, state->resources->shader,
            SHADER_BYTES, PS5_NATIVE_REGISTERS_CX);
    if (result == 0)
        result = bsp_resource_compose_overlay(
            &cursor, end, &state->resource_frames[resource_slot],
            state->resources->resource_heap,
            state->resources->resource_heap_bytes,
            state->overlay_draw_modifier, ps5_native_set_sh_direct,
            ps5_native_draw_index, &resource_composed);
#ifdef PS5_GOLDSRC_2D_GATE
    GoldSrc2DComposeResult screen_composed = {0};
    GoldSrcRenderState screen_alpha_state;
    GoldSrcRenderState screen_additive_state;
    Ps5GoldSrcPipelineBinding screen_alpha_binding;
    Ps5GoldSrcPipelineBinding screen_additive_binding;
    GoldSrc2DFrame *const screen =
        &state->goldsrc_2d_frames[resource_slot];
    if (result == 0 &&
        (goldsrc_render_state_2d(GOLDSRC_BLEND_ALPHA,
                                 &screen_alpha_state) != 0 ||
         goldsrc_render_state_2d(GOLDSRC_BLEND_ADDITIVE,
                                 &screen_additive_state) != 0))
        result = -1;
    if (result == 0)
        result = bind_goldsrc_pipeline(
            state, &cursor, end, &screen_alpha_state, frame->buffer,
            &screen_alpha_binding);
    if (result == 0)
        result = goldsrc_2d_compose_range(
            &cursor, end, screen, screen->alpha_first_index,
            screen->alpha_index_count, state->resources->resource_heap,
            state->resources->resource_heap_bytes,
            screen_alpha_binding.draw_modifier, ps5_native_set_sh_direct,
            ps5_native_draw_index, &screen_composed);
    if (result == 0)
        result = bind_goldsrc_pipeline(
            state, &cursor, end, &screen_additive_state, frame->buffer,
            &screen_additive_binding);
    if (result == 0)
        result = goldsrc_2d_compose_range(
            &cursor, end, screen, screen->additive_first_index,
            screen->additive_index_count,
            state->resources->resource_heap,
            state->resources->resource_heap_bytes,
            screen_additive_binding.draw_modifier,
            ps5_native_set_sh_direct, ps5_native_draw_index,
            &screen_composed);
    if (result == 0 &&
        (frame->frame_index == 0u ||
         frame->frame_index + 1u == BSP_GATE_FRAME_COUNT))
        (void)ps5log_printf(PS5LOG_MARK,
            "GOLDSRC_2D_FRAME schema=1 frame=%llu slot=%u "
            "alpha_key=%u additive_key=%u shader=%s draws=%u "
            "indices=%u hud_quads=%u console_quads=%u menu_quads=%u "
            "font_quads=%u atlas_hash=%016llx layout_hash=%016llx "
            "transient_bytes=%llu ownership=fence+videoout",
            (unsigned long long)frame->frame_index, resource_slot,
            screen_alpha_binding.permutation->key,
            screen_additive_binding.permutation->key,
            goldsrc_pipeline_shader_variant_name(
                screen_alpha_binding.permutation->shader),
            screen_composed.draws, screen_composed.indices,
            screen->hud_quads, screen->console_quads,
            screen->menu_quads, screen->font_quads,
            (unsigned long long)screen->atlas_hash,
            (unsigned long long)screen->layout_hash,
            (unsigned long long)screen->transient_bytes);
#endif
#ifdef PS5_GOLDSRC_VISIBILITY_GATE
    const GoldSrcVisibilityFrame *const visibility_expected =
        &state->goldsrc_visibility_frames[resource_slot];
    const uint32_t expected_sky_draws =
        sky_enabled ? visibility_expected->sky_draws : 0u;
    const uint32_t expected_map_draws =
        visibility_expected->selected_draws -
        (sky_enabled ? 0u : visibility_expected->sky_draws);
#else
    const uint32_t expected_sky_draws =
        sky_enabled ? state->sky_plan.draw_count : 0u;
    const uint32_t expected_map_draws =
        state->bsp_bundle.draw_count -
        (sky_enabled ? 0u : state->sky_plan.draw_count);
#endif
    if (result != 0 ||
        resource_composed.map_draws != expected_map_draws ||
        resource_composed.sky_draws != expected_sky_draws ||
        resource_composed.opaque_draws +
                resource_composed.alpha_test_draws +
                resource_composed.sky_draws !=
            expected_map_draws ||
        resource_composed.overlay_draws != 1u)
        return resource_compose_fail(state, resource_slot, -16);
#ifdef PS5_GOLDSRC_2D_GATE
    if (screen_composed.draws != 2u ||
        screen_composed.indices !=
            screen->alpha_index_count + screen->additive_index_count)
        return resource_compose_fail(state, resource_slot, -16);
#endif
#ifdef PS5_GOLDSRC_SPRITE_PARTICLE_GATE
    const uint32_t expected_effect_draws =
        effects->mode == GOLDSRC_EFFECT_MODE_CONTROL ? 0u :
        effects->mode == GOLDSRC_EFFECT_MODE_SPRITE ? 1u :
        effects->mode == GOLDSRC_EFFECT_MODE_PARTICLES ? 2u : 3u;
    const uint32_t expected_effect_indices =
        (effect_has_sprite ? effects->sprite_index_count : 0u) +
        (effect_has_particles
            ? effects->alpha_particle_index_count +
                  effects->additive_particle_index_count
            : 0u);
    if (effect_composed.draws != expected_effect_draws ||
        effect_composed.indices != expected_effect_indices)
        return resource_compose_fail(state, resource_slot, -16);
#endif
#endif /* !PS5_REF_AGC_LIVE_PHASE7 */
#elif defined(PS5_BSP_TEXTURED)
    BspFlatComposeResult composed = {0, 0};
    result = bsp_textured_compose(
        &cursor, end, state->bsp_draws, state->bsp_texture_bindings,
        state->bsp_draw_count, state->resources->bsp,
        state->resources->bsp_bytes, state->draw_modifier,
        ps5_native_set_sh_direct, ps5_native_draw_index, &composed);
    uint32_t expected_dwords = 0u;
    if (result != 0 ||
        bsp_textured_required_dwords(state->bsp_draw_count,
                                     &expected_dwords) != 0 ||
        composed.draws != state->bsp_draw_count ||
        composed.command_dwords != expected_dwords)
        return -16;
#else
    BspFlatComposeResult composed = {0, 0};
    result = bsp_flat_compose(
        &cursor, end, state->bsp_draws, state->bsp_draw_count,
        state->resources->bsp, state->resources->bsp_bytes,
        state->draw_modifier, ps5_native_set_sh_direct,
        ps5_native_draw_index, &composed);
    uint32_t expected_dwords = 0u;
    if (result != 0 ||
        bsp_flat_required_dwords(state->bsp_draw_count,
                                 &expected_dwords) != 0 ||
        composed.draws != state->bsp_draw_count ||
        composed.command_dwords != expected_dwords)
        return -16;
#endif
#else
    GearsRendererComposeResult composed = {0, 0};
    result = gears_renderer_compose_frame(
        &cursor, end, &state->clear_draw, frame->draws,
        state->draw_modifier, ps5_native_set_sh_direct,
        ps5_native_draw_auto, &composed);
    if (result != 0 || composed.draws != 4u ||
        composed.command_dwords != 120u)
        return -16;
#endif
    state->cursor[frame->buffer] = cursor;
    return 0;
}

static int frame_submit(const GearsAnimationFrame *frame, void *opaque)
{
    struct native_renderer *state = opaque;
    if (!frame || !state || frame->buffer >= 2u ||
        !state->cursor[frame->buffer])
        return -1;
    const unsigned slot = frame->buffer;
#ifdef PS5_RESOURCE_FOUNDATION
    if (ps5_transient_ring_seal(&state->transient_ring, slot,
                                frame->token) != PS5_TRANSIENT_OK)
        return -2;
    if (frame->frame_index == 0u ||
        frame->frame_index + 1u == BSP_GATE_FRAME_COUNT)
        (void)ps5log_printf(PS5LOG_MARK,
            "RESOURCE_FRAME_SEALED frame=%llu slot=%u token=%llu "
            "retirement=fence+videoout",
            (unsigned long long)frame->frame_index, slot,
            (unsigned long long)frame->token);
#endif
    struct ps5_present_stream stream = {
        state->commands[slot],
        state->commands[slot] +
            state->command_slot_bytes / sizeof(uint32_t),
        state->cursor[slot], 0
    };
    struct ps5_submission_input input = {
        .stream = &stream,
        .set_flip = ps5_native_set_flip,
        .submit = ps5_agc_submit_checked,
        .submit_opaque = &state->submit,
        .gpu_fence = state->fences[slot],
#ifdef PS5_GPU_FLIP_TIMING_GATE
        .gpu_eop_timestamp = state->gpu_eop_timestamps[slot],
#endif
        .videoout_handle = state->resources->video.handle,
        .buffer_index = (int32_t)slot,
        .flip_mode = state->resources->surface.flip_mode,
        .flip_arg = frame->token,
        .surface_registered = 1,
        .render_resources_ready = 1,
        .videoout_event_armed = 1,
    };
    struct ps5_submission_state submitted;
    state->transaction_started = 1;
#ifdef PS5_GPU_FLIP_TIMING_GATE
    state->gpu_flip_submit_ns[slot] = now_ns(0);
#endif
    const int result = ps5_submission_build_and_submit(&input, &submitted);
    if (result != PS5_SUBMISSION_OK || !submitted.submit_called ||
        !submitted.retain_all_resources)
        return result != 0 ? result : -2;
#ifdef PS5_RESOURCE_FOUNDATION
    if (frame->frame_index == 0u ||
        frame->frame_index + 1u == BSP_GATE_FRAME_COUNT)
        (void)ps5log_printf(PS5LOG_MARK,
            "RESOURCE_FRAME_SUBMITTED frame=%llu slot=%u token=%llu "
            "command_dwords=%u",
            (unsigned long long)frame->frame_index, slot,
            (unsigned long long)frame->token, submitted.command_dwords);
#endif
    state->cursor[slot] = stream.cursor;
    return 0;
}

static int frame_wait_gpu(const GearsAnimationFrame *frame, void *opaque)
{
    struct native_renderer *state = opaque;
    if (!frame || !state || frame->buffer >= 2u)
        return -1;
    const uint64_t deadline = now_ns(0) + UINT64_C(2000000000);
    while (__atomic_load_n(state->fences[frame->buffer], __ATOMIC_ACQUIRE) !=
               0u && now_ns(0) < deadline) {
        const struct timespec delay = {0, 1000000};
        (void)nanosleep(&delay, 0);
    }
    if (__atomic_load_n(state->fences[frame->buffer], __ATOMIC_ACQUIRE) != 0u)
        return -1;
#ifdef PS5_GPU_FLIP_TIMING_GATE
    state->gpu_flip_gpu_observed_ns[frame->buffer] = now_ns(0);
#endif
    return 0;
}

static int frame_wait_video(const GearsAnimationFrame *frame,
                            uint64_t *observed_token, void *opaque)
{
    struct native_renderer *state = opaque;
#ifdef PS5_REF_AGC_MODULE
    const uint64_t feature_frame_index = frame ?
        PS5_REF_AGC_FEATURE_FRAME(frame->frame_index) : 0u;
#else
    const uint64_t feature_frame_index = frame ? frame->frame_index : 0u;
#endif
    if (!frame || !state || !observed_token || frame->buffer >= 2u)
        return -1;
    struct ps5_frame_completion completion;
    if (ps5_frame_completion_begin(&completion, frame->token, 1) !=
        PS5_FRAME_WAITING)
        return -2;
    uint64_t event_storage[4] = {0, 0, 0, 0};
    struct ps5_event_diagnostics diagnostics = {0};
    const uint64_t deadline = now_ns(0) + UINT64_C(2000000000);
    for (;;) {
        const int expired = now_ns(0) >= deadline;
        const struct ps5_event_poll poll = {
            state->resources->video.equeue, event_storage,
            state->fences[frame->buffer], 10000u, 0, expired,
            sceKernelWaitEqueue, sceVideoOutGetEventData, &diagnostics
        };
        const int result = ps5_event_poll_completion(&completion, &poll);
        if (result == PS5_FRAME_DONE) {
            *observed_token = frame->token;
#ifdef PS5_GPU_FLIP_TIMING_GATE
            const uint64_t flip_observed_ns = now_ns(0);
            const uint64_t gpu_eop_ticks = __atomic_load_n(
                state->gpu_eop_timestamps[frame->buffer], __ATOMIC_ACQUIRE);
            Ps5GpuFlipTimingSample timing_sample;
            const int timing_result = ps5_gpu_flip_timing_record(
                &state->gpu_flip_timing, frame->frame_index, frame->buffer,
                frame->token, state->gpu_flip_submit_ns[frame->buffer],
                state->gpu_flip_gpu_observed_ns[frame->buffer],
                flip_observed_ns, gpu_eop_ticks, &timing_sample);
            if (timing_result != PS5_GPU_FLIP_TIMING_OK)
                return -100 + timing_result;
            if (frame->frame_index == 0u ||
                (frame->frame_index + 1u) % 600u == 0u ||
                frame->frame_index + 1u == BSP_GATE_FRAME_COUNT)
                (void)ps5log_printf(PS5LOG_MARK,
                    "GPU_FLIP_TIMING_SAMPLE schema=1 frame=%llu slot=%u "
                    "token=%llu cpu_submit_ns=%llu "
                    "cpu_gpu_observed_ns=%llu cpu_flip_observed_ns=%llu "
                    "submit_to_gpu_ns=%llu submit_to_flip_ns=%llu "
                    "gpu_to_flip_ns=%llu gpu_eop_ticks=%llu "
                    "gpu_eop_delta_ticks=%llu fence=zero token_match=exact",
                    (unsigned long long)timing_sample.frame,
                    timing_sample.slot,
                    (unsigned long long)timing_sample.token,
                    (unsigned long long)timing_sample.cpu_submit_ns,
                    (unsigned long long)timing_sample.cpu_gpu_observed_ns,
                    (unsigned long long)timing_sample.cpu_flip_observed_ns,
                    (unsigned long long)timing_sample.submit_to_gpu_ns,
                    (unsigned long long)timing_sample.submit_to_flip_ns,
                    (unsigned long long)timing_sample.gpu_to_flip_ns,
                    (unsigned long long)timing_sample.gpu_eop_ticks,
                    (unsigned long long)timing_sample.gpu_eop_delta_ticks);
#endif
#ifdef PS5_RESOURCE_FOUNDATION
            if (!ps5_cache_gpu_to_cpu_complete(
                    __atomic_load_n(state->fences[frame->buffer],
                                    __ATOMIC_ACQUIRE),
                    frame->token, *observed_token))
                return -3;
            state->completed_tokens[frame->buffer] = frame->token;
            state->last_completed_token = frame->token;
#ifdef PS5_GOLDSRC_STATE_MATRIX_GATE
            const uint32_t matrix_index =
                goldsrc_state_matrix_index(frame->frame_index);
            if (!state->goldsrc_matrix_seen[matrix_index][frame->buffer]) {
                const uint8_t *const pixels =
                    (const uint8_t *)state->resources->framebuffer +
                    (size_t)frame->buffer * PS5_SURFACE_BUFFER_STRIDE;
                const size_t bytes = state->resources->surface.tiled_footprint;
                ps5_native_cache_flush((void *)pixels, bytes);
                state->goldsrc_matrix_hashes[matrix_index][frame->buffer] =
                    readback_hash(pixels, bytes);
                state->goldsrc_matrix_bright[matrix_index][frame->buffer] =
                    bright_pixel_count(pixels, bytes);
                state->goldsrc_matrix_seen[matrix_index][frame->buffer] = 1u;
                const GoldSrcStateMatrixCase *const matrix_case =
                    goldsrc_state_matrix_case(matrix_index);
                uint32_t matrix_key = 0u;
                if (!matrix_case || goldsrc_render_state_key(
                        &matrix_case->state, &matrix_key) != 0)
                    return -4;
                (void)ps5log_printf(PS5LOG_MARK,
                    "GOLDSRC_STATE_MATRIX_READBACK schema=1 frame=%llu "
                    "slot=%u case=%u name=%s key=%u hash=%016llx "
                    "bright_pixels=%llu fence=zero videoout_token=exact",
                    (unsigned long long)frame->frame_index, frame->buffer,
                    matrix_index, matrix_case->name, matrix_key,
                    (unsigned long long)
                        state->goldsrc_matrix_hashes[matrix_index]
                                                     [frame->buffer],
                    (unsigned long long)
                        state->goldsrc_matrix_bright[matrix_index]
                                                     [frame->buffer]);
            }
#endif
#ifdef PS5_GOLDSRC_LIGHTING_GATE
            const uint32_t lighting_mode =
                goldsrc_lighting_mode(feature_frame_index);
            if (!state->goldsrc_lighting_seen[lighting_mode]
                                                   [frame->buffer]) {
                const uint8_t *const pixels =
                    (const uint8_t *)state->resources->framebuffer +
                    (size_t)frame->buffer * PS5_SURFACE_BUFFER_STRIDE;
                const size_t bytes = state->resources->surface.tiled_footprint;
                ps5_native_cache_flush((void *)pixels, bytes);
                state->goldsrc_lighting_hashes[lighting_mode]
                                               [frame->buffer] =
                    readback_hash(pixels, bytes);
                state->goldsrc_lighting_bright[lighting_mode]
                                               [frame->buffer] =
                    bright_pixel_count(pixels, bytes);
                state->goldsrc_lighting_seen[lighting_mode]
                                             [frame->buffer] = 1u;
                (void)ps5log_printf(PS5LOG_MARK,
                    "GOLDSRC_LIGHTING_READBACK schema=1 frame=%llu "
                    "slot=%u mode=%u name=%s hash=%016llx "
                    "bright_pixels=%llu fence=zero "
                    "videoout_token=exact",
                    (unsigned long long)frame->frame_index, frame->buffer,
                    lighting_mode,
                    goldsrc_lighting_mode_name(lighting_mode),
                    (unsigned long long)
                        state->goldsrc_lighting_hashes[lighting_mode]
                                                       [frame->buffer],
                    (unsigned long long)
                        state->goldsrc_lighting_bright[lighting_mode]
                                                       [frame->buffer]);
            }
#endif
#ifdef PS5_GOLDSRC_SPRITE_PARTICLE_GATE
            const uint32_t effect_mode =
                goldsrc_sprite_particle_mode(feature_frame_index);
            if (!state->goldsrc_effect_seen[effect_mode][frame->buffer]) {
                const uint8_t *const pixels =
                    (const uint8_t *)state->resources->framebuffer +
                    (size_t)frame->buffer * PS5_SURFACE_BUFFER_STRIDE;
                const size_t bytes = state->resources->surface.tiled_footprint;
                ps5_native_cache_flush((void *)pixels, bytes);
                state->goldsrc_effect_hashes[effect_mode][frame->buffer] =
                    readback_hash(pixels, bytes);
                state->goldsrc_effect_bright[effect_mode][frame->buffer] =
                    bright_pixel_count(pixels, bytes);
                state->goldsrc_effect_seen[effect_mode][frame->buffer] = 1u;
                (void)ps5log_printf(PS5LOG_MARK,
                    "GOLDSRC_SPRITE_PARTICLE_READBACK schema=1 "
                    "frame=%llu slot=%u mode=%u name=%s "
                    "hash=%016llx bright_pixels=%llu fence=zero "
                    "videoout_token=exact",
                    (unsigned long long)frame->frame_index, frame->buffer,
                    effect_mode,
                    goldsrc_sprite_particle_mode_name(effect_mode),
                    (unsigned long long)
                        state->goldsrc_effect_hashes[effect_mode]
                                                     [frame->buffer],
                    (unsigned long long)
                        state->goldsrc_effect_bright[effect_mode]
                                                     [frame->buffer]);
            }
#endif
#ifdef PS5_GOLDSRC_STUDIO_GATE
            const uint32_t studio_mode =
                goldsrc_studio_mode(feature_frame_index);
            if (!state->goldsrc_studio_seen[studio_mode][frame->buffer]) {
                const uint8_t *const pixels =
                    (const uint8_t *)state->resources->framebuffer +
                    (size_t)frame->buffer * PS5_SURFACE_BUFFER_STRIDE;
                const size_t bytes =
                    state->resources->surface.tiled_footprint;
                ps5_native_cache_flush((void *)pixels, bytes);
                state->goldsrc_studio_hashes[studio_mode][frame->buffer] =
                    readback_hash(pixels, bytes);
                state->goldsrc_studio_bright[studio_mode][frame->buffer] =
                    bright_pixel_count(pixels, bytes);
                state->goldsrc_studio_seen[studio_mode][frame->buffer] = 1u;
                (void)ps5log_printf(PS5LOG_MARK,
                    "GOLDSRC_STUDIO_READBACK schema=1 frame=%llu "
                    "slot=%u mode=%u name=%s hash=%016llx "
                    "bright_pixels=%llu fence=zero videoout_token=exact",
                    (unsigned long long)frame->frame_index, frame->buffer,
                    studio_mode, goldsrc_studio_mode_name(studio_mode),
                    (unsigned long long)
                        state->goldsrc_studio_hashes[studio_mode]
                                                     [frame->buffer],
                    (unsigned long long)
                        state->goldsrc_studio_bright[studio_mode]
                                                     [frame->buffer]);
            }
#endif
#ifdef PS5_GOLDSRC_BRUSH_GATE
            const uint32_t brush_mode =
                goldsrc_brush_mode(feature_frame_index);
            if (!state->goldsrc_brush_seen[brush_mode][frame->buffer]) {
                const uint8_t *const pixels =
                    (const uint8_t *)state->resources->framebuffer +
                    (size_t)frame->buffer * PS5_SURFACE_BUFFER_STRIDE;
                const size_t bytes =
                    state->resources->surface.tiled_footprint;
                ps5_native_cache_flush((void *)pixels, bytes);
                state->goldsrc_brush_hashes[brush_mode][frame->buffer] =
                    readback_hash(pixels, bytes);
                state->goldsrc_brush_bright[brush_mode][frame->buffer] =
                    bright_pixel_count(pixels, bytes);
                state->goldsrc_brush_seen[brush_mode][frame->buffer] = 1u;
                (void)ps5log_printf(PS5LOG_MARK,
                    "GOLDSRC_BRUSH_READBACK schema=1 frame=%llu "
                    "slot=%u mode=%u name=%s hash=%016llx "
                    "bright_pixels=%llu fence=zero videoout_token=exact",
                    (unsigned long long)frame->frame_index, frame->buffer,
                    brush_mode, goldsrc_brush_mode_name(brush_mode),
                    (unsigned long long)
                        state->goldsrc_brush_hashes[brush_mode]
                                                    [frame->buffer],
                    (unsigned long long)
                        state->goldsrc_brush_bright[brush_mode]
                                                   [frame->buffer]);
            }
#endif
#ifdef PS5_GOLDSRC_VISIBILITY_GATE
            const uint32_t visibility_mode =
                goldsrc_visibility_mode(feature_frame_index);
            const GoldSrcVisibilityFrame *const visibility =
                &state->goldsrc_visibility_frames[frame->buffer];
            if (!state->goldsrc_visibility_seen[visibility_mode]
                                                   [frame->buffer]) {
                const uint8_t *const pixels =
                    (const uint8_t *)state->resources->framebuffer +
                    (size_t)frame->buffer * PS5_SURFACE_BUFFER_STRIDE;
                const size_t bytes =
                    state->resources->surface.tiled_footprint;
                ps5_native_cache_flush((void *)pixels, bytes);
                state->goldsrc_visibility_hashes[visibility_mode]
                                                    [frame->buffer] =
                    readback_hash(pixels, bytes);
                state->goldsrc_visibility_bright[visibility_mode]
                                                    [frame->buffer] =
                    bright_pixel_count(pixels, bytes);
                state->goldsrc_visibility_seen[visibility_mode]
                                                  [frame->buffer] = 1u;
                if (!state->goldsrc_visibility_counts_seen[visibility_mode]) {
                    state->goldsrc_visibility_draw_counts[visibility_mode] =
                        visibility->selected_draws;
                    state->goldsrc_visibility_leaf_counts[visibility_mode] =
                        visibility->visible_leaves;
                    state->goldsrc_visibility_counts_seen[visibility_mode] = 1u;
                }
                (void)ps5log_printf(PS5LOG_MARK,
                    "GOLDSRC_VISIBILITY_READBACK schema=1 frame=%llu "
                    "slot=%u mode=%u name=%s hash=%016llx "
                    "bright_pixels=%llu draws=%u fence=zero "
                    "videoout_token=exact",
                    (unsigned long long)frame->frame_index, frame->buffer,
                    visibility_mode,
                    goldsrc_visibility_mode_name(visibility_mode),
                    (unsigned long long)
                        state->goldsrc_visibility_hashes[visibility_mode]
                                                         [frame->buffer],
                    (unsigned long long)
                        state->goldsrc_visibility_bright[visibility_mode]
                                                         [frame->buffer],
                    visibility->selected_draws);
            }
#endif
#ifdef PS5_GOLDSRC_PHASE4_FINAL_GATE
            if ((feature_frame_index == 59400u ||
                 feature_frame_index == 59401u) &&
                !state->goldsrc_phase4_final_seen[frame->buffer]) {
                const uint8_t *const pixels =
                    (const uint8_t *)state->resources->framebuffer +
                    (size_t)frame->buffer * PS5_SURFACE_BUFFER_STRIDE;
                const size_t bytes =
                    state->resources->surface.tiled_footprint;
                ps5_native_cache_flush((void *)pixels, bytes);
                state->goldsrc_phase4_final_hashes[frame->buffer] =
                    readback_hash(pixels, bytes);
                state->goldsrc_phase4_final_bright[frame->buffer] =
                    bright_pixel_count(pixels, bytes);
                state->goldsrc_phase4_final_seen[frame->buffer] = 1u;
                (void)ps5log_printf(PS5LOG_MARK,
                    "GOLDSRC_PHASE4_FINAL_READBACK schema=1 frame=%llu "
                    "slot=%u hash=%016llx bright_pixels=%llu "
                    "world_draws=%u brush_draws=69 studio_draws=12 "
                    "effect_draws=3 screen_draws=2 modes=3,3,4,4,3 "
                    "fence=zero videoout_token=exact",
                    (unsigned long long)frame->frame_index, frame->buffer,
                    (unsigned long long)
                        state->goldsrc_phase4_final_hashes[frame->buffer],
                    (unsigned long long)
                        state->goldsrc_phase4_final_bright[frame->buffer],
                    state->goldsrc_visibility_frames[frame->buffer]
                        .selected_draws);
            }
#endif
            if (frame->frame_index == 0u ||
                frame->frame_index + 1u == BSP_GATE_FRAME_COUNT)
                (void)ps5log_printf(PS5LOG_MARK,
                    "RESOURCE_FRAME_RETIRED frame=%llu slot=%u token=%llu "
                    "fence=zero videoout_token=exact",
                    (unsigned long long)frame->frame_index, frame->buffer,
                    (unsigned long long)frame->token);
#endif
#ifdef PS5_BSP_VIEWER
            if (frame->frame_index == 0u ||
                frame->frame_index + 1u == BSP_GATE_FRAME_COUNT)
                (void)ps5log_printf(PS5LOG_MARK,
                    "BSP_VIDEOOUT_TOKEN frame=%llu buffer=%u expected=%llu "
                    "observed=%llu exact=true",
                    (unsigned long long)frame->frame_index, frame->buffer,
                    (unsigned long long)frame->token,
                    (unsigned long long)*observed_token);
#endif
            return 0;
        }
        if (result < 0) {
#ifdef PS5_REF_AGC_LIVE_PHASE7
            (void)ps5log_printf(PS5LOG_ERR,
                "REF_AGC_LIVE_VIDEOOUT_FAILURE frame=%llu result=%d "
                "expected=%llu observed=%lld fence=%llu waits=%u "
                "wait_rc=%d event_count=%d decode_rc=%d",
                (unsigned long long)frame->frame_index, result,
                (unsigned long long)frame->token,
                (long long)diagnostics.last_flip_arg,
                (unsigned long long)__atomic_load_n(
                    state->fences[frame->buffer], __ATOMIC_ACQUIRE),
                diagnostics.wait_calls, diagnostics.last_wait_rc,
                diagnostics.last_event_count, diagnostics.last_decode_rc);
#endif
            return result;
        }
    }
}

static void frame_telemetry(uint64_t frame,
                            const GearsTelemetrySnapshot *snapshot,
                            int terminal_error, void *opaque)
{
    (void)opaque;
    if (!snapshot)
        return;
#ifdef PS5_BSP_VIEWER
    const char *const event = "BSP_FRAME";
#else
    const char *const event = "GEARS_FRAME";
#endif
    (void)ps5log_printf(terminal_error ? PS5LOG_ERR : PS5LOG_INFO,
        "%s frame=%llu completed=%llu compose_avg_ns=%llu "
        "gpu_wait_avg_ns=%llu video_wait_avg_ns=%llu "
        "present_interval_avg_ns=%llu present_interval_max_ns=%llu "
        "present_interval_over_budget=%llu "
        "errors=%llu terminal=%d",
        event, (unsigned long long)frame,
        (unsigned long long)snapshot->frames_completed,
        (unsigned long long)snapshot->compose_ns_average,
        (unsigned long long)snapshot->gpu_wait_ns_average,
        (unsigned long long)snapshot->video_wait_ns_average,
        (unsigned long long)snapshot->present_interval_ns_average,
        (unsigned long long)snapshot->present_interval_ns_max,
        (unsigned long long)snapshot->present_interval_over_budget,
        (unsigned long long)snapshot->errors, terminal_error);
}

static int guards_intact(void)
{
    const uint64_t footprint = resources.surface.tiled_footprint;
    const uint64_t offsets[3] = {
        footprint,
        PS5_SURFACE_BUFFER_STRIDE - 64u,
        PS5_SURFACE_BUFFER_STRIDE + footprint,
    };
    for (unsigned zone = 0; zone < 3u; ++zone) {
        const volatile uint32_t *guard = (const volatile uint32_t *)(
            (const uint8_t *)resources.framebuffer + offsets[zone]);
        for (unsigned word = 0; word < 16u; ++word)
            if (guard[word] != GUARD_WORD)
                return 0;
    }
    const volatile uint32_t *depth_guard = (const volatile uint32_t *)(
        (const uint8_t *)resources.depth + DEPTH_BYTES);
    for (unsigned word = 0; word < 16u; ++word)
        if (depth_guard[word] != GUARD_WORD)
            return 0;
#ifdef PS5_BSP_VIEWER
    const volatile uint32_t *bsp_guard = (const volatile uint32_t *)(
        (const uint8_t *)resources.bsp + renderer.bsp_plan.guard_offset);
    for (unsigned word = 0; word < 16u; ++word)
        if (bsp_guard[word] != GUARD_WORD)
            return 0;
#endif
    return 1;
}

static int cleanup(void)
{
    int result;
#ifdef PS5_BSP_NOCLIP
    if (resources.pad_opened &&
        (result = scePadClose(resources.pad_handle)) != 0)
        return result;
    resources.pad_opened = 0;
    resources.pad_handle = -1;
#endif
    if (resources.video.handle >= 0) {
        result = ps5_videoout_close(&resources.video, &video_ops);
        log_result("videoout_close_chain", result);
        if (result != 0)
            return result;
    }
    if (resources.framebuffer_mapped &&
        (result = sceKernelMunmap(resources.framebuffer,
                                  PS5_SURFACE_ALLOCATION_BYTES)) != 0)
        return result;
    resources.framebuffer_mapped = 0;
    if (resources.framebuffer_allocated &&
        (result = sceKernelReleaseDirectMemory(resources.framebuffer_offset,
                         PS5_SURFACE_ALLOCATION_BYTES)) != 0)
        return result;
    resources.framebuffer_allocated = 0;
#ifdef PS5_RESOURCE_FOUNDATION
    if (resources.resource_heap_mapped) {
#ifdef PS5_REF_AGC_LIVE_PHASE7
        if (resources.live_world_allocation.generation != 0u)
            (void)ps5_resource_pool_release_unsubmitted(
                &resources.resource_pool,
                &resources.live_world_allocation);
        if (resources.live_texture_allocation.generation != 0u)
            (void)ps5_resource_pool_release_unsubmitted(
                &resources.resource_pool,
                &resources.live_texture_allocation);
        ref_agc_gpu_world_cache_destroy(&renderer.live_world_cache);
        ref_agc_gpu_texture_cache_destroy(&renderer.live_texture_cache);
#endif
#ifdef PS5_TEXTURE_PATH
        for (uint32_t slot = 0u; slot < 2u; ++slot)
            if (resources.dynamic_lightmap_allocations[slot].generation != 0u)
                (void)ps5_resource_pool_release_unsubmitted(
                    &resources.resource_pool,
                    &resources.dynamic_lightmap_allocations[slot]);
#endif
        (void)ps5_resource_pool_release_unsubmitted(
            &resources.resource_pool, &resources.transient_allocation);
        (void)ps5_resource_pool_release_unsubmitted(
            &resources.resource_pool, &resources.depth_allocation);
        (void)ps5_resource_pool_release_unsubmitted(
            &resources.resource_pool, &resources.shader_allocation);
        (void)ps5_resource_pool_release_unsubmitted(
            &resources.resource_pool, &resources.bsp_allocation);
        if ((result = sceKernelMunmap(resources.resource_heap,
                                      resources.resource_heap_bytes)) != 0)
            return result;
        resources.resource_heap_mapped = 0;
    }
    if (resources.resource_heap_allocated &&
        (result = sceKernelReleaseDirectMemory(
             resources.resource_heap_offset,
             resources.resource_heap_bytes)) != 0)
        return result;
    resources.resource_heap_allocated = 0;
#else
    if (resources.depth_mapped &&
        (result = sceKernelMunmap(resources.depth,
                                  DEPTH_ALLOCATION_BYTES)) != 0)
        return result;
    resources.depth_mapped = 0;
    if (resources.depth_allocated &&
        (result = sceKernelReleaseDirectMemory(resources.depth_offset,
                                                DEPTH_ALLOCATION_BYTES)) != 0)
        return result;
    resources.depth_allocated = 0;
#ifdef PS5_BSP_VIEWER
    if (resources.bsp_mapped &&
        (result = sceKernelMunmap(resources.bsp, resources.bsp_bytes)) != 0)
        return result;
    resources.bsp_mapped = 0;
    if (resources.bsp_allocated &&
        (result = sceKernelReleaseDirectMemory(resources.bsp_offset,
                                                resources.bsp_bytes)) != 0)
        return result;
    resources.bsp_allocated = 0;
#endif
    if (resources.shader_mapped &&
        (result = sceKernelMunmap(resources.shader, SHADER_BYTES)) != 0)
        return result;
    resources.shader_mapped = 0;
    if (resources.shader_allocated &&
        (result = sceKernelReleaseDirectMemory(resources.shader_offset,
                                                SHADER_BYTES)) != 0)
        return result;
    resources.shader_allocated = 0;
#endif
    if (resources.command_mapped) {
        struct ps5_batch_map_entry entry = {
            resources.command, 0, resources.command_bytes, 0xf2, 0x0c, 0, 1
        };
        int processed = -1;
        result = sceKernelBatchMap(&entry, 1, &processed);
        if (result != 0 || processed != 1)
            return result != 0 ? result : -1;
        resources.command_mapped = 0;
    }
    if (resources.command_allocated &&
        (result = sceKernelReleaseDirectMemory(resources.command_offset,
                                                resources.command_bytes)) != 0)
        return result;
    resources.command_allocated = 0;
    if (resources.command_reserved &&
        (result = sceKernelMunmap(resources.command,
                                  resources.command_bytes)) != 0)
        return result;
    resources.command_reserved = 0;
    if (resources.agc_loaded &&
        (result = sceSysmoduleUnloadModuleInternal(AGC_MODULE)) != 0)
        return result;
    resources.agc_loaded = 0;
    return 0;
}

static void park(const char *reason)
{
    (void)ps5log_printf(PS5LOG_ERR, "PARKED retain_all_resources=true reason=%s",
                        reason ? reason : "unknown");
    ps5log_close("parked-retain");
#ifdef PS5_REF_AGC_MODULE
    PS5_RefAgcRuntimeFailed(-1);
    pthread_exit(NULL);
#else
    for (;;)
        (void)pause();
#endif
}

#ifdef PS5_BSP_VIEWER
static uint64_t readback_hash(const void *data, size_t bytes)
{
    const uint8_t *cursor = data;
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t index = 0; index < bytes; ++index) {
        hash ^= cursor[index];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static uint64_t bright_pixel_count(const void *data, size_t bytes)
{
    const uint8_t *pixels = data;
    uint64_t bright = 0u;
    for (size_t offset = 0; offset + 3u < bytes; offset += 4u)
        if (pixels[offset] > 32u || pixels[offset + 1u] > 32u ||
            pixels[offset + 2u] > 32u)
            ++bright;
    return bright;
}

#ifndef PS5_REF_AGC_LIVE_PHASE7
static void park_complete(void)
{
#ifdef PS5_REF_AGC_MODULE
    const int cleanup_result = cleanup();
    (void)ps5log_printf(cleanup_result == 0 ? PS5LOG_MARK : PS5LOG_ERR,
        "REF_AGC_TEARDOWN videoout=closed direct_memory=released "
        "agc=unloaded result=%d ownership=%s",
        cleanup_result, cleanup_result == 0 ? "exact" : "retained");
    PS5_RefAgcRuntimeComplete(
        BSP_GATE_FRAME_COUNT,
        renderer.goldsrc_phase4_final_hashes[0] ^
            renderer.goldsrc_phase4_final_hashes[1],
        renderer.goldsrc_phase4_final_bright[0] +
            renderer.goldsrc_phase4_final_bright[1],
        cleanup_result);
    ps5log_close(cleanup_result == 0 ? "ref-agc-runtime-complete" :
                                      "ref-agc-teardown-failure");
    pthread_exit(NULL);
#else
#ifdef PS5_TEXTURE_PATH
#ifdef PS5_GPU_FLIP_TIMING_GATE
    ps5log_close("gpu-flip-timing-soak-complete");
#elif defined(PS5_GOLDSRC_PHASE4_FINAL_GATE)
    ps5log_close("goldsrc-phase4-final-soak-complete");
#elif defined(PS5_GOLDSRC_VISIBILITY_GATE)
    ps5log_close("goldsrc-phase4-visibility-soak-complete");
#elif defined(PS5_GOLDSRC_BRUSH_GATE)
    ps5log_close("goldsrc-phase4-brush-soak-complete");
#elif defined(PS5_GOLDSRC_STUDIO_GATE)
    ps5log_close("goldsrc-phase4-studio-soak-complete");
#elif defined(PS5_GOLDSRC_SPRITE_PARTICLE_GATE)
    ps5log_close("goldsrc-phase4-sprite-particle-soak-complete");
#elif defined(PS5_GOLDSRC_LIGHTING_GATE)
    ps5log_close("goldsrc-phase4-lighting-soak-complete");
#elif defined(PS5_GOLDSRC_2D_GATE)
    ps5log_close("goldsrc-phase4-2d-soak-complete");
#elif defined(PS5_TEXTURE_FINAL_GATE)
    ps5log_close("bsp-texture-path-final-soak-complete");
#elif defined(PS5_TEXTURE_ACCOUNTING_GATE)
    ps5log_close("bsp-texture-path-accounting-soak-complete");
#elif defined(PS5_TEXTURE_SKY_GATE)
    ps5log_close("bsp-texture-path-sky-soak-complete");
#elif defined(PS5_TEXTURE_ALPHA_GATE)
    ps5log_close("bsp-texture-path-alpha-soak-complete");
#elif defined(PS5_TEXTURE_MIP_GATE)
    ps5log_close("bsp-texture-path-mip-soak-complete");
#else
    ps5log_close("bsp-texture-path-lightmap-soak-complete");
#endif
#elif defined(PS5_RESOURCE_FOUNDATION)
    ps5log_close("bsp-resource-soak-complete");
#elif defined(PS5_BSP_TEXTURED)
    ps5log_close("bsp-textured-soak-complete");
#elif defined(PS5_BSP_NOCLIP)
    ps5log_close("bsp-noclip-soak-complete");
#else
    ps5log_close("bsp-gate1-complete");
#endif
    for (;;)
        (void)pause();
#endif
}
#endif
#endif

static int fail_pre_submit(const char *step, int result)
{
    log_result(step, result);
    const int cleanup_result = cleanup();
    log_result("pre_submit_cleanup", cleanup_result);
    ps5log_close("pre-submit-failure");
    return result != 0 ? result : -1;
}

int main(void)
{
#ifdef PS5_REF_AGC_MODULE
    (void)PS5_RefAgcRuntimeFailed(0);
#endif
    memset(&resources, 0, sizeof(resources));
    resources.command_offset = resources.framebuffer_offset =
        resources.shader_offset = resources.depth_offset = -1;
    resources.bsp_offset = -1;
#ifdef PS5_RESOURCE_FOUNDATION
    resources.resource_heap_offset = -1;
#endif
    resources.pad_handle = -1;
    resources.video.handle = -1;
    ps5log_config log_config;
    const char *log_path = 0;
    const uint64_t boot_token = now_ns(0);
    ps5log_config_defaults(&log_config);
    const int config_result = ps5log_load_config(
        ps5log_default_conf_paths, ps5log_default_conf_path_count,
        &log_config, &log_path);
    const int log_result = config_result == 0
        ? ps5log_init(&log_config, PS5_XASH3D_TITLE_ID,
                      PS5_XASH3D_APP_NAME, boot_token)
        : 1;
    (void)ps5log_line(PS5LOG_INFO, "LOG_SCHEMA=3");
    (void)ps5log_line(PS5LOG_INFO,
                      "LOG_TRANSPORT=ps5log/1 tcp structured");
    (void)ps5log_line(PS5LOG_INFO, "LOG_FS_SINKS=disabled");
    (void)ps5log_hex64(PS5LOG_INFO, "LOG_BOOT_MONOTONIC_NS", boot_token);
    (void)ps5log_printf(PS5LOG_INFO,
                        "LOG_CONFIG_RESULT=%d LOG_INIT_RESULT=%d path=%s",
                        config_result, log_result,
                        log_path ? log_path : "unavailable");
#ifdef PS5_BSP_VIEWER
#ifdef PS5_TEXTURE_PATH
#ifdef PS5_REF_AGC_LIVE_PHASE7
    (void)ps5log_printf(PS5LOG_MARK,
        "BSP_TEXTURE_PATH_BOOT schema=1 slice=phase7-live-consumer "
        "target=gfx1013 fw=12.02 transient_slots=2 "
        "ownership=fence+videoout+ack bundle_sha256=%s bundle_bytes=%llu "
        "lifetime=engine-owned input_owner=engine",
        PS5_BSP_BUNDLE_SHA256,
        (unsigned long long)PS5_BSP_BUNDLE_BYTES);
#elif defined(PS5_GPU_FLIP_TIMING_GATE)
    (void)ps5log_printf(PS5LOG_MARK,
        "BSP_TEXTURE_PATH_BOOT schema=1 slice=gpu-flip-timing "
        "target=gfx1013 fw=12.02 transient_slots=2 "
        "ownership=fence+videoout gpu_timestamp=eop-release-mem "
        "bundle_sha256=%s bundle_bytes=%llu "
        "studio_sha256=%s studio_bytes=%llu soak_frames=%u "
        "input_gate=not-required",
        PS5_BSP_BUNDLE_SHA256,
        (unsigned long long)PS5_BSP_BUNDLE_BYTES,
        PS5_STUDIO_BUNDLE_SHA256,
        (unsigned long long)PS5_STUDIO_BUNDLE_BYTES,
        BSP_GATE_FRAME_COUNT);
#elif defined(PS5_GOLDSRC_PHASE4_FINAL_GATE)
    (void)ps5log_printf(PS5LOG_MARK,
        "BSP_TEXTURE_PATH_BOOT schema=1 slice=goldsrc-phase4-final "
        "target=gfx1013 fw=12.02 transient_slots=2 "
        "ownership=fence+videoout bundle_sha256=%s bundle_bytes=%llu "
        "studio_sha256=%s studio_bytes=%llu soak_frames=%u "
        "input_gate=not-required",
        PS5_BSP_BUNDLE_SHA256,
        (unsigned long long)PS5_BSP_BUNDLE_BYTES,
        PS5_STUDIO_BUNDLE_SHA256,
        (unsigned long long)PS5_STUDIO_BUNDLE_BYTES,
        BSP_GATE_FRAME_COUNT);
#elif defined(PS5_GOLDSRC_VISIBILITY_GATE)
    (void)ps5log_printf(PS5LOG_MARK,
        "BSP_TEXTURE_PATH_BOOT schema=1 slice=goldsrc-visibility "
        "target=gfx1013 fw=12.02 transient_slots=2 "
        "ownership=fence+videoout bundle_sha256=%s bundle_bytes=%u "
        "soak_frames=%u input_gate=not-required",
        PS5_BSP_BUNDLE_SHA256, PS5_BSP_BUNDLE_BYTES,
        BSP_GATE_FRAME_COUNT);
#elif defined(PS5_GOLDSRC_BRUSH_GATE)
    (void)ps5log_printf(PS5LOG_MARK,
        "BSP_TEXTURE_PATH_BOOT schema=1 slice=goldsrc-brush "
        "target=gfx1013 fw=12.02 transient_slots=2 "
        "ownership=fence+videoout bundle_sha256=%s bundle_bytes=%u "
        "soak_frames=%u input_gate=not-required",
        PS5_BSP_BUNDLE_SHA256, PS5_BSP_BUNDLE_BYTES,
        BSP_GATE_FRAME_COUNT);
#elif defined(PS5_GOLDSRC_STUDIO_GATE)
    (void)ps5log_printf(PS5LOG_MARK,
        "BSP_TEXTURE_PATH_BOOT schema=1 slice=goldsrc-studio "
        "target=gfx1013 fw=12.02 transient_slots=2 "
        "ownership=fence+videoout bundle_sha256=%s bundle_bytes=%u "
        "studio_sha256=%s studio_bytes=%u soak_frames=%u "
        "input_gate=not-required",
        PS5_BSP_BUNDLE_SHA256, PS5_BSP_BUNDLE_BYTES,
        PS5_STUDIO_BUNDLE_SHA256, PS5_STUDIO_BUNDLE_BYTES,
        BSP_GATE_FRAME_COUNT);
#elif defined(PS5_GOLDSRC_SPRITE_PARTICLE_GATE)
    (void)ps5log_printf(PS5LOG_MARK,
        "BSP_TEXTURE_PATH_BOOT schema=1 slice=goldsrc-sprite-particles "
        "target=gfx1013 fw=12.02 transient_slots=2 "
        "ownership=fence+videoout bundle_sha256=%s bundle_bytes=%u "
        "soak_frames=%u input_gate=not-required",
        PS5_BSP_BUNDLE_SHA256, PS5_BSP_BUNDLE_BYTES,
        BSP_GATE_FRAME_COUNT);
#elif defined(PS5_GOLDSRC_LIGHTING_GATE)
    (void)ps5log_printf(PS5LOG_MARK,
        "BSP_TEXTURE_PATH_BOOT schema=1 slice=goldsrc-lighting "
        "target=gfx1013 fw=12.02 transient_slots=2 "
        "ownership=fence+videoout bundle_sha256=%s bundle_bytes=%u "
        "soak_frames=%u input_gate=not-required",
        PS5_BSP_BUNDLE_SHA256, PS5_BSP_BUNDLE_BYTES,
        BSP_GATE_FRAME_COUNT);
#elif defined(PS5_GOLDSRC_2D_GATE)
    (void)ps5log_printf(PS5LOG_MARK,
        "BSP_TEXTURE_PATH_BOOT schema=1 slice=goldsrc-2d "
        "target=gfx1013 fw=12.02 transient_slots=2 "
        "ownership=fence+videoout bundle_sha256=%s bundle_bytes=%u "
        "soak_frames=%u input_gate=not-required",
        PS5_BSP_BUNDLE_SHA256, PS5_BSP_BUNDLE_BYTES,
        BSP_GATE_FRAME_COUNT);
#elif defined(PS5_TEXTURE_FINAL_GATE)
    (void)ps5log_printf(PS5LOG_MARK,
        "BSP_TEXTURE_PATH_BOOT schema=1 slice=final "
        "target=gfx1013 fw=12.02 transient_slots=2 "
        "ownership=fence+videoout bundle_sha256=%s bundle_bytes=%u "
        "soak_frames=%u input_gate=not-repeated",
        PS5_BSP_BUNDLE_SHA256, PS5_BSP_BUNDLE_BYTES,
        BSP_GATE_FRAME_COUNT);
#elif defined(PS5_TEXTURE_ACCOUNTING_GATE)
    (void)ps5log_printf(PS5LOG_MARK,
        "BSP_TEXTURE_PATH_BOOT schema=1 slice=accounting "
        "target=gfx1013 fw=12.02 transient_slots=2 "
        "ownership=fence+videoout bundle_sha256=%s bundle_bytes=%u "
        "soak_frames=%u input_gate=not-repeated",
        PS5_BSP_BUNDLE_SHA256, PS5_BSP_BUNDLE_BYTES,
        BSP_GATE_FRAME_COUNT);
#elif defined(PS5_TEXTURE_SKY_GATE)
    (void)ps5log_printf(PS5LOG_MARK,
        "BSP_TEXTURE_PATH_BOOT schema=1 slice=sky "
        "target=gfx1013 fw=12.02 transient_slots=2 "
        "ownership=fence+videoout bundle_sha256=%s bundle_bytes=%u "
        "soak_frames=%u",
        PS5_BSP_BUNDLE_SHA256, PS5_BSP_BUNDLE_BYTES,
        BSP_GATE_FRAME_COUNT);
#elif defined(PS5_TEXTURE_ALPHA_GATE)
    (void)ps5log_printf(PS5LOG_MARK,
        "BSP_TEXTURE_PATH_BOOT schema=1 slice=alpha-test "
        "target=gfx1013 fw=12.02 transient_slots=2 "
        "ownership=fence+videoout bundle_sha256=%s bundle_bytes=%u "
        "soak_frames=%u",
        PS5_BSP_BUNDLE_SHA256, PS5_BSP_BUNDLE_BYTES,
        BSP_GATE_FRAME_COUNT);
#elif defined(PS5_TEXTURE_MIP_GATE)
    (void)ps5log_printf(PS5LOG_MARK,
        "BSP_TEXTURE_PATH_BOOT schema=1 slice=mip-sampler "
        "target=gfx1013 fw=12.02 transient_slots=2 "
        "ownership=fence+videoout bundle_sha256=%s bundle_bytes=%u "
        "soak_frames=%u",
        PS5_BSP_BUNDLE_SHA256, PS5_BSP_BUNDLE_BYTES,
        BSP_GATE_FRAME_COUNT);
#else
    (void)ps5log_printf(PS5LOG_MARK,
        "BSP_TEXTURE_PATH_BOOT schema=1 slice=dynamic-lightmap "
        "target=gfx1013 fw=12.02 transient_slots=2 "
        "ownership=fence+videoout bundle_sha256=%s bundle_bytes=%u "
        "soak_frames=%u",
        PS5_BSP_BUNDLE_SHA256, PS5_BSP_BUNDLE_BYTES,
        BSP_GATE_FRAME_COUNT);
#endif
#elif defined(PS5_RESOURCE_FOUNDATION)
    (void)ps5log_printf(PS5LOG_MARK,
        "BSP_RESOURCE_BOOT schema=1 target=gfx1013 fw=12.02 "
        "constants=vsharp transient_slots=2 pipelines=4 overlay=quad "
        "bundle_sha256=%s bundle_bytes=%u soak_frames=%u",
        PS5_BSP_BUNDLE_SHA256, PS5_BSP_BUNDLE_BYTES,
        BSP_GATE_FRAME_COUNT);
#elif defined(PS5_BSP_TEXTURED)
    (void)ps5log_printf(PS5LOG_MARK,
        "BSP_TEXTURED_BOOT schema=1 target=gfx1013 fw=12.02 "
        "composition=base_x_lightmap bundle_sha256=%s bundle_bytes=%u "
        "soak_frames=%u",
        PS5_BSP_BUNDLE_SHA256, PS5_BSP_BUNDLE_BYTES,
        BSP_GATE_FRAME_COUNT);
#elif defined(PS5_BSP_NOCLIP)
    (void)ps5log_printf(PS5LOG_MARK,
        "BSP_NOCLIP_BOOT schema=1 target=gfx1013 fw=12.02 "
        "bundle_sha256=%s bundle_bytes=%u soak_frames=%u",
        PS5_BSP_BUNDLE_SHA256, PS5_BSP_BUNDLE_BYTES,
        BSP_GATE_FRAME_COUNT);
#else
    (void)ps5log_printf(PS5LOG_MARK,
        "BSP_BOOT schema=1 target=gfx1013 fw=12.02 color_clear=rt_draw "
        "bundle_sha256=%s bundle_bytes=%u",
        PS5_BSP_BUNDLE_SHA256, PS5_BSP_BUNDLE_BYTES);
#endif
#else
    (void)ps5log_line(PS5LOG_MARK,
        "GEARS_BOOT schema=1 target=gfx1013 fw=12.02 color_clear=rt_draw");
#endif

    int result = sceSysmoduleLoadModuleInternal(AGC_MODULE);
    if (result != 0)
        return fail_pre_submit("agc_load", result);
    resources.agc_loaded = 1;
    result = sceAgcInit(&resources.agc_state, sizeof(resources.agc_state));
    if (result != 0)
        return fail_pre_submit("agc_init", result);
#ifdef PS5_BSP_VIEWER
    result = load_bsp_bundle();
    if (result != 0)
        return fail_pre_submit("bsp_bundle_load", result);
#ifdef PS5_BSP_NOCLIP
#ifdef PS5_REF_AGC_LIVE_PHASE7
    result = bsp_noclip_init(&renderer.noclip,
                             renderer.bsp_bundle.camera_position,
                             renderer.bsp_bundle.camera_forward);
    if (result != 0)
        return fail_pre_submit("live_camera_fallback", result);
    (void)ps5log_line(PS5LOG_MARK,
        "REF_AGC_LIVE_CAMERA_READY source=engine-view "
        "startup_fallback=bundle-camera pad_owner=engine");
    /* FW 12.02 hardware A/B runs show that a bounded scheduler handoff is
     * required after the 193 MiB resource heap is loaded and the live camera
     * fallback is initialized.  Delays before the load or immediately after
     * it still present only the clear; this exact boundary presents geometry. */
    if (ps5_live_camera_settle() != 0)
        return fail_pre_submit("live_camera_settle", -1);
#else
    result = open_noclip_pad();
    if (result != 0)
        return fail_pre_submit("noclip_pad_open", result);
    (void)ps5log_line(PS5LOG_MARK,
        "BSP_NOCLIP_PAD_READY sticks=dual triggers=vertical "
#ifdef PS5_TEXTURE_ACCOUNTING_ENABLED
        "connected_required=false");
#else
        "connected_required=true");
#endif
#endif
#endif
    BspCommandPlan command_plan;
#ifdef PS5_BSP_TEXTURED
    const int command_plan_result = bsp_command_plan_with_stride(
#ifdef PS5_REF_AGC_LIVE_PHASE7
        REF_AGC_GPU_WORLD_MAX_DRAWS,
#else
        renderer.bsp_plan.scene_draw_count,
#endif
        BSP_TEXTURED_DWORDS_PER_DRAW, BSP_FIXED_COMMAND_DWORDS,
        &command_plan);
#else
    const int command_plan_result = bsp_command_plan(
        renderer.bsp_plan.scene_draw_count, BSP_FIXED_COMMAND_DWORDS,
        &command_plan);
#endif
    if (command_plan_result != 0)
        return fail_pre_submit("bsp_command_plan", -1);
    resources.command_bytes = command_plan.allocation_bytes;
    renderer.command_slot_bytes = command_plan.slot_bytes;
#else
    resources.command_bytes = COMMAND_BYTES;
    renderer.command_slot_bytes = COMMAND_SLOT_BYTES;
#endif
    result = map_command();
    if (result != 0)
        return fail_pre_submit("command_map", result);
    result = allocate_direct(&resources.framebuffer,
                             &resources.framebuffer_offset,
                             PS5_SURFACE_ALLOCATION_BYTES,
                             PS5_SURFACE_ALIGNMENT);
    if (result != 0)
        return fail_pre_submit("framebuffer_map", result);
    resources.framebuffer_allocated = resources.framebuffer_mapped = 1;
    result = ps5_surface_make_plan(0u, &resources.surface);
    if (result != 0)
        return fail_pre_submit("surface_plan", result);
#ifdef PS5_REF_AGC_LIVE_PHASE7
    renderer.live_aspect_ratio =
        (float)resources.surface.width / (float)resources.surface.height;
#endif

    memset(resources.framebuffer, 0, PS5_SURFACE_ALLOCATION_BYTES);
    const uint64_t guard_offsets[3] = {
        resources.surface.tiled_footprint,
        PS5_SURFACE_BUFFER_STRIDE - 64u,
        PS5_SURFACE_BUFFER_STRIDE + resources.surface.tiled_footprint,
    };
    for (unsigned zone = 0; zone < 3u; ++zone) {
        uint32_t *guard = (uint32_t *)((uint8_t *)resources.framebuffer +
                                       guard_offsets[zone]);
        for (unsigned word = 0; word < 16u; ++word)
            guard[word] = GUARD_WORD;
        ps5_native_cache_flush(guard, 64u);
    }
    result = ps5_videoout_open(&resources.video, &video_ops,
                               &resources.surface, resources.framebuffer);
    if (result != 0)
        return fail_pre_submit("videoout_open_chain", result);

#ifndef PS5_RESOURCE_FOUNDATION
    result = allocate_direct(&resources.shader, &resources.shader_offset,
                             SHADER_BYTES, SHADER_ALIGNMENT);
    if (result != 0)
        return fail_pre_submit("shader_map", result);
    resources.shader_allocated = resources.shader_mapped = 1;
    result = allocate_direct(&resources.depth, &resources.depth_offset,
                             DEPTH_ALLOCATION_BYTES, DEPTH_ALIGNMENT);
    if (result != 0)
        return fail_pre_submit("depth_map", result);
    resources.depth_allocated = resources.depth_mapped = 1;
#endif
    memset(resources.shader, 0, SHADER_BYTES);

#ifdef PS5_BSP_VIEWER
#ifdef PS5_BSP_TEXTURED
#ifdef PS5_RESOURCE_FOUNDATION
    const struct ps5_shader_metadata metadata = {
        PS5_BSP_RESOURCE_GS_RSRC1, PS5_BSP_RESOURCE_GS_RSRC2,
        PS5_BSP_RESOURCE_PS_RSRC1, PS5_BSP_RESOURCE_PS_RSRC2,
        PS5_BSP_RESOURCE_GE_CNTL, PS5_BSP_RESOURCE_SHADER_STAGES_EN,
        PS5_BSP_RESOURCE_GS_OUT_PRIM_TYPE, PS5_BSP_RESOURCE_DRAW_MODIFIER,
        ps5_bsp_resource_pre_raster_cx,
        sizeof(ps5_bsp_resource_pre_raster_cx) /
            sizeof(ps5_bsp_resource_pre_raster_cx[0]),
        ps5_bsp_resource_pixel_cx,
        sizeof(ps5_bsp_resource_pixel_cx) /
            sizeof(ps5_bsp_resource_pixel_cx[0])
    };
    const uint8_t *const embedded_gs_start = ps5_bsp_resource_gs_start;
    const uint8_t *const embedded_gs_end = ps5_bsp_resource_gs_end;
    const uint8_t *const embedded_ps_start = ps5_bsp_resource_ps_start;
    const uint8_t *const embedded_ps_end = ps5_bsp_resource_ps_end;
    const size_t expected_gs_isa = PS5_BSP_RESOURCE_GS_ISA_BYTES;
    const size_t expected_ps_isa = PS5_BSP_RESOURCE_PS_ISA_BYTES;
#else
    const struct ps5_shader_metadata metadata = {
        PS5_BSP_TEXTURED_GS_RSRC1, PS5_BSP_TEXTURED_GS_RSRC2,
        PS5_BSP_TEXTURED_PS_RSRC1, PS5_BSP_TEXTURED_PS_RSRC2,
        PS5_BSP_TEXTURED_GE_CNTL, PS5_BSP_TEXTURED_SHADER_STAGES_EN,
        PS5_BSP_TEXTURED_GS_OUT_PRIM_TYPE, PS5_BSP_TEXTURED_DRAW_MODIFIER,
        ps5_bsp_textured_pre_raster_cx,
        sizeof(ps5_bsp_textured_pre_raster_cx) /
            sizeof(ps5_bsp_textured_pre_raster_cx[0]),
        ps5_bsp_textured_pixel_cx,
        sizeof(ps5_bsp_textured_pixel_cx) /
            sizeof(ps5_bsp_textured_pixel_cx[0])
    };
    const uint8_t *const embedded_gs_start = ps5_bsp_textured_gs_start;
    const uint8_t *const embedded_gs_end = ps5_bsp_textured_gs_end;
    const uint8_t *const embedded_ps_start = ps5_bsp_textured_ps_start;
    const uint8_t *const embedded_ps_end = ps5_bsp_textured_ps_end;
    const size_t expected_gs_isa = PS5_BSP_TEXTURED_GS_ISA_BYTES;
    const size_t expected_ps_isa = PS5_BSP_TEXTURED_PS_ISA_BYTES;
#endif
#else
    const struct ps5_shader_metadata metadata = {
        PS5_BSP_FLAT_GS_RSRC1, PS5_BSP_FLAT_GS_RSRC2,
        PS5_BSP_FLAT_PS_RSRC1, PS5_BSP_FLAT_PS_RSRC2,
        PS5_BSP_FLAT_GE_CNTL, PS5_BSP_FLAT_SHADER_STAGES_EN,
        PS5_BSP_FLAT_GS_OUT_PRIM_TYPE, PS5_BSP_FLAT_DRAW_MODIFIER,
        ps5_bsp_flat_pre_raster_cx,
        sizeof(ps5_bsp_flat_pre_raster_cx) /
            sizeof(ps5_bsp_flat_pre_raster_cx[0]),
        ps5_bsp_flat_pixel_cx,
        sizeof(ps5_bsp_flat_pixel_cx) / sizeof(ps5_bsp_flat_pixel_cx[0])
    };
    const uint8_t *const embedded_gs_start = ps5_bsp_flat_gs_start;
    const uint8_t *const embedded_gs_end = ps5_bsp_flat_gs_end;
    const uint8_t *const embedded_ps_start = ps5_bsp_flat_ps_start;
    const uint8_t *const embedded_ps_end = ps5_bsp_flat_ps_end;
    const size_t expected_gs_isa = PS5_BSP_FLAT_GS_ISA_BYTES;
    const size_t expected_ps_isa = PS5_BSP_FLAT_PS_ISA_BYTES;
#endif
#else
    const struct ps5_shader_metadata metadata = {
        PS5_GEARS_GS_RSRC1, PS5_GEARS_GS_RSRC2,
        PS5_GEARS_PS_RSRC1, PS5_GEARS_PS_RSRC2,
        PS5_GEARS_GE_CNTL, PS5_GEARS_SHADER_STAGES_EN,
        PS5_GEARS_GS_OUT_PRIM_TYPE, PS5_GEARS_DRAW_MODIFIER,
        ps5_gears_pre_raster_cx, 10u, ps5_gears_pixel_cx, 9u
    };
    const uint8_t *const embedded_gs_start = ps5_gears_gs_start;
    const uint8_t *const embedded_gs_end = ps5_gears_gs_end;
    const uint8_t *const embedded_ps_start = ps5_gears_ps_start;
    const uint8_t *const embedded_ps_end = ps5_gears_ps_end;
    const size_t expected_gs_isa = PS5_GEARS_GS_ISA_BYTES;
    const size_t expected_ps_isa = PS5_GEARS_PS_ISA_BYTES;
#endif
    uint8_t *base = resources.shader;
    struct ps5_shader_arena *gs_arena =
        (struct ps5_shader_arena *)(base + GS_HEADER_OFFSET);
    struct ps5_shader_arena *ps_arena =
        (struct ps5_shader_arena *)(base + PS_HEADER_OFFSET);
    uint8_t *gs_code = base + GS_CODE_OFFSET;
    uint8_t *ps_code = base + PS_CODE_OFFSET;
    const size_t gs_isa = (size_t)(embedded_gs_end - embedded_gs_start);
    const size_t ps_isa = (size_t)(embedded_ps_end - embedded_ps_start);
    const uint32_t gs_size = (uint32_t)gs_isa + SHADER_FOOTER_BYTES;
    const uint32_t ps_size = (uint32_t)ps_isa + SHADER_FOOTER_BYTES;
    if (gs_isa != expected_gs_isa || ps_isa != expected_ps_isa ||
        ps5_shader_header_build(gs_arena, PS5_SHADER_PRE_RASTER, gs_size,
                                &metadata) != 0 ||
        ps5_shader_header_build(ps_arena, PS5_SHADER_PIXEL, ps_size,
                                &metadata) != 0)
        return fail_pre_submit("shader_header", -1);
    memcpy(gs_code, embedded_gs_start, gs_isa);
    memcpy(ps_code, embedded_ps_start, ps_isa);
    memcpy(gs_code + gs_size - SHADER_FOOTER_BYTES, "barefoot", 8u);
    memcpy(ps_code + ps_size - SHADER_FOOTER_BYTES, "barefoot", 8u);
    void *gs_object = 0;
    void *ps_object = 0;
    result = sceAgcCreateShader(&gs_object, gs_arena, gs_code);
    if (result != 0 || gs_object != gs_arena)
        return fail_pre_submit("create_gs", result != 0 ? result : -1);
    result = sceAgcCreateShader(&ps_object, ps_arena, ps_code);
    if (result != 0 || ps_object != ps_arena)
        return fail_pre_submit("create_ps", result != 0 ? result : -1);
    struct ps5_agc_linked_cx *linked_cx =
        (struct ps5_agc_linked_cx *)(base + LINKED_CX_OFFSET);
    struct ps5_agc_linked_uc *linked_uc =
        (struct ps5_agc_linked_uc *)(base + LINKED_UC_OFFSET);
    result = sceAgcLinkShaders(linked_cx, linked_uc, 0, gs_object, ps_object,
                               4u);
    if (result != 0)
        return fail_pre_submit("link_shaders", result);

    ps5_agc_register defaults[PS5_COLOR_REGISTER_COUNT];
    result = ps5_color_select_runtime_defaults(
        defaults, (const struct ps5_agc_register_defaults *)
                      sceAgcGetRegisterDefaults());
    if (result != 0)
        return fail_pre_submit("color_defaults", result);
    struct ps5_pipeline_registers *pipelines =
        (struct ps5_pipeline_registers *)(base + PIPELINE_OFFSET);
    for (unsigned slot = 0; slot < 2u; ++slot) {
        ps5_agc_register color[PS5_COLOR_REGISTER_COUNT];
        const uintptr_t address = (uintptr_t)resources.framebuffer +
            resources.surface.buffer_offsets[slot];
        if (ps5_color_build_target(color, defaults, address,
                                   resources.surface.width,
                                   resources.surface.height) != 0 ||
            ps5_pipeline_build(
                &pipelines[slot], color, linked_cx, linked_uc,
                gs_arena->cx, ps_arena->cx, gs_arena->sh, ps_arena->sh,
                resources.surface.width, resources.surface.height) != 0)
            return fail_pre_submit("pipeline_build", -1);
    }
#ifdef PS5_RESOURCE_FOUNDATION
    const struct ps5_shader_metadata alpha_test_metadata = {
        PS5_BSP_ALPHA_TEST_GS_RSRC1, PS5_BSP_ALPHA_TEST_GS_RSRC2,
        PS5_BSP_ALPHA_TEST_PS_RSRC1, PS5_BSP_ALPHA_TEST_PS_RSRC2,
        PS5_BSP_ALPHA_TEST_GE_CNTL,
        PS5_BSP_ALPHA_TEST_SHADER_STAGES_EN,
        PS5_BSP_ALPHA_TEST_GS_OUT_PRIM_TYPE,
        PS5_BSP_ALPHA_TEST_DRAW_MODIFIER,
        ps5_bsp_alpha_test_pre_raster_cx,
        sizeof(ps5_bsp_alpha_test_pre_raster_cx) /
            sizeof(ps5_bsp_alpha_test_pre_raster_cx[0]),
        ps5_bsp_alpha_test_pixel_cx,
        sizeof(ps5_bsp_alpha_test_pixel_cx) /
            sizeof(ps5_bsp_alpha_test_pixel_cx[0])
    };
    struct ps5_shader_arena *alpha_gs_arena =
        (struct ps5_shader_arena *)(base + ALPHA_GS_HEADER_OFFSET);
    struct ps5_shader_arena *alpha_ps_arena =
        (struct ps5_shader_arena *)(base + ALPHA_PS_HEADER_OFFSET);
    uint8_t *alpha_gs_code = base + ALPHA_GS_CODE_OFFSET;
    uint8_t *alpha_ps_code = base + ALPHA_PS_CODE_OFFSET;
    const size_t alpha_gs_isa =
        (size_t)(ps5_bsp_alpha_test_gs_end - ps5_bsp_alpha_test_gs_start);
    const size_t alpha_ps_isa =
        (size_t)(ps5_bsp_alpha_test_ps_end - ps5_bsp_alpha_test_ps_start);
    const uint32_t alpha_gs_size =
        (uint32_t)alpha_gs_isa + SHADER_FOOTER_BYTES;
    const uint32_t alpha_ps_size =
        (uint32_t)alpha_ps_isa + SHADER_FOOTER_BYTES;
    if (alpha_gs_isa != PS5_BSP_ALPHA_TEST_GS_ISA_BYTES ||
        alpha_ps_isa != PS5_BSP_ALPHA_TEST_PS_ISA_BYTES ||
        alpha_test_metadata.draw_modifier != metadata.draw_modifier ||
        ps5_shader_header_build(alpha_gs_arena, PS5_SHADER_PRE_RASTER,
                                alpha_gs_size, &alpha_test_metadata) != 0 ||
        ps5_shader_header_build(alpha_ps_arena, PS5_SHADER_PIXEL,
                                alpha_ps_size, &alpha_test_metadata) != 0)
        return fail_pre_submit("alpha_test_shader_header", -1);
    memcpy(alpha_gs_code, ps5_bsp_alpha_test_gs_start, alpha_gs_isa);
    memcpy(alpha_ps_code, ps5_bsp_alpha_test_ps_start, alpha_ps_isa);
    memcpy(alpha_gs_code + alpha_gs_size - SHADER_FOOTER_BYTES,
           "barefoot", 8u);
    memcpy(alpha_ps_code + alpha_ps_size - SHADER_FOOTER_BYTES,
           "barefoot", 8u);
    void *alpha_gs_object = 0;
    void *alpha_ps_object = 0;
    result = sceAgcCreateShader(&alpha_gs_object, alpha_gs_arena,
                                alpha_gs_code);
    if (result != 0 || alpha_gs_object != alpha_gs_arena)
        return fail_pre_submit("create_alpha_test_gs",
                               result != 0 ? result : -1);
    result = sceAgcCreateShader(&alpha_ps_object, alpha_ps_arena,
                                alpha_ps_code);
    if (result != 0 || alpha_ps_object != alpha_ps_arena)
        return fail_pre_submit("create_alpha_test_ps",
                               result != 0 ? result : -1);
    struct ps5_agc_linked_cx *alpha_linked_cx =
        (struct ps5_agc_linked_cx *)(base + ALPHA_LINKED_CX_OFFSET);
    struct ps5_agc_linked_uc *alpha_linked_uc =
        (struct ps5_agc_linked_uc *)(base + ALPHA_LINKED_UC_OFFSET);
    result = sceAgcLinkShaders(alpha_linked_cx, alpha_linked_uc, 0,
                               alpha_gs_object, alpha_ps_object, 4u);
    if (result != 0)
        return fail_pre_submit("link_alpha_test_shaders", result);
    struct ps5_pipeline_registers *alpha_test_pipelines =
        (struct ps5_pipeline_registers *)(base + ALPHA_PIPELINE_OFFSET);
    for (unsigned slot = 0; slot < 2u; ++slot) {
        ps5_agc_register color[PS5_COLOR_REGISTER_COUNT];
        const uintptr_t address = (uintptr_t)resources.framebuffer +
            resources.surface.buffer_offsets[slot];
        if (ps5_color_build_target(color, defaults, address,
                                   resources.surface.width,
                                   resources.surface.height) != 0 ||
            ps5_pipeline_build(
                &alpha_test_pipelines[slot], color, alpha_linked_cx,
                alpha_linked_uc, alpha_gs_arena->cx, alpha_ps_arena->cx,
                alpha_gs_arena->sh, alpha_ps_arena->sh,
                resources.surface.width, resources.surface.height) != 0)
            return fail_pre_submit("alpha_test_pipeline_build", -1);
    }
    const struct ps5_shader_metadata sky_metadata = {
        PS5_BSP_SKY_GS_RSRC1, PS5_BSP_SKY_GS_RSRC2,
        PS5_BSP_SKY_PS_RSRC1, PS5_BSP_SKY_PS_RSRC2,
        PS5_BSP_SKY_GE_CNTL, PS5_BSP_SKY_SHADER_STAGES_EN,
        PS5_BSP_SKY_GS_OUT_PRIM_TYPE, PS5_BSP_SKY_DRAW_MODIFIER,
        ps5_bsp_sky_pre_raster_cx,
        sizeof(ps5_bsp_sky_pre_raster_cx) /
            sizeof(ps5_bsp_sky_pre_raster_cx[0]),
        ps5_bsp_sky_pixel_cx,
        sizeof(ps5_bsp_sky_pixel_cx) /
            sizeof(ps5_bsp_sky_pixel_cx[0])
    };
    struct ps5_shader_arena *sky_gs_arena =
        (struct ps5_shader_arena *)(base + SKY_GS_HEADER_OFFSET);
    struct ps5_shader_arena *sky_ps_arena =
        (struct ps5_shader_arena *)(base + SKY_PS_HEADER_OFFSET);
    uint8_t *sky_gs_code = base + SKY_GS_CODE_OFFSET;
    uint8_t *sky_ps_code = base + SKY_PS_CODE_OFFSET;
    const size_t sky_gs_isa =
        (size_t)(ps5_bsp_sky_gs_end - ps5_bsp_sky_gs_start);
    const size_t sky_ps_isa =
        (size_t)(ps5_bsp_sky_ps_end - ps5_bsp_sky_ps_start);
    const uint32_t sky_gs_size =
        (uint32_t)sky_gs_isa + SHADER_FOOTER_BYTES;
    const uint32_t sky_ps_size =
        (uint32_t)sky_ps_isa + SHADER_FOOTER_BYTES;
    if (sky_gs_isa != PS5_BSP_SKY_GS_ISA_BYTES ||
        sky_ps_isa != PS5_BSP_SKY_PS_ISA_BYTES ||
        sky_metadata.draw_modifier != metadata.draw_modifier ||
        ps5_shader_header_build(sky_gs_arena, PS5_SHADER_PRE_RASTER,
                                sky_gs_size, &sky_metadata) != 0 ||
        ps5_shader_header_build(sky_ps_arena, PS5_SHADER_PIXEL,
                                sky_ps_size, &sky_metadata) != 0)
        return fail_pre_submit("sky_shader_header", -1);
    memcpy(sky_gs_code, ps5_bsp_sky_gs_start, sky_gs_isa);
    memcpy(sky_ps_code, ps5_bsp_sky_ps_start, sky_ps_isa);
    memcpy(sky_gs_code + sky_gs_size - SHADER_FOOTER_BYTES,
           "barefoot", 8u);
    memcpy(sky_ps_code + sky_ps_size - SHADER_FOOTER_BYTES,
           "barefoot", 8u);
    void *sky_gs_object = 0;
    void *sky_ps_object = 0;
    result = sceAgcCreateShader(&sky_gs_object, sky_gs_arena, sky_gs_code);
    if (result != 0 || sky_gs_object != sky_gs_arena)
        return fail_pre_submit("create_sky_gs", result != 0 ? result : -1);
    result = sceAgcCreateShader(&sky_ps_object, sky_ps_arena, sky_ps_code);
    if (result != 0 || sky_ps_object != sky_ps_arena)
        return fail_pre_submit("create_sky_ps", result != 0 ? result : -1);
    struct ps5_agc_linked_cx *sky_linked_cx =
        (struct ps5_agc_linked_cx *)(base + SKY_LINKED_CX_OFFSET);
    struct ps5_agc_linked_uc *sky_linked_uc =
        (struct ps5_agc_linked_uc *)(base + SKY_LINKED_UC_OFFSET);
    result = sceAgcLinkShaders(sky_linked_cx, sky_linked_uc, 0,
                               sky_gs_object, sky_ps_object, 4u);
    if (result != 0)
        return fail_pre_submit("link_sky_shaders", result);
    struct ps5_pipeline_registers *sky_pipelines =
        (struct ps5_pipeline_registers *)(base + SKY_PIPELINE_OFFSET);
    for (unsigned slot = 0; slot < 2u; ++slot) {
        ps5_agc_register color[PS5_COLOR_REGISTER_COUNT];
        const uintptr_t address = (uintptr_t)resources.framebuffer +
            resources.surface.buffer_offsets[slot];
        if (ps5_color_build_target(color, defaults, address,
                                   resources.surface.width,
                                   resources.surface.height) != 0 ||
            ps5_pipeline_build(
                &sky_pipelines[slot], color, sky_linked_cx, sky_linked_uc,
                sky_gs_arena->cx, sky_ps_arena->cx, sky_gs_arena->sh,
                sky_ps_arena->sh, resources.surface.width,
                resources.surface.height) != 0)
            return fail_pre_submit("sky_pipeline_build", -1);
    }
    const struct ps5_shader_metadata overlay_metadata = {
        PS5_BSP_OVERLAY_GS_RSRC1, PS5_BSP_OVERLAY_GS_RSRC2,
        PS5_BSP_OVERLAY_PS_RSRC1, PS5_BSP_OVERLAY_PS_RSRC2,
        PS5_BSP_OVERLAY_GE_CNTL, PS5_BSP_OVERLAY_SHADER_STAGES_EN,
        PS5_BSP_OVERLAY_GS_OUT_PRIM_TYPE, PS5_BSP_OVERLAY_DRAW_MODIFIER,
        ps5_bsp_overlay_pre_raster_cx,
        sizeof(ps5_bsp_overlay_pre_raster_cx) /
            sizeof(ps5_bsp_overlay_pre_raster_cx[0]),
        ps5_bsp_overlay_pixel_cx,
        sizeof(ps5_bsp_overlay_pixel_cx) /
            sizeof(ps5_bsp_overlay_pixel_cx[0])
    };
    struct ps5_shader_arena *overlay_gs_arena =
        (struct ps5_shader_arena *)(base + OVERLAY_GS_HEADER_OFFSET);
    struct ps5_shader_arena *overlay_ps_arena =
        (struct ps5_shader_arena *)(base + OVERLAY_PS_HEADER_OFFSET);
    uint8_t *overlay_gs_code = base + OVERLAY_GS_CODE_OFFSET;
    uint8_t *overlay_ps_code = base + OVERLAY_PS_CODE_OFFSET;
    const size_t overlay_gs_isa =
        (size_t)(ps5_bsp_overlay_gs_end - ps5_bsp_overlay_gs_start);
    const size_t overlay_ps_isa =
        (size_t)(ps5_bsp_overlay_ps_end - ps5_bsp_overlay_ps_start);
    const uint32_t overlay_gs_size =
        (uint32_t)overlay_gs_isa + SHADER_FOOTER_BYTES;
    const uint32_t overlay_ps_size =
        (uint32_t)overlay_ps_isa + SHADER_FOOTER_BYTES;
    if (overlay_gs_isa != PS5_BSP_OVERLAY_GS_ISA_BYTES ||
        overlay_ps_isa != PS5_BSP_OVERLAY_PS_ISA_BYTES ||
        ps5_shader_header_build(overlay_gs_arena, PS5_SHADER_PRE_RASTER,
                                overlay_gs_size, &overlay_metadata) != 0 ||
        ps5_shader_header_build(overlay_ps_arena, PS5_SHADER_PIXEL,
                                overlay_ps_size, &overlay_metadata) != 0)
        return fail_pre_submit("overlay_shader_header", -1);
    memcpy(overlay_gs_code, ps5_bsp_overlay_gs_start, overlay_gs_isa);
    memcpy(overlay_ps_code, ps5_bsp_overlay_ps_start, overlay_ps_isa);
    memcpy(overlay_gs_code + overlay_gs_size - SHADER_FOOTER_BYTES,
           "barefoot", 8u);
    memcpy(overlay_ps_code + overlay_ps_size - SHADER_FOOTER_BYTES,
           "barefoot", 8u);
    void *overlay_gs_object = 0;
    void *overlay_ps_object = 0;
    result = sceAgcCreateShader(&overlay_gs_object, overlay_gs_arena,
                                overlay_gs_code);
    if (result != 0 || overlay_gs_object != overlay_gs_arena)
        return fail_pre_submit("create_overlay_gs",
                               result != 0 ? result : -1);
    result = sceAgcCreateShader(&overlay_ps_object, overlay_ps_arena,
                                overlay_ps_code);
    if (result != 0 || overlay_ps_object != overlay_ps_arena)
        return fail_pre_submit("create_overlay_ps",
                               result != 0 ? result : -1);
    struct ps5_agc_linked_cx *overlay_linked_cx =
        (struct ps5_agc_linked_cx *)(base + OVERLAY_LINKED_CX_OFFSET);
    struct ps5_agc_linked_uc *overlay_linked_uc =
        (struct ps5_agc_linked_uc *)(base + OVERLAY_LINKED_UC_OFFSET);
    result = sceAgcLinkShaders(overlay_linked_cx, overlay_linked_uc, 0,
                               overlay_gs_object, overlay_ps_object, 4u);
    if (result != 0)
        return fail_pre_submit("link_overlay_shaders", result);
    struct ps5_pipeline_registers *overlay_pipelines =
        (struct ps5_pipeline_registers *)(base + OVERLAY_PIPELINE_OFFSET);
    for (unsigned slot = 0; slot < 2u; ++slot) {
        ps5_agc_register color[PS5_COLOR_REGISTER_COUNT];
        const uintptr_t address = (uintptr_t)resources.framebuffer +
            resources.surface.buffer_offsets[slot];
        if (ps5_color_build_target(color, defaults, address,
                                   resources.surface.width,
                                   resources.surface.height) != 0 ||
            ps5_pipeline_build(
                &overlay_pipelines[slot], color, overlay_linked_cx,
                overlay_linked_uc, overlay_gs_arena->cx,
                overlay_ps_arena->cx, overlay_gs_arena->sh,
                overlay_ps_arena->sh, resources.surface.width,
                resources.surface.height) != 0)
            return fail_pre_submit("overlay_pipeline_build", -1);
    }
    ps5_agc_register *overlay_depth_disabled =
        (ps5_agc_register *)(base + OVERLAY_DEPTH_DISABLED_OFFSET);
    if (ps5_depth_build_disabled(overlay_depth_disabled) != 0)
        return fail_pre_submit("overlay_depth_state", -1);
#endif
    ps5_agc_register *depth_registers =
        (ps5_agc_register *)(base + DEPTH_REGISTERS_OFFSET);
    if (ps5_depth_build_d32_no_htile(depth_registers,
                                     (uintptr_t)resources.depth,
                                     resources.surface.width,
                                     resources.surface.height) != 0)
        return fail_pre_submit("depth_state", -1);

#ifdef PS5_GOLDSRC_PHASE4
    if (GOLDSRC_SHADER_SLOTS_OFFSET +
            GOLDSRC_SHADER_VARIANT_COUNT * PS5_SHADER_PIPELINE_SLOT_BYTES >
        SHADER_BYTES)
        return fail_pre_submit("goldsrc_shader_storage", -1);
    ps5_agc_register phase4_color_targets[2][PS5_PIPELINE_RT_REGISTERS];
    for (unsigned slot = 0; slot < 2u; ++slot) {
        const uintptr_t address = (uintptr_t)resources.framebuffer +
            resources.surface.buffer_offsets[slot];
        if (ps5_color_build_target(
                phase4_color_targets[slot], defaults, address,
                resources.surface.width, resources.surface.height) != 0)
            return fail_pre_submit("goldsrc_color_target", -1);
    }
    for (unsigned variant = 0;
         variant < GOLDSRC_SHADER_VARIANT_COUNT; ++variant) {
        const GoldSrcShaderAsset *const asset =
            &goldsrc_shader_catalog[variant];
        if (asset->variant != (GoldSrcShaderVariant)variant)
            return fail_pre_submit("goldsrc_shader_catalog", -1);
        const Ps5EmbeddedShaderPair embedded = {
            asset->gs_start,
            (size_t)(asset->gs_end - asset->gs_start),
            asset->ps_start,
            (size_t)(asset->ps_end - asset->ps_start),
            asset->metadata,
        };
        void *const shader_slot = base + GOLDSRC_SHADER_SLOTS_OFFSET +
            variant * PS5_SHADER_PIPELINE_SLOT_BYTES;
        result = ps5_shader_pipeline_slot_build(
            shader_slot, PS5_SHADER_PIPELINE_SLOT_BYTES, &embedded,
            phase4_color_targets, resources.surface.width,
            resources.surface.height, sceAgcCreateShader,
            sceAgcLinkShaders, &renderer.goldsrc_shader_slots[variant]);
        if (result != 0)
            return fail_pre_submit("goldsrc_shader_pipeline", result);
    }
    if (goldsrc_pipeline_cache_build(
            &renderer.goldsrc_pipeline_cache, UINT32_C(0x000000b6),
            UINT32_C(0x00000240)) != 0)
        return fail_pre_submit("goldsrc_pipeline_cache", -1);
    const size_t phase4_state_bytes =
        ps5_goldsrc_pipeline_runtime_register_bytes(
            renderer.goldsrc_pipeline_cache.count);
    if (GOLDSRC_STATE_REGISTERS_OFFSET + phase4_state_bytes > SHADER_BYTES ||
        ps5_goldsrc_pipeline_runtime_init(
            &renderer.goldsrc_pipeline_runtime,
            &renderer.goldsrc_pipeline_cache,
            renderer.goldsrc_shader_slots, GOLDSRC_SHADER_VARIANT_COUNT,
            base + GOLDSRC_STATE_REGISTERS_OFFSET,
            SHADER_BYTES - GOLDSRC_STATE_REGISTERS_OFFSET,
            resources.shader, SHADER_BYTES) != 0)
        return fail_pre_submit("goldsrc_pipeline_runtime", -1);
#ifdef PS5_GOLDSRC_BRUSH_GATE
    if (goldsrc_brush_plan_build(&renderer.goldsrc_brush_plan,
                                 &renderer.bsp_bundle) != 0)
        return fail_pre_submit("goldsrc_brush_plan", -1);
#ifdef PS5_GOLDSRC_PHASE4_FINAL_GATE
    if (goldsrc_brush_phase4_scene_plan_extend(
            &renderer.goldsrc_brush_plan, &renderer.bsp_bundle) != 0)
        return fail_pre_submit("goldsrc_phase4_scene_plan", -1);
#endif
#endif
#ifdef PS5_GOLDSRC_VISIBILITY_GATE
    if (goldsrc_visibility_plan_build(&renderer.goldsrc_visibility_plan,
                                      &renderer.bsp_bundle) != 0)
        return fail_pre_submit("goldsrc_visibility_plan", -1);
#endif
#ifdef PS5_GOLDSRC_STATE_MATRIX_GATE
    if (goldsrc_state_matrix_validate() != 0)
        return fail_pre_submit("goldsrc_state_matrix", -1);
    (void)ps5log_printf(PS5LOG_MARK,
        "GOLDSRC_STATE_MATRIX_READY schema=1 cases=%u hold_frames=%u "
        "readback_slots=2 coverage=blend+depth-write+cull+fog+lightmap",
        GOLDSRC_STATE_MATRIX_CASE_COUNT,
        GOLDSRC_STATE_MATRIX_HOLD_FRAMES);
#endif
#ifdef PS5_GOLDSRC_VIEWPORT_GATE
    if (GOLDSRC_VIEWPORT_FULL_OFFSET +
            PS5_VIEWPORT_SCISSOR_REGISTER_COUNT * sizeof(ps5_agc_register) >
        SHADER_BYTES)
        return fail_pre_submit("goldsrc_viewport_storage", -1);
    renderer.goldsrc_viewport_inset =
        (ps5_agc_register *)(base + GOLDSRC_VIEWPORT_INSET_OFFSET);
    renderer.goldsrc_viewport_full =
        (ps5_agc_register *)(base + GOLDSRC_VIEWPORT_FULL_OFFSET);
    const Ps5RectU32 inset_viewport = {320u, 180u, 1280u, 720u};
    const Ps5RectU32 inset_scissor = {400u, 220u, 1120u, 640u};
    const Ps5RectU32 full_rect = {
        0u, 0u, resources.surface.width, resources.surface.height,
    };
    if (ps5_viewport_scissor_build(
            renderer.goldsrc_viewport_inset, &inset_viewport,
            &inset_scissor, resources.surface.width,
            resources.surface.height) != 0 ||
        ps5_viewport_scissor_build(
            renderer.goldsrc_viewport_full, &full_rect, &full_rect,
            resources.surface.width, resources.surface.height) != 0)
        return fail_pre_submit("goldsrc_viewport_build", -1);
    (void)ps5log_printf(PS5LOG_MARK,
        "GOLDSRC_VIEWPORT_READY schema=1 framebuffer=%ux%u "
        "inset_viewport=320,180+1280x720 inset_scissor=400,220+1120x640 "
        "restore=full-frame registers=%u",
        resources.surface.width, resources.surface.height,
        PS5_VIEWPORT_SCISSOR_REGISTER_COUNT);
#endif
    (void)ps5log_printf(PS5LOG_MARK,
        "GOLDSRC_PIPELINES_READY schema=1 semantic_permutations=%u "
        "shader_variants=%u native_slots=%u base_depth=000000b6 "
        "base_raster=00000240 state_register_bytes=%llu target=gfx1013"
#ifdef PS5_REF_AGC_LIVE_PHASE7
        PS5_LIVE_CAMERA_SETTLE_TELEMETRY
#endif
        ,
        renderer.goldsrc_pipeline_cache.count,
        GOLDSRC_SHADER_VARIANT_COUNT, GOLDSRC_SHADER_VARIANT_COUNT,
        (unsigned long long)phase4_state_bytes);
#ifdef PS5_GOLDSRC_2D_GATE
    (void)ps5log_printf(PS5LOG_MARK,
        "GOLDSRC_2D_READY schema=1 framebuffer=%ux%u "
        "projection=orthographic atlas=%ux%u format=rgba8 "
        "sampler=point batches=alpha+additive "
        "components=hud+console+menu+font geometry=per-frame-transient "
        "ownership=fence+videoout",
        resources.surface.width, resources.surface.height,
        GOLDSRC_2D_ATLAS_WIDTH, GOLDSRC_2D_ATLAS_HEIGHT);
#endif
#ifdef PS5_GOLDSRC_SPRITE_PARTICLE_GATE
    (void)ps5log_printf(PS5LOG_MARK,
        "GOLDSRC_SPRITE_PARTICLE_READY schema=1 atlas=%ux%u "
        "format=rgba8 modes=control+sprite+particles+combined "
        "hold_frames=%u sprite_quads=%u alpha_particles=%u "
        "additive_particles=%u batches=alpha+additive "
        "geometry=per-frame-transient camera=locked-relative "
        "ownership=fence+videoout",
        GOLDSRC_EFFECT_ATLAS_WIDTH, GOLDSRC_EFFECT_ATLAS_HEIGHT,
        GOLDSRC_EFFECT_HOLD_FRAMES, GOLDSRC_EFFECT_SPRITE_QUADS,
        GOLDSRC_EFFECT_ALPHA_PARTICLE_QUADS,
        GOLDSRC_EFFECT_ADDITIVE_PARTICLE_QUADS);
#endif
#ifdef PS5_GOLDSRC_STUDIO_GATE
    const GoldSrcStudioBundleHeader *const studio_header =
        renderer.goldsrc_studio_bundle.header;
    (void)ps5log_printf(PS5LOG_MARK,
        "GOLDSRC_STUDIO_READY schema=1 source_fnv64=%016llx "
        "model_hash=%016llx sequence_hash=%016llx bones=%u frames=%u "
        "fps_milli=%u vertices=%u indices=%u draws=%u textures=%u "
        "chrome_textures=%u texture_bytes=%llu "
        "modes=control+textured+chrome+additive+combined "
        "hold_frames=%u skinning=cpu geometry=per-frame-transient "
        "texture_residency=shared-bsp-allocation camera=locked-relative "
        "ownership=fence+videoout",
        (unsigned long long)studio_header->source_hash,
        (unsigned long long)studio_header->model_name_hash,
        (unsigned long long)studio_header->sequence_name_hash,
        studio_header->bone_count, studio_header->frame_count,
        (uint32_t)(studio_header->fps*1000.0f+0.5f),
        studio_header->vertex_count, studio_header->index_count,
        studio_header->draw_count, studio_header->texture_count,
        renderer.goldsrc_studio_bundle.chrome_textures,
        (unsigned long long)
            renderer.goldsrc_studio_bundle.texture_pixel_bytes,
        GOLDSRC_STUDIO_HOLD_FRAMES);
#endif
#ifdef PS5_GOLDSRC_BRUSH_GATE
    const GoldSrcBrushPlan *const brush_plan =
        &renderer.goldsrc_brush_plan;
    (void)ps5log_printf(PS5LOG_MARK,
        "GOLDSRC_BRUSH_READY schema=1 models=%u entities=%u "
        "modes=control+opaque+alpha+additive+combined hold_frames=%u "
        "entity_indices=%u,%u,%u source_modes=%u,%u,%u "
        "classname_hashes=%08x,%08x,%08x draws=%u,%u,%u "
        "indices=%u,%u,%u transforms=independent animated=true "
        "geometry=real-bsp-submodels texture_residency=shared-bsp "
        "camera=locked-relative ownership=fence+videoout",
        renderer.bsp_bundle.brush_model_count,
        renderer.bsp_bundle.brush_entity_count,
        GOLDSRC_BRUSH_HOLD_FRAMES,
        brush_plan->entity_indices[0], brush_plan->entity_indices[1],
        brush_plan->entity_indices[2],
        brush_plan->source_render_modes[0],
        brush_plan->source_render_modes[1],
        brush_plan->source_render_modes[2],
        brush_plan->classname_hashes[0],
        brush_plan->classname_hashes[1],
        brush_plan->classname_hashes[2],
        brush_plan->draw_counts[0], brush_plan->draw_counts[1],
        brush_plan->draw_counts[2],
        brush_plan->index_counts[0], brush_plan->index_counts[1],
        brush_plan->index_counts[2]);
#endif
#ifdef PS5_GOLDSRC_VISIBILITY_GATE
    const GoldSrcVisibilityPlan *const visibility_plan =
        &renderer.goldsrc_visibility_plan;
    (void)ps5log_printf(PS5LOG_MARK,
        "GOLDSRC_VISIBILITY_READY schema=1 planes=%u nodes=%u leaves=%u "
        "pvs_row_bytes=%u draw_refs=%u world_first_face=%u "
        "world_face_count=%u draw_bounds=%u "
        "modes=control+pvs+frustum+combined hold_frames=%u "
        "camera=locked-bsp-tree ownership=fence+videoout",
        visibility_plan->planes, visibility_plan->nodes,
        visibility_plan->leaves, visibility_plan->pvs_row_bytes,
        visibility_plan->draw_refs, visibility_plan->world_first_face,
        visibility_plan->world_face_count, renderer.bsp_bundle.draw_count,
        GOLDSRC_VISIBILITY_HOLD_FRAMES);
#endif
#ifdef PS5_GOLDSRC_PHASE4_FINAL_GATE
    (void)ps5log_printf(PS5LOG_MARK,
        "GOLDSRC_PHASE4_SCENE_READY schema=1 water_entity=%u "
        "water_draws=%u water_indices=%u water_texture=water3 "
        "water_mode=alpha glass_entity=%u glass_draws=%u glass_indices=%u "
        "glass_texture=glass_med glass_mode=alpha source=real-bsp-brushes",
        brush_plan->entity_indices[GOLDSRC_BRUSH_INSTANCE_WATER],
        brush_plan->draw_counts[GOLDSRC_BRUSH_INSTANCE_WATER],
        brush_plan->index_counts[GOLDSRC_BRUSH_INSTANCE_WATER],
        brush_plan->entity_indices[GOLDSRC_BRUSH_INSTANCE_GLASS],
        brush_plan->draw_counts[GOLDSRC_BRUSH_INSTANCE_GLASS],
        brush_plan->index_counts[GOLDSRC_BRUSH_INSTANCE_GLASS]);
    (void)ps5log_printf(PS5LOG_MARK,
        "GOLDSRC_PHASE4_FINAL_READY schema=1 frames=%u "
        "combined_window=59400..59999 semantic_permutations=%u "
        "shader_variants=%u components=lighting+2d+sprites+particles+"
        "studio+brush+pvs+frustum world_draws=%u brush_draws=69 "
        "studio_draws=12 effect_draws=3 screen_draws=2 "
        "skinning=cpu transient=per-slot camera=locked-bsp-tree "
        "ownership=fence+videoout",
        BSP_GATE_FRAME_COUNT, renderer.goldsrc_pipeline_cache.count,
        GOLDSRC_SHADER_VARIANT_COUNT,
        renderer.goldsrc_visibility_plan.world_face_count);
#endif
#endif

#ifdef PS5_BSP_VIEWER
    if (prepare_bsp_scene() != 0)
        return fail_pre_submit("bsp_scene", -1);
#ifdef PS5_TEXTURE_PATH
#ifndef PS5_REF_AGC_LIVE_PHASE7
    if (bsp_alpha_test_plan(&renderer.bsp_bundle,
                            renderer.noclip.position,
                            &renderer.alpha_test_plan) != 0)
        return fail_pre_submit("alpha_test_plan", -1);
#ifdef PS5_TEXTURE_ALPHA_GATE
    memcpy(renderer.noclip.forward,
           renderer.alpha_test_plan.target_forward,
           sizeof(renderer.noclip.forward));
#endif
    if (bsp_sky_plan(&renderer.bsp_bundle, renderer.noclip.position,
                     &renderer.sky_plan) != 0)
        return fail_pre_submit("sky_plan", -1);
#ifdef PS5_TEXTURE_SKY_GATE
    memcpy(renderer.noclip.position, renderer.sky_plan.target_position,
           sizeof(renderer.noclip.position));
    memcpy(renderer.noclip.forward, renderer.sky_plan.target_forward,
           sizeof(renderer.noclip.forward));
#endif
#else
    uint32_t live_opaque_draws = 0u;
    uint32_t live_alpha_draws = 0u;
    uint32_t live_sky_draws = 0u;
    if (bsp_resource_draw_counts(
            &renderer.bsp_bundle, &live_opaque_draws,
            &live_alpha_draws, &live_sky_draws) != 0 ||
        live_opaque_draws + live_alpha_draws + live_sky_draws !=
            renderer.bsp_bundle.draw_count)
        return fail_pre_submit("live_draw_classification", -1);
    (void)ps5log_printf(PS5LOG_MARK,
        "REF_AGC_LIVE_DRAW_CLASSES opaque=%u alpha_test=%u sky=%u "
        "total=%u source=baked-c1a0",
        live_opaque_draws, live_alpha_draws, live_sky_draws,
        renderer.bsp_bundle.draw_count);
#endif
#endif
#ifdef PS5_BSP_TEXTURED
    const uint32_t bsp_draw_dwords =
        renderer.bsp_draw_count * BSP_TEXTURED_DWORDS_PER_DRAW;
#else
    const uint32_t bsp_draw_dwords =
        renderer.bsp_draw_count * BSP_FLAT_DWORDS_PER_DRAW;
#endif
    (void)ps5log_printf(PS5LOG_MARK,
        "BSP_BUNDLE_READY vertices=%u indices=%u map_draws=%u "
        "scene_draws=%u draw_dwords=%u command_slot_bytes=%llu "
        "allocation_bytes=%llu",
        renderer.bsp_bundle.vertex_count, renderer.bsp_bundle.index_count,
        renderer.bsp_bundle.draw_count, renderer.bsp_draw_count,
        bsp_draw_dwords,
        (unsigned long long)renderer.command_slot_bytes,
        (unsigned long long)resources.bsp_bytes);
#ifdef PS5_RESOURCE_FOUNDATION
#ifdef PS5_TEXTURE_PATH
    (void)ps5log_printf(PS5LOG_MARK,
        "RESOURCE_HEAP_READY bytes=%llu allocations=6 "
        "pool=fence-retired transient_slots=2 slot_bytes=%llu",
        (unsigned long long)resources.resource_heap_bytes,
        (unsigned long long)renderer.transient_ring.slots[0].bytes);
    (void)ps5log_printf(PS5LOG_MARK,
        "DYNAMIC_LIGHTMAP_READY image=%ux%u image_bytes=%llu "
        "patch=%u,%u+%ux%u hit_face=%u patch_bytes=%llu "
        "dirty_span_bytes=%llu slots=2 guards=256 "
#ifdef PS5_GOLDSRC_LIGHTING_GATE
        "selection=largest-styled-face",
#else
        "selection=center-ray",
#endif
        renderer.dynamic_lightmap_layout.image_width,
        renderer.dynamic_lightmap_layout.image_height,
        (unsigned long long)renderer.dynamic_lightmap_layout.image_bytes,
        renderer.dynamic_lightmap_layout.patch_x,
        renderer.dynamic_lightmap_layout.patch_y,
        renderer.dynamic_lightmap_layout.patch_width,
        renderer.dynamic_lightmap_layout.patch_height,
        renderer.dynamic_lightmap_layout.hit_face,
        (unsigned long long)renderer.dynamic_lightmap_layout.patch_bytes,
        (unsigned long long)
            renderer.dynamic_lightmap_layout.dirty_span_bytes);
#ifdef PS5_GOLDSRC_LIGHTING_GATE
    (void)ps5log_printf(PS5LOG_MARK,
        "GOLDSRC_LIGHTING_READY schema=1 face=%u draw=%u "
        "styles=%u styled_faces=%u styled_layers=%u "
        "atlas=%ux%u patch=%u,%u+%ux%u patch_bytes=%llu "
        "source_samples=%u source_hash=%016llx "
        "modes=base+lightstyle+dlight+combined hold_frames=%u "
        "lightstyle_hz=10 dlight=face-local-radial "
        "camera=face-normal-standoff target=%.3f,%.3f,%.3f "
        "forward=%.6f,%.6f,%.6f upload=phase3-bounded",
        renderer.goldsrc_lighting_plan.layout.hit_face,
        renderer.goldsrc_lighting_plan.draw_index,
        renderer.goldsrc_lighting_plan.style_count,
        renderer.goldsrc_lighting_plan.styled_face_count,
        renderer.goldsrc_lighting_plan.styled_layer_count,
        renderer.goldsrc_lighting_plan.layout.image_width,
        renderer.goldsrc_lighting_plan.layout.image_height,
        renderer.goldsrc_lighting_plan.layout.patch_x,
        renderer.goldsrc_lighting_plan.layout.patch_y,
        renderer.goldsrc_lighting_plan.layout.patch_width,
        renderer.goldsrc_lighting_plan.layout.patch_height,
        (unsigned long long)
            renderer.goldsrc_lighting_plan.layout.patch_bytes,
        renderer.goldsrc_lighting_plan.face->sample_bytes,
        (unsigned long long)renderer.goldsrc_lighting_plan.source_hash,
        GOLDSRC_LIGHTING_HOLD_FRAMES,
        renderer.goldsrc_lighting_plan.target_position[0],
        renderer.goldsrc_lighting_plan.target_position[1],
        renderer.goldsrc_lighting_plan.target_position[2],
        renderer.goldsrc_lighting_plan.target_forward[0],
        renderer.goldsrc_lighting_plan.target_forward[1],
        renderer.goldsrc_lighting_plan.target_forward[2]);
#endif
#ifdef PS5_TEXTURE_ACCOUNTING_ENABLED
    const BspTextureResidency *const texture_residency =
        &renderer.texture_accounting.residency;
    (void)ps5log_printf(PS5LOG_MARK,
        "TEXTURE_RESIDENCY_READY schema=1 pool_capacity_bytes=%llu "
        "pool_resident_bytes=%llu bsp_allocation_bytes=%llu "
        "shader_allocation_bytes=%llu depth_allocation_bytes=%llu "
        "transient_allocation_bytes=%llu "
        "lightmap_allocation_bytes=%llu texture_payload_bytes=%llu "
        "base_texture_bytes=%llu source_lightmap_bytes=%llu "
        "dynamic_lightmap_image_bytes=%llu lightmap_slots=%u "
        "allocations=6 accounting=exact",
        (unsigned long long)texture_residency->pool_capacity_bytes,
        (unsigned long long)texture_residency->pool_resident_bytes,
        (unsigned long long)texture_residency->bsp_allocation_bytes,
        (unsigned long long)texture_residency->shader_allocation_bytes,
        (unsigned long long)texture_residency->depth_allocation_bytes,
        (unsigned long long)texture_residency->transient_allocation_bytes,
        (unsigned long long)
            texture_residency->dynamic_lightmap_allocation_bytes,
        (unsigned long long)texture_residency->texture_payload_bytes,
        (unsigned long long)texture_residency->base_texture_bytes,
        (unsigned long long)texture_residency->source_lightmap_bytes,
        (unsigned long long)texture_residency->dynamic_lightmap_image_bytes,
        texture_residency->dynamic_lightmap_slots);
#endif
#else
    (void)ps5log_printf(PS5LOG_MARK,
        "RESOURCE_HEAP_READY bytes=%llu allocations=4 "
        "pool=fence-retired transient_slots=2 slot_bytes=%llu",
        (unsigned long long)resources.resource_heap_bytes,
        (unsigned long long)renderer.transient_ring.slots[0].bytes);
#endif
#if defined(PS5_TEXTURE_MIP_GATE) || defined(PS5_TEXTURE_ALPHA_GATE) || \
    defined(PS5_TEXTURE_SKY_GATE) || \
    defined(PS5_TEXTURE_ACCOUNTING_ENABLED)
    uint32_t minimum_mips = 15u;
    uint32_t maximum_mips = 0u;
    uint64_t mip_chain_bytes = 0u;
    for (uint32_t texture = 0u;
         texture < renderer.bsp_bundle.texture_count; ++texture) {
        const BspBundleTexture *const item =
            &renderer.bsp_bundle.textures[texture];
        if (item->mip_count < minimum_mips)
            minimum_mips = item->mip_count;
        if (item->mip_count > maximum_mips)
            maximum_mips = item->mip_count;
        mip_chain_bytes += item->bytes;
    }
    (void)ps5log_printf(PS5LOG_MARK,
        "MIP_CHAINS_READY textures=%u layout=addr-sw-linear "
        "order=smallest-to-base pitch_alignment=256 levels_min=%u "
        "levels_max=%u chain_bytes=%llu deterministic=box-rne",
        renderer.bsp_bundle.texture_count, minimum_mips, maximum_mips,
        (unsigned long long)mip_chain_bytes);
#endif
    (void)ps5log_printf(PS5LOG_MARK,
        "BSP_TEXTURE_TABLE_LAYOUT textures=%u descriptor_dwords=%u "
        "storage=per-frame-transient lightmap=%ux%u "
        "base_mode=repeat lightmap_mode=clamp "
#ifdef PS5_TEXTURE_MIP_GATE
        "base_filter=alternating-trilinear-anisotropic4x "
        "lightmap_filter=bilinear "
#else
        "base_filter=anisotropic4x lightmap_filter=bilinear "
#endif
        "composition=base_x_lightmap",
        renderer.bsp_bundle.texture_count,
        renderer.bsp_plan.descriptor_table_dwords,
        renderer.bsp_bundle.lightmap_image->width,
        renderer.bsp_bundle.lightmap_image->height);
#ifdef PS5_TEXTURE_ALPHA_GATE
    uint32_t opaque_draws = 0u;
    uint32_t alpha_test_draws = 0u;
    uint32_t sky_draws = 0u;
    uint32_t opaque_db_shader_control = 0u;
    uint32_t alpha_db_shader_control = 0u;
    int opaque_db_found = 0;
    int alpha_db_found = 0;
    for (size_t index = 0u;
         index < sizeof(ps5_bsp_resource_pixel_cx) /
                     sizeof(ps5_bsp_resource_pixel_cx[0]); ++index)
        if (ps5_bsp_resource_pixel_cx[index].offset == 0x203u) {
            opaque_db_shader_control =
                ps5_bsp_resource_pixel_cx[index].value;
            opaque_db_found = 1;
        }
    for (size_t index = 0u;
         index < sizeof(ps5_bsp_alpha_test_pixel_cx) /
                     sizeof(ps5_bsp_alpha_test_pixel_cx[0]); ++index)
        if (ps5_bsp_alpha_test_pixel_cx[index].offset == 0x203u) {
            alpha_db_shader_control =
                ps5_bsp_alpha_test_pixel_cx[index].value;
            alpha_db_found = 1;
        }
    if (bsp_resource_draw_counts(&renderer.bsp_bundle, &opaque_draws,
                                 &alpha_test_draws, &sky_draws) != 0 ||
        alpha_test_draws != renderer.alpha_test_plan.draw_count ||
        opaque_draws + alpha_test_draws + sky_draws !=
            renderer.bsp_bundle.draw_count ||
        !opaque_db_found || !alpha_db_found ||
        (opaque_db_shader_control & 0x40u) != 0u ||
        (alpha_db_shader_control & 0x40u) == 0u)
        return fail_pre_submit("alpha_test_draw_classification", -1);
    (void)ps5log_printf(PS5LOG_MARK,
        "ALPHA_TEST_READY textures=%u draws=%u opaque_draws=%u "
        "target_texture=%u target_face=%u camera=nearest-centroid "
        "opaque_pipeline=bsp_resource alpha_pipeline=bsp_alpha_test "
        "opaque_db=%08x alpha_db=%08x kill_bit=0x40 "
        "threshold=0.5 depth_write=enabled blend=disabled",
        renderer.alpha_test_plan.texture_count,
        renderer.alpha_test_plan.draw_count, opaque_draws,
        renderer.alpha_test_plan.target_texture,
        renderer.alpha_test_plan.target_face,
        opaque_db_shader_control, alpha_db_shader_control);
#endif
#ifdef PS5_TEXTURE_SKY_GATE
    uint32_t opaque_draws = 0u;
    uint32_t alpha_test_draws = 0u;
    uint32_t sky_draws = 0u;
    const int sky_shader_distinct =
        PS5_BSP_RESOURCE_PS_ISA_BYTES != PS5_BSP_SKY_PS_ISA_BYTES ||
        memcmp(ps5_bsp_resource_ps_start, ps5_bsp_sky_ps_start,
               PS5_BSP_RESOURCE_PS_ISA_BYTES) != 0;
    if (bsp_resource_draw_counts(&renderer.bsp_bundle, &opaque_draws,
                                 &alpha_test_draws, &sky_draws) != 0 ||
        sky_draws != renderer.sky_plan.draw_count ||
        opaque_draws + alpha_test_draws + sky_draws !=
            renderer.bsp_bundle.draw_count || !sky_shader_distinct)
        return fail_pre_submit("sky_draw_classification", -1);
    (void)ps5log_printf(PS5LOG_MARK,
        "SKY_PASS_READY textures=%u draws=%u opaque_draws=%u "
        "alpha_draws=%u target_texture=%u target_face=%u "
        "camera=nearest-centroid-standoff pipeline=bsp_sky "
        "composition=base-unlit "
        "depth_write=enabled blend=disabled opaque_ps_bytes=%u "
        "sky_ps_bytes=%u shader_distinct=true",
        renderer.sky_plan.texture_count, renderer.sky_plan.draw_count,
        opaque_draws, alpha_test_draws, renderer.sky_plan.target_texture,
        renderer.sky_plan.target_face, PS5_BSP_RESOURCE_PS_ISA_BYTES,
        PS5_BSP_SKY_PS_ISA_BYTES);
#endif
#ifdef PS5_TEXTURE_ACCOUNTING_ENABLED
    uint32_t accounting_opaque_draws = 0u;
    uint32_t accounting_alpha_draws = 0u;
    uint32_t accounting_sky_draws = 0u;
    if (bsp_resource_draw_counts(
            &renderer.bsp_bundle, &accounting_opaque_draws,
            &accounting_alpha_draws, &accounting_sky_draws) != 0 ||
        accounting_alpha_draws != renderer.alpha_test_plan.draw_count ||
        accounting_sky_draws != renderer.sky_plan.draw_count ||
        accounting_opaque_draws + accounting_alpha_draws +
                accounting_sky_draws != renderer.bsp_bundle.draw_count)
        return fail_pre_submit("texture_feature_classification", -1);
    (void)ps5log_printf(PS5LOG_MARK,
        "TEXTURE_FEATURES_READY mip_chains=%u opaque_draws=%u "
        "alpha_draws=%u sky_draws=%u total_draws=%u pipelines=4 "
        "composition=opaque+alpha+sky sampler=anisotropic4x",
        renderer.bsp_bundle.texture_count, accounting_opaque_draws,
        accounting_alpha_draws, accounting_sky_draws,
        renderer.bsp_bundle.draw_count);
#endif
#elif defined(PS5_BSP_TEXTURED)
    (void)ps5log_printf(PS5LOG_MARK,
        "BSP_TEXTURE_TABLES_READY textures=%u descriptor_dwords=%u "
        "lightmap=%ux%u base_mode=repeat lightmap_mode=clamp "
        "filter=bilinear composition=base_x_lightmap",
        renderer.bsp_bundle.texture_count,
        renderer.bsp_plan.descriptor_table_dwords,
        renderer.bsp_bundle.lightmap_image->width,
        renderer.bsp_bundle.lightmap_image->height);
#endif
#else
    static const size_t vertex_offsets[3] = {
        GEAR0_OFFSET, GEAR1_OFFSET, GEAR2_OFFSET
    };
    static const GearSpec specs[3] = {
        {0.2500f, 0.9125f, 1.0875f, 0.250f, 20u},
        {0.1250f, 0.4125f, 0.5875f, 0.500f, 10u},
        {0.3250f, 0.4125f, 0.5875f, 0.125f, 10u},
    };
    for (unsigned gear = 0; gear < 3u; ++gear) {
        GearsVertex *vertices = (GearsVertex *)(base + vertex_offsets[gear]);
        const size_t count = gears_mesh_vertex_count(specs[gear].teeth);
        if (gears_build_mesh(vertices, count, &specs[gear]) != count)
            return fail_pre_submit("gear_mesh", -1);
        uint32_t *srd = (uint32_t *)(base + GEAR_SRD_OFFSET + gear * 16u);
        const uintptr_t vertex_address = (uintptr_t)vertices;
        const uintptr_t table_address = (uintptr_t)srd;
        if ((table_address >> 32) != UINT64_C(2))
            return fail_pre_submit("srd_aperture", -1);
        srd[0] = (uint32_t)vertex_address;
        srd[1] = (uint32_t)((vertex_address >> 32) & 0xffffu) | (24u << 16);
        srd[2] = (uint32_t)count;
        srd[3] = UINT32_C(0x11014fac);
        renderer.srd_tables[gear] = (uint32_t)table_address;
        renderer.vertex_counts[gear] = (uint32_t)count;
    }
    GearsRtClearVertex *clear_vertices =
        (GearsRtClearVertex *)(base + CLEAR_VERTEX_OFFSET);
    uint32_t *clear_srd = (uint32_t *)(base + CLEAR_SRD_OFFSET);
    const uintptr_t clear_vertex_address = (uintptr_t)clear_vertices;
    const uintptr_t clear_table_address = (uintptr_t)clear_srd;
    if ((clear_table_address >> 32) != UINT64_C(2))
        return fail_pre_submit("clear_srd_aperture", -1);
    clear_srd[0] = (uint32_t)clear_vertex_address;
    clear_srd[1] = (uint32_t)((clear_vertex_address >> 32) & 0xffffu) |
                   (24u << 16);
    clear_srd[2] = GEARS_RT_CLEAR_VERTEX_COUNT;
    clear_srd[3] = UINT32_C(0x11014fac);
    const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    if (gears_rt_clear_build(clear_vertices, &renderer.clear_draw,
                             (uint32_t)clear_table_address, black) != 0)
        return fail_pre_submit("rt_clear_build", -1);
#endif

    volatile uint32_t *depth_guard = (volatile uint32_t *)(
        (uint8_t *)resources.depth + DEPTH_BYTES);
    for (unsigned word = 0; word < 16u; ++word)
        depth_guard[word] = GUARD_WORD;
    ps5_native_cache_flush((const void *)depth_guard, 64u);
    ps5_native_cache_flush(resources.shader, SHADER_BYTES);

    renderer.resources = &resources;
#ifdef PS5_BSP_VIEWER
    renderer.commands[0] = (uint32_t *)((uint8_t *)resources.command +
                                        command_plan.slot_offsets[0]);
    renderer.commands[1] = (uint32_t *)((uint8_t *)resources.command +
                                        command_plan.slot_offsets[1]);
    renderer.fences[0] = (volatile uint64_t *)(
        (uint8_t *)resources.command + command_plan.fence_offsets[0]);
    renderer.fences[1] = (volatile uint64_t *)(
        (uint8_t *)resources.command + command_plan.fence_offsets[1]);
#ifdef PS5_GPU_FLIP_TIMING_GATE
    renderer.gpu_eop_timestamps[0] = (volatile uint64_t *)(
        (uint8_t *)resources.command + command_plan.timestamp_offsets[0]);
    renderer.gpu_eop_timestamps[1] = (volatile uint64_t *)(
        (uint8_t *)resources.command + command_plan.timestamp_offsets[1]);
    if (ps5_gpu_flip_timing_init(&renderer.gpu_flip_timing,
                                 BSP_GATE_FRAME_COUNT) != 0)
        return fail_pre_submit("gpu_flip_timing_init", -1);
    (void)ps5log_printf(PS5LOG_MARK,
        "GPU_FLIP_TIMING_BEGIN schema=1 frames=%u slots=2 "
        "gpu_clock_unit=raw-ticks gpu_packet=release_mem-data_sel_3 "
        "packet_order=draws,timestamp,setflip,ownership-fence "
        "cpu_clock=monotonic latency_anchor=submit-begin "
        "flip_observation=exact-videoout-event",
        BSP_GATE_FRAME_COUNT);
#endif
#else
    renderer.commands[0] = resources.command;
    renderer.commands[1] = (uint32_t *)((uint8_t *)resources.command +
                                         0x2000u);
    renderer.fences[0] = (volatile uint64_t *)((uint8_t *)resources.command +
                                               FENCE0_OFFSET);
    renderer.fences[1] = (volatile uint64_t *)((uint8_t *)resources.command +
                                               FENCE1_OFFSET);
#endif
    renderer.pipelines[0] = &pipelines[0];
    renderer.pipelines[1] = &pipelines[1];
#ifdef PS5_RESOURCE_FOUNDATION
    renderer.overlay_pipelines[0] = &overlay_pipelines[0];
    renderer.overlay_pipelines[1] = &overlay_pipelines[1];
    renderer.alpha_test_pipelines[0] = &alpha_test_pipelines[0];
    renderer.alpha_test_pipelines[1] = &alpha_test_pipelines[1];
    renderer.sky_pipelines[0] = &sky_pipelines[0];
    renderer.sky_pipelines[1] = &sky_pipelines[1];
    renderer.overlay_draw_modifier = overlay_metadata.draw_modifier;
    renderer.overlay_depth_disabled = overlay_depth_disabled;
    if (PS5_PIPELINE_PERMUTATION_COUNT != 4 ||
        ps5_pipeline_permutations[PS5_PIPELINE_BSP_RESOURCE]
                .gs_application_words != 2u ||
        ps5_pipeline_permutations[PS5_PIPELINE_BSP_ALPHA_TEST]
                .gs_application_words != 2u ||
        ps5_pipeline_permutations[PS5_PIPELINE_BSP_SKY]
                .gs_application_words != 2u ||
        ps5_pipeline_permutations[PS5_PIPELINE_BSP_OVERLAY]
                .gs_application_words != 1u)
        return fail_pre_submit("pipeline_permutation_table", -1);
    (void)ps5log_printf(PS5LOG_MARK,
        "RESOURCE_PIPELINES_READY count=%u map=%s alpha_test=%s sky=%s "
        "overlay=%s map_gs_words=%u alpha_gs_words=%u sky_gs_words=%u "
        "overlay_gs_words=%u "
        "overlay_depth=disabled",
        PS5_PIPELINE_PERMUTATION_COUNT,
        ps5_pipeline_permutations[PS5_PIPELINE_BSP_RESOURCE].name,
        ps5_pipeline_permutations[PS5_PIPELINE_BSP_ALPHA_TEST].name,
        ps5_pipeline_permutations[PS5_PIPELINE_BSP_SKY].name,
        ps5_pipeline_permutations[PS5_PIPELINE_BSP_OVERLAY].name,
        ps5_pipeline_permutations[PS5_PIPELINE_BSP_RESOURCE]
            .gs_application_words,
        ps5_pipeline_permutations[PS5_PIPELINE_BSP_ALPHA_TEST]
            .gs_application_words,
        ps5_pipeline_permutations[PS5_PIPELINE_BSP_SKY]
            .gs_application_words,
        ps5_pipeline_permutations[PS5_PIPELINE_BSP_OVERLAY]
            .gs_application_words);
#endif
    renderer.depth_registers = depth_registers;
    renderer.draw_modifier = metadata.draw_modifier;
    ps5_native_submit_context_init(&renderer.submit, resources.command,
                                   resources.command_bytes);

    GearsFrameRunnerInput input = {0};
    input.start_ns = now_ns(0);
    input.first_flip_token = UINT64_C(0x0000420000000001);
    input.present_interval_budget_ns = UINT64_C(17000000);
#ifdef PS5_BSP_VIEWER
    for (unsigned index = 0; index < GEARS_SCENE_DRAW_COUNT; ++index) {
        input.srd_tables[index] = 1u;
        input.vertex_counts[index] = 3u;
    }
#else
    memcpy(input.srd_tables, renderer.srd_tables, sizeof(input.srd_tables));
    memcpy(input.vertex_counts, renderer.vertex_counts,
           sizeof(input.vertex_counts));
#endif
    input.now_ns = now_ns;
    input.compose = frame_compose;
    input.submit = frame_submit;
    input.wait_gpu = frame_wait_gpu;
    input.wait_videoout = frame_wait_video;
    input.telemetry = frame_telemetry;
    input.user = &renderer;
#ifdef PS5_BSP_VIEWER
#ifdef PS5_TEXTURE_PATH
#ifdef PS5_REF_AGC_LIVE_PHASE7
    (void)ps5log_line(PS5LOG_MARK,
        "BSP_LOOP_BEGIN mode=phase7-live-consumer buffers=2 "
        "color_dma=false depth_dma=true indexed=true frames=engine-owned "
        "camera=live-refapi geometry=live-refapi textures=live-refapi "
        "lists=world lightmaps=live-atlas retirement=fence+videoout+ack "
        "input_dependency=engine");
#elif defined(PS5_GPU_FLIP_TIMING_GATE)
    (void)ps5log_line(PS5LOG_MARK,
        "BSP_LOOP_BEGIN mode=gpu-flip-timing-soak buffers=2 "
        "color_dma=false depth_dma=true indexed=true frames=60000 "
        "features=phase4-final-integrated timestamp=eop-release-mem "
        "camera=locked-bsp-tree geometry=world+entities "
        "descriptors=per-frame overlay=fixed "
        "retirement=fence+videoout input_dependency=none");
#elif defined(PS5_GOLDSRC_PHASE4_FINAL_GATE)
#ifdef PS5_REF_AGC_MODULE
    (void)ps5log_line(PS5LOG_MARK,
        "BSP_LOOP_BEGIN mode=ref-agc-integration buffers=2 "
        "color_dma=false depth_dma=true indexed=true frames=600 "
        "features=lighting+2d+sprites+particles+studio+brush+pvs+frustum "
        "combined_window=0..599 camera=locked-bsp-tree "
        "geometry=phase4-baked-c1a0e descriptors=per-frame overlay=fixed "
        "retirement=fence+videoout input_dependency=none");
#else
    (void)ps5log_line(PS5LOG_MARK,
        "BSP_LOOP_BEGIN mode=goldsrc-phase4-final-soak buffers=2 "
        "color_dma=false depth_dma=true indexed=true frames=60000 "
        "features=lighting+2d+sprites+particles+studio+brush+pvs+frustum "
        "combined_window=59400..59999 camera=locked-bsp-tree "
        "geometry=world+entities descriptors=per-frame overlay=fixed "
        "retirement=fence+videoout input_dependency=none");
#endif
#elif defined(PS5_GOLDSRC_VISIBILITY_GATE)
    (void)ps5log_line(PS5LOG_MARK,
        "BSP_LOOP_BEGIN mode=goldsrc-visibility-soak buffers=2 "
        "color_dma=false depth_dma=true indexed=true frames=10000 "
        "modes=control+pvs+frustum+combined camera=locked-bsp-tree "
        "geometry=world-model-only descriptors=per-frame overlay=fixed "
        "retirement=fence+videoout input_dependency=none");
#elif defined(PS5_GOLDSRC_BRUSH_GATE)
    (void)ps5log_line(PS5LOG_MARK,
        "BSP_LOOP_BEGIN mode=goldsrc-brush-soak buffers=2 "
        "color_dma=false depth_dma=true indexed=true frames=10000 "
        "modes=control+opaque+alpha+additive+combined "
        "transforms=independent geometry=real-bsp-submodels "
        "camera=locked-proof-view descriptors=per-frame overlay=fixed "
        "retirement=fence+videoout input_dependency=none");
#elif defined(PS5_GOLDSRC_STUDIO_GATE)
    (void)ps5log_line(PS5LOG_MARK,
        "BSP_LOOP_BEGIN mode=goldsrc-studio-soak buffers=2 "
        "color_dma=false depth_dma=true indexed=true frames=10000 "
        "modes=control+textured+chrome+additive+combined "
        "animation=cpu-skinning geometry=per-frame-transient "
        "textures=private-bundle camera=locked-proof-view "
        "descriptors=per-frame overlay=fixed "
        "retirement=fence+videoout input_dependency=none");
#elif defined(PS5_GOLDSRC_SPRITE_PARTICLE_GATE)
    (void)ps5log_line(PS5LOG_MARK,
        "BSP_LOOP_BEGIN mode=goldsrc-sprite-particle-soak buffers=2 "
        "color_dma=false depth_dma=true indexed=true frames=10000 "
        "effects=control+sprite+particles+combined "
        "geometry=per-frame-transient camera=locked-proof-view "
        "descriptors=per-frame overlay=fixed "
        "retirement=fence+videoout input_dependency=none");
#elif defined(PS5_GOLDSRC_LIGHTING_GATE)
    (void)ps5log_line(PS5LOG_MARK,
        "BSP_LOOP_BEGIN mode=goldsrc-lighting-soak buffers=2 "
        "color_dma=false depth_dma=true indexed=true frames=10000 "
        "modes=base+lightstyle+dlight+combined "
        "lightmap_upload=phase3-bounded descriptors=per-frame "
        "camera=locked-proof-view overlay=fixed "
        "retirement=fence+videoout input_dependency=none");
#elif defined(PS5_TEXTURE_FINAL_GATE)
    (void)ps5log_line(PS5LOG_MARK,
        "BSP_LOOP_BEGIN mode=texture-path-final-soak buffers=2 "
        "color_dma=false depth_dma=true indexed=true frames=60000 "
        "opaque_pass=separate alpha_test_pass=separate sky_pass=separate "
        "sampler=anisotropic4x dynamic_lightmap=bounded "
        "residency=partitioned uploads=checked-per-frame "
        "descriptors=per-frame overlay=fixed retirement=fence+videoout "
        "input_dependency=none");
#elif defined(PS5_TEXTURE_ACCOUNTING_GATE)
    (void)ps5log_line(PS5LOG_MARK,
        "BSP_LOOP_BEGIN mode=texture-path-accounting-soak buffers=2 "
        "color_dma=false depth_dma=true indexed=true frames=10000 "
        "opaque_pass=separate alpha_test_pass=separate sky_pass=separate "
        "sampler=anisotropic4x dynamic_lightmap=bounded "
        "residency=partitioned uploads=checked-per-frame "
        "descriptors=per-frame overlay=fixed retirement=fence+videoout "
        "input_dependency=none");
#elif defined(PS5_TEXTURE_SKY_GATE)
    (void)ps5log_line(PS5LOG_MARK,
        "BSP_LOOP_BEGIN mode=texture-path-sky-soak buffers=2 "
        "color_dma=false depth_dma=true indexed=true frames=10000 "
        "opaque_pass=separate alpha_test_pass=separate sky_pass=separate "
        "sampler=anisotropic4x lightmap_pattern=paired "
        "descriptors=per-frame overlay=fixed retirement=fence+videoout");
#elif defined(PS5_TEXTURE_ALPHA_GATE)
    (void)ps5log_line(PS5LOG_MARK,
        "BSP_LOOP_BEGIN mode=texture-path-alpha-soak buffers=2 "
        "color_dma=false depth_dma=true indexed=true frames=10000 "
        "opaque_pass=separate alpha_test_pass=separate "
        "sampler=anisotropic4x lightmap_pattern=paired "
        "descriptors=per-frame overlay=fixed retirement=fence+videoout");
#elif defined(PS5_TEXTURE_MIP_GATE)
    (void)ps5log_line(PS5LOG_MARK,
        "BSP_LOOP_BEGIN mode=texture-path-mip-soak buffers=2 "
        "color_dma=false depth_dma=true indexed=true frames=10000 "
        "mip_layout=addr-sw-linear samplers=trilinear+anisotropic4x "
        "lightmap_pattern=paired descriptors=per-frame "
        "overlay=fixed retirement=fence+videoout");
#else
    (void)ps5log_line(PS5LOG_MARK,
        "BSP_LOOP_BEGIN mode=texture-path-lightmap-soak buffers=2 "
        "color_dma=false depth_dma=true indexed=true frames=10000 "
        "dynamic_lightmap=bounded descriptors=per-frame "
        "overlay=fixed retirement=fence+videoout");
#endif
#elif defined(PS5_RESOURCE_FOUNDATION)
    (void)ps5log_line(PS5LOG_MARK,
        "BSP_LOOP_BEGIN mode=resource-foundation-soak buffers=2 "
        "color_dma=false depth_dma=true indexed=true frames=60000 "
        "constants=per-frame descriptors=per-frame overlay=transient "
        "retirement=fence+videoout");
#elif defined(PS5_BSP_TEXTURED)
    (void)ps5log_line(PS5LOG_MARK,
        "BSP_LOOP_BEGIN mode=textured-noclip-soak buffers=2 "
        "color_dma=false depth_dma=true indexed=true frames=60000 "
        "composition=base_x_lightmap");
#elif defined(PS5_BSP_NOCLIP)
    (void)ps5log_line(PS5LOG_MARK,
        "BSP_LOOP_BEGIN mode=noclip-soak buffers=2 "
        "color_dma=false depth_dma=true indexed=true frames=10000");
#else
    (void)ps5log_line(PS5LOG_MARK,
        "BSP_LOOP_BEGIN mode=fixed-camera buffers=2 "
        "color_dma=false depth_dma=true indexed=true");
#endif
#else
    (void)ps5log_line(PS5LOG_MARK,
        "GEARS_LOOP_BEGIN mode=continuous buffers=2 "
        "color_dma=false depth_dma=true");
#endif
    GearsFrameLoop loop;
#ifdef PS5_REF_AGC_MODULE
    PS5_RefAgcRuntimeReady();
    (void)ps5log_line(PS5LOG_MARK,
        "REF_AGC_RUNTIME_READY backend=phase4-native api=18 "
        "videoout=owned direct_memory=owned agc=initialized scene=planned");
#endif
    if (gears_frame_loop_init(&loop, &input) != 0)
        return fail_pre_submit("frame_loop_init", -1);
#ifdef PS5_BSP_VIEWER
#ifdef PS5_REF_AGC_LIVE_PHASE7
    for (;;) {
        const int wait_result = PS5_RefAgcWaitLiveFrame(
            renderer.live_last_serial, &renderer.live_frame);
        if (wait_result == 1)
            break;
        if (wait_result != 0)
            park("live-frame-wait-failure");
        if (renderer.live_frame.serial != renderer.live_last_serial + 1u ||
            renderer.live_frame.dropped_entities != 0u ||
            renderer.live_frame.dropped_2d_commands != 0u)
            park("live-frame-sequence-or-capacity-failure");
        renderer.live_frame_valid = 1;
        if (renderer.live_frame.view.valid)
            ++renderer.live_view_frames;
        if (renderer.live_frame.serial == 1u ||
            (renderer.live_frame.view.valid &&
             renderer.live_view_frames == 1u) ||
            renderer.live_frame.serial % 600u == 0u)
            (void)ps5log_printf(PS5LOG_MARK,
                "REF_AGC_LIVE_FRAME_INPUT serial=%llu map_serial=%llu "
                "view_valid=%u viewport=%d,%d,%d,%d "
                "origin_milli=%d,%d,%d angles_milli=%d,%d,%d "
                "entities=%u draw2d=%u drops=zero",
                (unsigned long long)renderer.live_frame.serial,
                (unsigned long long)renderer.live_frame.map_serial,
                renderer.live_frame.view.valid,
                renderer.live_frame.view.viewport[0],
                renderer.live_frame.view.viewport[1],
                renderer.live_frame.view.viewport[2],
                renderer.live_frame.view.viewport[3],
                (int)(renderer.live_frame.view.origin[0] * 1000.0f),
                (int)(renderer.live_frame.view.origin[1] * 1000.0f),
                (int)(renderer.live_frame.view.origin[2] * 1000.0f),
                (int)(renderer.live_frame.view.angles[0] * 1000.0f),
                (int)(renderer.live_frame.view.angles[1] * 1000.0f),
                (int)(renderer.live_frame.view.angles[2] * 1000.0f),
                renderer.live_frame.entity_count,
                renderer.live_frame.command_2d_count);
        struct live_texture_sync_context texture_sync = {
            .cache = &renderer.live_texture_cache,
            .prior_use_retired = renderer.live_consumed_frames == 0u ||
                                 renderer.last_completed_token != 0u,
            .cache_result = REF_AGC_GPU_TEXTURE_OK,
        };
        uint64_t next_texture_revision = renderer.live_texture_revision;
        const int texture_visit_result = PS5_RefAgcVisitTextures(
            renderer.live_texture_revision, sync_live_texture,
            &texture_sync, &next_texture_revision);
        if (texture_visit_result != REF_AGC_TEXTURE_OK ||
            ref_agc_gpu_texture_cache_validate(
                &renderer.live_texture_cache) != REF_AGC_GPU_TEXTURE_OK) {
            (void)ps5log_printf(PS5LOG_ERR,
                "REF_AGC_LIVE_TEXTURE_FAILURE serial=%llu after_revision=%llu "
                "visit_result=%d cache_result=%d prior_use_retired=%d",
                (unsigned long long)renderer.live_frame.serial,
                (unsigned long long)renderer.live_texture_revision,
                texture_visit_result, texture_sync.cache_result,
                texture_sync.prior_use_retired);
            park("live-texture-sync-or-ownership-failure");
        }
        const int texture_changed =
            next_texture_revision != renderer.live_texture_revision;
        if (texture_changed) {
            RefAgcGpuTextureStats texture_stats;
            if (ref_agc_gpu_texture_cache_stats(
                    &renderer.live_texture_cache, &texture_stats) !=
                REF_AGC_GPU_TEXTURE_OK)
                park("live-texture-stats-failure");
            (void)ps5log_printf(PS5LOG_MARK,
                "REF_AGC_LIVE_TEXTURE_SYNC serial=%llu revision=%llu "
                "creates=%llu updates=%llu deletes=%llu active=%u peak=%u "
                "resident_bytes=%llu peak_bytes=%llu source_bytes=%llu "
                "flushes=%llu descriptor_hash=%016llx "
                "arena_bytes=%u ownership=retired-before-reuse",
                (unsigned long long)renderer.live_frame.serial,
                (unsigned long long)next_texture_revision,
                (unsigned long long)texture_stats.creates,
                (unsigned long long)texture_stats.updates,
                (unsigned long long)texture_stats.deletes,
                texture_stats.active, texture_stats.peak_active,
                (unsigned long long)texture_stats.resident_bytes,
                (unsigned long long)texture_stats.peak_resident_bytes,
                (unsigned long long)texture_stats.source_bytes_copied,
                (unsigned long long)texture_stats.flushes,
                (unsigned long long)texture_stats.descriptor_hash,
                REF_AGC_GPU_TEXTURE_ARENA_BYTES);
            renderer.live_texture_revision = next_texture_revision;
        }
        struct live_world_sync_context world_sync = {
            .cache = &renderer.live_world_cache,
            .textures = &renderer.live_texture_cache,
            .prior_use_retired = renderer.live_consumed_frames == 0u ||
                                 renderer.last_completed_token != 0u,
            .cache_result = REF_AGC_GPU_WORLD_OK,
        };
        uint64_t next_world_revision = renderer.live_world_revision;
        const int world_visit_result = PS5_RefAgcVisitWorld(
            renderer.live_world_revision, sync_live_world,
            &world_sync, &next_world_revision);
        if (world_visit_result != REF_AGC_WORLD_OK ||
            ref_agc_gpu_world_cache_validate(
                &renderer.live_world_cache) != REF_AGC_GPU_WORLD_OK) {
            (void)ps5log_printf(PS5LOG_ERR,
                "REF_AGC_LIVE_WORLD_FAILURE serial=%llu "
                "after_revision=%llu visit_result=%d cache_result=%d "
                "prior_use_retired=%d texture_revision=%llu",
                (unsigned long long)renderer.live_frame.serial,
                (unsigned long long)renderer.live_world_revision,
                world_visit_result, world_sync.cache_result,
                world_sync.prior_use_retired,
                (unsigned long long)renderer.live_texture_revision);
            park("live-world-sync-or-ownership-failure");
        }
        if (texture_changed && renderer.live_world_revision != 0u &&
            next_world_revision == renderer.live_world_revision &&
            ref_agc_gpu_world_cache_refresh_textures(
                &renderer.live_world_cache, &renderer.live_texture_cache,
                world_sync.prior_use_retired) != REF_AGC_GPU_WORLD_OK)
            park("live-world-texture-refresh-failure");
        if (next_world_revision != renderer.live_world_revision) {
            RefAgcGpuWorldStats world_stats;
            if (ref_agc_gpu_world_cache_stats(
                    &renderer.live_world_cache, &world_stats) !=
                REF_AGC_GPU_WORLD_OK || !world_stats.active ||
                world_stats.draw_count == 0u ||
                world_stats.vertex_count == 0u ||
                world_stats.index_count == 0u)
                park("live-world-stats-failure");
            (void)ps5log_printf(PS5LOG_MARK,
                "REF_AGC_LIVE_WORLD_SYNC serial=%llu revision=%llu "
                "vertices=%u indices=%u draws=%u texture_tables=%u "
                "lightmapped_draws=%u lightmap=%ux%u row_pitch=%u "
                "lightmap_bytes=%llu lightmap_rgb_sum=%llu "
                "lightmap_nonzero_texels=%u lightmap_rgb_range=%u..%u "
                "resident_bytes=%llu peak_bytes=%llu source_hash=%016llx "
                "upload_hash=%016llx arena_bytes=%u index_mode=per-draw-u16 "
                "source_indices=u32 memory=direct "
                "ownership=retired-before-reuse",
                (unsigned long long)renderer.live_frame.serial,
                (unsigned long long)next_world_revision,
                world_stats.vertex_count, world_stats.index_count,
                world_stats.draw_count, world_stats.texture_tables,
                world_stats.lightmapped_draw_count,
                world_stats.lightmap_width, world_stats.lightmap_height,
                world_stats.lightmap_row_pitch,
                (unsigned long long)world_stats.lightmap_bytes,
                (unsigned long long)world_stats.lightmap_rgb_sum,
                world_stats.lightmap_nonzero_texels,
                world_stats.lightmap_rgb_min, world_stats.lightmap_rgb_max,
                (unsigned long long)world_stats.resident_bytes,
                (unsigned long long)world_stats.peak_resident_bytes,
                (unsigned long long)world_stats.source_hash,
                (unsigned long long)world_stats.upload_hash,
                REF_AGC_GPU_WORLD_ARENA_BYTES);
            renderer.live_world_revision = next_world_revision;
        }
        if (renderer.live_texture_revision != 0u &&
            ps5_cache_cpu_to_gpu_plan(
                resources.resource_heap, resources.resource_heap_bytes,
                resources.live_texture_arena,
                REF_AGC_GPU_TEXTURE_ARENA_BYTES,
                &renderer.live_texture_cache_plan) != 0)
            park("live-texture-acquire-plan-failure");
        if (renderer.live_world_revision != 0u &&
            ps5_cache_cpu_to_gpu_plan(
                resources.resource_heap, resources.resource_heap_bytes,
                resources.live_world_arena,
                REF_AGC_GPU_WORLD_ARENA_BYTES,
                &renderer.live_world_cache_plan) != 0)
            park("live-world-acquire-plan-failure");
        result = gears_frame_loop_step(&loop);
        if (result != 0) {
            GearsFrameRunnerResult failed_run = {0};
            const int result_rc = gears_frame_loop_result(
                &loop, &failed_run);
            (void)ps5log_printf(PS5LOG_ERR,
                "REF_AGC_LIVE_STEP_FAILURE serial=%llu step_result=%d "
                "result_rc=%d state=%d failed_frame=%llu callback=%d "
                "completed=%llu active_stage=%s",
                (unsigned long long)renderer.live_frame.serial, result,
                result_rc, failed_run.state,
                (unsigned long long)failed_run.failed_frame,
                failed_run.callback_result,
                (unsigned long long)failed_run.frames_completed,
                renderer.live_compose_stage ?
                    renderer.live_compose_stage : "unknown");
            park("live-frame-submit-or-ownership-failure");
        }
        result = gears_frame_loop_retire_oldest(&loop);
        if (result != 0) {
            GearsFrameRunnerResult failed_run = {0};
            const int result_rc = gears_frame_loop_result(
                &loop, &failed_run);
            (void)ps5log_printf(PS5LOG_ERR,
                "REF_AGC_LIVE_RETIRE_FAILURE serial=%llu result=%d "
                "result_rc=%d state=%d failed_frame=%llu callback=%d "
                "completed=%llu",
                (unsigned long long)renderer.live_frame.serial, result,
                result_rc, failed_run.state,
                (unsigned long long)failed_run.failed_frame,
                failed_run.callback_result,
                (unsigned long long)failed_run.frames_completed);
            park("live-frame-retire-or-ownership-failure");
        }
        if (ref_agc_live_world_view_ready(&renderer.live_frame)) {
            const float applied_camera[7] = {
                renderer.noclip.position[0], renderer.noclip.position[1],
                renderer.noclip.position[2], renderer.noclip.forward[0],
                renderer.noclip.forward[1], renderer.noclip.forward[2],
                renderer.live_aspect_ratio,
            };
            const uint64_t camera_hash = readback_hash(
                applied_camera, sizeof(applied_camera));
            if (renderer.live_camera_hash != 0u &&
                camera_hash != renderer.live_camera_hash)
                ++renderer.live_camera_changes;
            renderer.live_camera_hash = camera_hash;
        }
        renderer.live_last_serial = renderer.live_frame.serial;
        ++renderer.live_consumed_frames;
        if (PS5_RefAgcConsumeLiveFrame(renderer.live_last_serial) != 0)
            park("live-frame-ack-failure");
        if (renderer.live_consumed_frames == 1u ||
            renderer.live_consumed_frames % 600u == 0u)
            (void)ps5log_printf(PS5LOG_MARK,
                "REF_AGC_LIVE_CONSUMED serial=%llu consumed=%llu "
                "view_frames=%llu camera_hash=%016llx camera_changes=%llu "
                "map_serial=%llu entities=%u draw2d=%u "
                "ack=exact drops=zero",
                (unsigned long long)renderer.live_last_serial,
                (unsigned long long)renderer.live_consumed_frames,
                (unsigned long long)renderer.live_view_frames,
                (unsigned long long)renderer.live_camera_hash,
                (unsigned long long)renderer.live_camera_changes,
                (unsigned long long)renderer.live_frame.map_serial,
                renderer.live_frame.entity_count,
                renderer.live_frame.command_2d_count);
    }
    result = gears_frame_loop_drain(&loop);
    GearsFrameRunnerResult live_run = {0};
    if (result != 0 || gears_frame_loop_result(&loop, &live_run) != 0 ||
        live_run.state != GEARS_RUN_COMPLETE ||
        live_run.frames_completed != renderer.live_consumed_frames ||
        renderer.live_consumed_frames == 0u ||
        renderer.live_last_serial != renderer.live_consumed_frames ||
        live_run.telemetry.errors != 0u || !guards_intact())
        park("live-frame-drain-or-accounting-failure");

    const uint64_t live_retire_token = renderer.last_completed_token;
    uint32_t live_reclaimed = 0u;
#ifdef PS5_REF_AGC_LIVE_PHASE7
    RefAgcGpuTextureStats live_texture_stats;
    RefAgcGpuWorldStats live_world_stats;
    if (ref_agc_gpu_texture_cache_stats(
            &renderer.live_texture_cache, &live_texture_stats) !=
            REF_AGC_GPU_TEXTURE_OK ||
        ref_agc_gpu_texture_cache_validate(
            &renderer.live_texture_cache) != REF_AGC_GPU_TEXTURE_OK ||
        live_texture_stats.revision == 0u ||
        live_texture_stats.creates == 0u ||
        live_texture_stats.active == 0u ||
        live_texture_stats.source_bytes_copied == 0u ||
        live_texture_stats.descriptor_hash == 0u)
        park("live-texture-final-accounting-failure");
    if (ref_agc_gpu_world_cache_stats(
            &renderer.live_world_cache, &live_world_stats) !=
            REF_AGC_GPU_WORLD_OK ||
        ref_agc_gpu_world_cache_validate(
            &renderer.live_world_cache) != REF_AGC_GPU_WORLD_OK ||
        live_world_stats.revision == 0u ||
        live_world_stats.publishes == 0u ||
        !live_world_stats.active || live_world_stats.vertex_count == 0u ||
        live_world_stats.index_count == 0u ||
        live_world_stats.draw_count == 0u ||
        live_world_stats.texture_tables != live_world_stats.draw_count ||
        live_world_stats.lightmapped_draw_count == 0u ||
        live_world_stats.lightmap_width == 0u ||
        live_world_stats.lightmap_height == 0u ||
        live_world_stats.lightmap_row_pitch == 0u ||
        live_world_stats.lightmap_bytes == 0u ||
        live_world_stats.lightmap_rgb_sum == 0u ||
        live_world_stats.lightmap_nonzero_texels == 0u ||
        live_world_stats.lightmap_rgb_max == 0u ||
        live_world_stats.upload_hash == 0u)
        park("live-world-final-accounting-failure");
#endif
#ifdef PS5_TEXTURE_PATH
    if (ps5_resource_pool_release_deferred(
            &resources.resource_pool,
            &resources.dynamic_lightmap_allocations[0],
            live_retire_token) != PS5_RESOURCE_POOL_OK ||
        ps5_resource_pool_release_deferred(
            &resources.resource_pool,
            &resources.dynamic_lightmap_allocations[1],
            live_retire_token) != PS5_RESOURCE_POOL_OK)
        park("live-dynamic-lightmap-retirement-failure");
#endif
#ifdef PS5_REF_AGC_LIVE_PHASE7
    if (ps5_resource_pool_release_deferred(
            &resources.resource_pool, &resources.live_world_allocation,
            live_retire_token) != PS5_RESOURCE_POOL_OK)
        park("live-world-arena-retirement-failure");
    if (ps5_resource_pool_release_deferred(
            &resources.resource_pool, &resources.live_texture_allocation,
            live_retire_token) != PS5_RESOURCE_POOL_OK)
        park("live-texture-arena-retirement-failure");
#endif
    if (ps5_resource_pool_release_deferred(
            &resources.resource_pool, &resources.bsp_allocation,
            live_retire_token) != PS5_RESOURCE_POOL_OK ||
        ps5_resource_pool_release_deferred(
            &resources.resource_pool, &resources.shader_allocation,
            live_retire_token) != PS5_RESOURCE_POOL_OK ||
        ps5_resource_pool_release_deferred(
            &resources.resource_pool, &resources.depth_allocation,
            live_retire_token) != PS5_RESOURCE_POOL_OK ||
        ps5_resource_pool_release_deferred(
            &resources.resource_pool, &resources.transient_allocation,
            live_retire_token) != PS5_RESOURCE_POOL_OK ||
        ps5_resource_pool_reclaim(
            &resources.resource_pool, live_retire_token, 1,
            &live_reclaimed) != PS5_RESOURCE_POOL_OK ||
        live_reclaimed != 8u)
        park("live-resource-pool-retirement-failure");

    const size_t live_readback_bytes = resources.surface.tiled_footprint;
    uint8_t *const live_first = resources.framebuffer;
    uint8_t *const live_second = (uint8_t *)resources.framebuffer +
        PS5_SURFACE_BUFFER_STRIDE;
    ps5_native_cache_flush(live_first, live_readback_bytes);
    ps5_native_cache_flush(live_second, live_readback_bytes);
    const uint64_t live_first_hash = readback_hash(
        live_first, live_readback_bytes);
    const uint64_t live_second_hash = readback_hash(
        live_second, live_readback_bytes);
    const uint64_t live_buffer_hashes[2] = {
        live_first_hash, live_second_hash,
    };
    const uint64_t live_frame_hash = readback_hash(
        live_buffer_hashes, sizeof(live_buffer_hashes));
    const uint64_t live_first_bright = bright_pixel_count(
        live_first, live_readback_bytes);
    const uint64_t live_second_bright = bright_pixel_count(
        live_second, live_readback_bytes);
    if (live_first_hash == 0u || live_second_hash == 0u ||
        live_first_bright == 0u || live_second_bright == 0u)
        park("live-readback-visibility-failure");
    (void)ps5log_printf(PS5LOG_MARK,
        "REF_AGC_GPU_WORLD_COMPLETE revision=%llu publishes=%llu "
        "clears=%llu vertices=%u indices=%u draws=%u texture_tables=%u "
        "lightmapped_draws=%u lightmap=%ux%u row_pitch=%u "
        "lightmap_bytes=%llu lightmap_rgb_sum=%llu "
        "lightmap_nonzero_texels=%u lightmap_rgb_range=%u..%u "
        "resident_bytes=%llu peak_bytes=%llu source_hash=%016llx "
        "upload_hash=%016llx flushes=%llu arena_bytes=%u "
        "geometry=live-refapi textures=live-refapi memory=direct "
        "source_indices=u32 gpu_indices=per-draw-u16 "
        "ownership=fence+videoout-before-reuse errors=0",
        (unsigned long long)live_world_stats.revision,
        (unsigned long long)live_world_stats.publishes,
        (unsigned long long)live_world_stats.clears,
        live_world_stats.vertex_count, live_world_stats.index_count,
        live_world_stats.draw_count, live_world_stats.texture_tables,
        live_world_stats.lightmapped_draw_count,
        live_world_stats.lightmap_width, live_world_stats.lightmap_height,
        live_world_stats.lightmap_row_pitch,
        (unsigned long long)live_world_stats.lightmap_bytes,
        (unsigned long long)live_world_stats.lightmap_rgb_sum,
        live_world_stats.lightmap_nonzero_texels,
        live_world_stats.lightmap_rgb_min, live_world_stats.lightmap_rgb_max,
        (unsigned long long)live_world_stats.resident_bytes,
        (unsigned long long)live_world_stats.peak_resident_bytes,
        (unsigned long long)live_world_stats.source_hash,
        (unsigned long long)live_world_stats.upload_hash,
        (unsigned long long)live_world_stats.flushes,
        REF_AGC_GPU_WORLD_ARENA_BYTES);
    ref_agc_gpu_world_cache_destroy(&renderer.live_world_cache);
    (void)ps5log_printf(PS5LOG_MARK,
        "REF_AGC_GPU_TEXTURE_COMPLETE revision=%llu creates=%llu "
        "updates=%llu deletes=%llu active=%u peak=%u resident_bytes=%llu "
        "peak_bytes=%llu source_bytes=%llu flushes=%llu "
        "descriptor_hash=%016llx arena_bytes=%u descriptors=rgba8+bilinear "
        "memory=direct ownership=fence+videoout-before-reuse errors=0",
        (unsigned long long)live_texture_stats.revision,
        (unsigned long long)live_texture_stats.creates,
        (unsigned long long)live_texture_stats.updates,
        (unsigned long long)live_texture_stats.deletes,
        live_texture_stats.active, live_texture_stats.peak_active,
        (unsigned long long)live_texture_stats.resident_bytes,
        (unsigned long long)live_texture_stats.peak_resident_bytes,
        (unsigned long long)live_texture_stats.source_bytes_copied,
        (unsigned long long)live_texture_stats.flushes,
        (unsigned long long)live_texture_stats.descriptor_hash,
        REF_AGC_GPU_TEXTURE_ARENA_BYTES);
    ref_agc_gpu_texture_cache_destroy(&renderer.live_texture_cache);
    (void)ps5log_printf(PS5LOG_MARK,
        "REF_AGC_LIVE_COMPLETE frames=%llu serial=%llu view_frames=%llu "
        "camera_hash=%016llx camera_changes=%llu "
        "buffer0=%016llx buffer1=%016llx frame_hash=%016llx "
        "bright_pixels=%llu "
        "resource_reclaimed=%u ownership=fence+videoout+ack "
        "guards=intact errors=0",
        (unsigned long long)live_run.frames_completed,
        (unsigned long long)renderer.live_last_serial,
        (unsigned long long)renderer.live_view_frames,
        (unsigned long long)renderer.live_camera_hash,
        (unsigned long long)renderer.live_camera_changes,
        (unsigned long long)live_first_hash,
        (unsigned long long)live_second_hash,
        (unsigned long long)live_frame_hash,
        (unsigned long long)(live_first_bright + live_second_bright),
        live_reclaimed);
    const int live_cleanup_result = cleanup();
    (void)ps5log_printf(live_cleanup_result == 0 ? PS5LOG_MARK : PS5LOG_ERR,
        "REF_AGC_TEARDOWN videoout=closed direct_memory=released "
        "agc=unloaded result=%d ownership=%s",
        live_cleanup_result,
        live_cleanup_result == 0 ? "exact" : "retained");
    PS5_RefAgcRuntimeLiveStats(
        renderer.live_consumed_frames, renderer.live_last_serial,
        renderer.live_view_frames, renderer.live_camera_hash,
        renderer.live_camera_changes);
    PS5_RefAgcRuntimeComplete(
        live_run.frames_completed, live_frame_hash,
        live_first_bright + live_second_bright, live_cleanup_result);
    ps5log_close(live_cleanup_result == 0 ? "ref-agc-live-complete" :
                                           "ref-agc-live-teardown-failure");
    return live_cleanup_result;
#else
    for (unsigned frame = 0; frame < BSP_GATE_FRAME_COUNT; ++frame) {
        result = gears_frame_loop_step(&loop);
        if (result != 0)
            park("bsp-submit-or-ownership-failure");
        if ((frame + 1u) % 600u == 0u && !guards_intact())
            park("guard-corruption");
    }
    result = gears_frame_loop_drain(&loop);
    GearsFrameRunnerResult run = {0};
    if (result != 0 || gears_frame_loop_result(&loop, &run) != 0 ||
        run.state != GEARS_RUN_COMPLETE ||
        run.frames_completed != BSP_GATE_FRAME_COUNT)
        park("bsp-drain-or-ownership-failure");
    if (!guards_intact())
        park("guard-corruption");
#ifdef PS5_GPU_FLIP_TIMING_GATE
    Ps5GpuFlipTimingSummary gpu_flip_summary;
    if (ps5_gpu_flip_timing_finish(&renderer.gpu_flip_timing,
                                   &gpu_flip_summary) != 0)
        park("gpu-flip-timing-gap-or-order-failure");
    (void)ps5log_printf(PS5LOG_MARK,
        "GPU_FLIP_TIMING_SUMMARY schema=1 frames=%llu "
        "records=%llu gpu_timestamp_writes=%llu "
        "gpu_timestamp_changes=%llu gpu_timestamp_regressions=0 "
        "cpu_order_errors=0 sequence_gaps=0 "
        "first_gpu_eop_ticks=%llu last_gpu_eop_ticks=%llu "
        "submit_to_gpu_min_ns=%llu submit_to_gpu_avg_ns=%llu "
        "submit_to_gpu_max_ns=%llu submit_to_flip_min_ns=%llu "
        "submit_to_flip_avg_ns=%llu submit_to_flip_max_ns=%llu "
        "gpu_to_flip_min_ns=%llu gpu_to_flip_avg_ns=%llu "
        "gpu_to_flip_max_ns=%llu ownership=fence+exact-videoout-event "
        "result=pass",
        (unsigned long long)gpu_flip_summary.frames,
        (unsigned long long)gpu_flip_summary.frames,
        (unsigned long long)gpu_flip_summary.gpu_timestamp_writes,
        (unsigned long long)gpu_flip_summary.gpu_timestamp_changes,
        (unsigned long long)gpu_flip_summary.first_gpu_eop_ticks,
        (unsigned long long)gpu_flip_summary.last_gpu_eop_ticks,
        (unsigned long long)gpu_flip_summary.submit_to_gpu_min_ns,
        (unsigned long long)gpu_flip_summary.submit_to_gpu_avg_ns,
        (unsigned long long)gpu_flip_summary.submit_to_gpu_max_ns,
        (unsigned long long)gpu_flip_summary.submit_to_flip_min_ns,
        (unsigned long long)gpu_flip_summary.submit_to_flip_avg_ns,
        (unsigned long long)gpu_flip_summary.submit_to_flip_max_ns,
        (unsigned long long)gpu_flip_summary.gpu_to_flip_min_ns,
        (unsigned long long)gpu_flip_summary.gpu_to_flip_avg_ns,
        (unsigned long long)gpu_flip_summary.gpu_to_flip_max_ns);
#endif
#ifdef PS5_RESOURCE_FOUNDATION
    int resource_ring_reusable = renderer.last_completed_token != 0u;
    for (uint32_t slot = 0u; slot < 2u; ++slot) {
        if (ps5_transient_ring_begin(
                &renderer.transient_ring, slot,
                renderer.completed_tokens[slot],
                renderer.completed_tokens[slot] != 0u) !=
                PS5_TRANSIENT_OK ||
            ps5_transient_ring_abort_unsubmitted(
                &renderer.transient_ring, slot) != PS5_TRANSIENT_OK)
            resource_ring_reusable = 0;
    }
    (void)ps5log_printf(resource_ring_reusable ? PS5LOG_MARK : PS5LOG_ERR,
        "RESOURCE_RING_RETIRED slots=2 reusable=%s tokens=exact "
        "last_token=%llu",
        resource_ring_reusable ? "true" : "false",
        (unsigned long long)renderer.last_completed_token);
    if (!resource_ring_reusable)
        park("resource-ring-retirement-gate-failure");
#endif
    const size_t readback_bytes = resources.surface.tiled_footprint;
    uint8_t *const first = resources.framebuffer;
    uint8_t *const second = (uint8_t *)resources.framebuffer +
        PS5_SURFACE_BUFFER_STRIDE;
    ps5_native_cache_flush(first, readback_bytes);
    ps5_native_cache_flush(second, readback_bytes);
    const uint64_t first_hash = readback_hash(first, readback_bytes);
    const uint64_t second_hash = readback_hash(second, readback_bytes);
    const uint64_t first_bright = bright_pixel_count(first, readback_bytes);
    const uint64_t second_bright = bright_pixel_count(second, readback_bytes);
#ifdef PS5_BSP_NOCLIP
#ifdef PS5_RESOURCE_FOUNDATION
    (void)ps5log_printf(PS5LOG_MARK,
        "BSP_RESOURCE_READBACK buffer0=%016llx buffer1=%016llx bytes=%llu "
        "bright_pixels0=%llu bright_pixels1=%llu guards=intact "
        "frames=%llu errors=%llu overlay=transient",
        (unsigned long long)first_hash, (unsigned long long)second_hash,
        (unsigned long long)readback_bytes,
        (unsigned long long)first_bright,
        (unsigned long long)second_bright,
        (unsigned long long)run.frames_completed,
        (unsigned long long)run.telemetry.errors);
#elif defined(PS5_BSP_TEXTURED)
    (void)ps5log_printf(PS5LOG_MARK,
        "BSP_TEXTURED_READBACK buffer0=%016llx buffer1=%016llx bytes=%llu "
        "bright_pixels0=%llu bright_pixels1=%llu guards=intact "
        "frames=%llu errors=%llu",
        (unsigned long long)first_hash, (unsigned long long)second_hash,
        (unsigned long long)readback_bytes,
        (unsigned long long)first_bright,
        (unsigned long long)second_bright,
        (unsigned long long)run.frames_completed,
        (unsigned long long)run.telemetry.errors);
#else
    (void)ps5log_printf(PS5LOG_MARK,
        "BSP_NOCLIP_READBACK buffer0=%016llx buffer1=%016llx bytes=%llu "
        "bright_pixels0=%llu bright_pixels1=%llu guards=intact "
        "frames=%llu errors=%llu",
        (unsigned long long)first_hash, (unsigned long long)second_hash,
        (unsigned long long)readback_bytes,
        (unsigned long long)first_bright,
        (unsigned long long)second_bright,
        (unsigned long long)run.frames_completed,
        (unsigned long long)run.telemetry.errors);
#endif
#if !defined(PS5_TEXTURE_ACCOUNTING_ENABLED) && \
    !defined(PS5_GOLDSRC_PHASE4)
    const int input_continuity_valid =
        run.telemetry.errors == 0u &&
        run.telemetry.present_interval_over_budget == 0u &&
        renderer.pad_read_errors == 0u &&
        renderer.noclip.sampled_frames == BSP_GATE_FRAME_COUNT &&
        renderer.noclip.connected_frames == BSP_GATE_FRAME_COUNT;
#endif
#ifdef PS5_RESOURCE_FOUNDATION
    const int resource_valid =
#ifdef PS5_GOLDSRC_PHASE4
        run.telemetry.errors == 0u &&
        run.frames_completed == BSP_GATE_FRAME_COUNT &&
        renderer.pad_read_errors == 0u &&
        renderer.noclip.sampled_frames == BSP_GATE_FRAME_COUNT &&
#elif defined(PS5_TEXTURE_ACCOUNTING_ENABLED)
        run.telemetry.errors == 0u &&
        run.telemetry.present_interval_over_budget == 0u &&
        renderer.pad_read_errors == 0u &&
        renderer.noclip.sampled_frames == BSP_GATE_FRAME_COUNT &&
#else
        input_continuity_valid &&
#endif
        first_bright != 0u && second_bright != 0u &&
        renderer.bsp_plan.descriptor_table_dwords ==
            renderer.bsp_bundle.texture_count * BSP_TEXTURE_TABLE_DWORDS &&
        renderer.resource_frames[0].transient_bytes != 0u &&
        renderer.resource_frames[1].transient_bytes != 0u &&
        renderer.last_completed_token != 0u;
#ifdef PS5_TEXTURE_PATH
    const int dynamic_lightmap_valid = resource_valid &&
#ifndef PS5_GOLDSRC_LIGHTING_GATE
        first_hash != second_hash &&
#endif
        bsp_dynamic_lightmap_guards_intact(
            &renderer.dynamic_lightmap_slots[0],
            &renderer.dynamic_lightmap_layout) &&
        bsp_dynamic_lightmap_guards_intact(
            &renderer.dynamic_lightmap_slots[1],
            &renderer.dynamic_lightmap_layout) &&
        bsp_dynamic_lightmap_surrounding_hash(
            renderer.dynamic_lightmap_slots[0].pixels,
            &renderer.dynamic_lightmap_layout) ==
            renderer.dynamic_lightmap_slots[0].surrounding_hash &&
        bsp_dynamic_lightmap_surrounding_hash(
            renderer.dynamic_lightmap_slots[1].pixels,
            &renderer.dynamic_lightmap_layout) ==
            renderer.dynamic_lightmap_slots[1].surrounding_hash &&
#if defined(PS5_GOLDSRC_LIGHTING_GATE) || \
    defined(PS5_GOLDSRC_VISIBILITY_GATE)
#ifdef PS5_GOLDSRC_LIGHTING_GATE
        renderer.dynamic_lightmap_slots[0].last_pattern ==
            goldsrc_lighting_mode(
                PS5_REF_AGC_FEATURE_FRAME(BSP_GATE_FRAME_COUNT - 2u)) &&
        renderer.dynamic_lightmap_slots[1].last_pattern ==
            goldsrc_lighting_mode(
                PS5_REF_AGC_FEATURE_FRAME(BSP_GATE_FRAME_COUNT - 1u)) &&
#else
        renderer.dynamic_lightmap_slots[0].last_pattern == 0u &&
        renderer.dynamic_lightmap_slots[1].last_pattern == 0u &&
#endif
        bsp_dynamic_lightmap_patch_hash(
            renderer.dynamic_lightmap_slots[0].pixels,
            &renderer.dynamic_lightmap_layout) ==
        bsp_dynamic_lightmap_patch_hash(
            renderer.dynamic_lightmap_slots[1].pixels,
            &renderer.dynamic_lightmap_layout) &&
#elif defined(PS5_TEXTURE_MIP_GATE) || defined(PS5_TEXTURE_ALPHA_GATE) || \
    defined(PS5_TEXTURE_SKY_GATE)
        renderer.dynamic_lightmap_slots[0].last_pattern == 1u &&
        renderer.dynamic_lightmap_slots[1].last_pattern == 1u &&
        bsp_dynamic_lightmap_patch_hash(
            renderer.dynamic_lightmap_slots[0].pixels,
            &renderer.dynamic_lightmap_layout) ==
        bsp_dynamic_lightmap_patch_hash(
            renderer.dynamic_lightmap_slots[1].pixels,
            &renderer.dynamic_lightmap_layout) &&
#else
        renderer.dynamic_lightmap_slots[0].last_pattern == 0u &&
        renderer.dynamic_lightmap_slots[1].last_pattern == 1u &&
#endif
        renderer.dynamic_lightmap_slots[0].last_frame ==
            BSP_GATE_FRAME_COUNT - 2u &&
        renderer.dynamic_lightmap_slots[1].last_frame ==
            BSP_GATE_FRAME_COUNT - 1u;
    if (!dynamic_lightmap_valid)
        park("dynamic-lightmap-readback-or-guard-gate-failure");
#ifdef PS5_GOLDSRC_LIGHTING_GATE
    (void)ps5log_printf(PS5LOG_MARK,
        "DYNAMIC_LIGHTMAP_READBACK slot0=%016llx slot1=%016llx "
        "final_mode=%s slots_equal=true surrounding=stable "
        "guards=intact frames=%llu",
        (unsigned long long)bsp_dynamic_lightmap_patch_hash(
            renderer.dynamic_lightmap_slots[0].pixels,
            &renderer.dynamic_lightmap_layout),
        (unsigned long long)bsp_dynamic_lightmap_patch_hash(
            renderer.dynamic_lightmap_slots[1].pixels,
            &renderer.dynamic_lightmap_layout),
        goldsrc_lighting_mode_name(
            goldsrc_lighting_mode(
                PS5_REF_AGC_FEATURE_FRAME(BSP_GATE_FRAME_COUNT - 1u))),
        (unsigned long long)run.frames_completed);
#elif defined(PS5_GOLDSRC_VISIBILITY_GATE)
    (void)ps5log_printf(PS5LOG_MARK,
        "DYNAMIC_LIGHTMAP_READBACK slot0=%016llx slot1=%016llx "
        "final_pattern=0 slots_equal=true surrounding=stable "
        "guards=intact frames=%llu",
        (unsigned long long)bsp_dynamic_lightmap_patch_hash(
            renderer.dynamic_lightmap_slots[0].pixels,
            &renderer.dynamic_lightmap_layout),
        (unsigned long long)bsp_dynamic_lightmap_patch_hash(
            renderer.dynamic_lightmap_slots[1].pixels,
            &renderer.dynamic_lightmap_layout),
        (unsigned long long)run.frames_completed);
#elif defined(PS5_TEXTURE_MIP_GATE) || defined(PS5_TEXTURE_ALPHA_GATE) || \
    defined(PS5_TEXTURE_SKY_GATE)
    (void)ps5log_printf(PS5LOG_MARK,
        "DYNAMIC_LIGHTMAP_READBACK slot0=%016llx slot1=%016llx "
        "final_pattern=1 slots_equal=true surrounding=stable "
        "guards=intact frames=%llu",
        (unsigned long long)bsp_dynamic_lightmap_patch_hash(
            renderer.dynamic_lightmap_slots[0].pixels,
            &renderer.dynamic_lightmap_layout),
        (unsigned long long)bsp_dynamic_lightmap_patch_hash(
            renderer.dynamic_lightmap_slots[1].pixels,
            &renderer.dynamic_lightmap_layout),
        (unsigned long long)run.frames_completed);
#else
    (void)ps5log_printf(PS5LOG_MARK,
        "DYNAMIC_LIGHTMAP_READBACK pattern0=%016llx pattern1=%016llx "
        "gpu_buffer0=%016llx gpu_buffer1=%016llx buffers_distinct=true "
        "surrounding=stable guards=intact frames=%llu",
        (unsigned long long)bsp_dynamic_lightmap_patch_hash(
            renderer.dynamic_lightmap_slots[0].pixels,
            &renderer.dynamic_lightmap_layout),
        (unsigned long long)bsp_dynamic_lightmap_patch_hash(
            renderer.dynamic_lightmap_slots[1].pixels,
            &renderer.dynamic_lightmap_layout),
        (unsigned long long)first_hash,
        (unsigned long long)second_hash,
        (unsigned long long)run.frames_completed);
#endif
#ifdef PS5_GOLDSRC_PHASE4
    const GoldSrcRenderState proved_opaque_state = {
        GOLDSRC_BLEND_OPAQUE, GOLDSRC_CULL_NONE, 1u, 0u, 1u, 0u,
    };
    const GoldSrcRenderState proved_alpha_state = {
        GOLDSRC_BLEND_ALPHA_TEST, GOLDSRC_CULL_NONE, 1u, 0u, 1u, 0u,
    };
    const GoldSrcPipelinePermutation *const proved_opaque =
        goldsrc_pipeline_cache_find(
            &renderer.goldsrc_pipeline_cache, &proved_opaque_state);
    const GoldSrcPipelinePermutation *const proved_alpha =
        goldsrc_pipeline_cache_find(
            &renderer.goldsrc_pipeline_cache, &proved_alpha_state);
    if (!proved_opaque || !proved_alpha)
        park("goldsrc-state-cache-final-gate-failure");
    (void)ps5log_printf(PS5LOG_MARK,
        "GOLDSRC_PIPELINE_GATE_COMPLETE schema=1 frames=%llu "
        "semantic_permutations=%u shader_variants=%u "
        "opaque_key=%u opaque_shader=%s alpha_key=%u alpha_shader=%s "
        "framebuffer_distinct=true input_required=false "
        "tokens=exact guards=intact errors=%llu",
        (unsigned long long)run.frames_completed,
        renderer.goldsrc_pipeline_cache.count,
        GOLDSRC_SHADER_VARIANT_COUNT, proved_opaque->key,
        goldsrc_pipeline_shader_variant_name(proved_opaque->shader),
        proved_alpha->key,
        goldsrc_pipeline_shader_variant_name(proved_alpha->shader),
        (unsigned long long)run.telemetry.errors);
#ifdef PS5_GOLDSRC_STATE_MATRIX_GATE
    int matrix_valid = 1;
    for (uint32_t matrix_index = 0u;
         matrix_index < GOLDSRC_STATE_MATRIX_CASE_COUNT; ++matrix_index)
        for (uint32_t slot = 0u; slot < 2u; ++slot)
            if (!renderer.goldsrc_matrix_seen[matrix_index][slot] ||
                renderer.goldsrc_matrix_hashes[matrix_index][slot] == 0u ||
                renderer.goldsrc_matrix_bright[matrix_index][slot] == 0u ||
                (matrix_index != 0u &&
                 renderer.goldsrc_matrix_hashes[matrix_index][slot] ==
                     renderer.goldsrc_matrix_hashes[0][slot]))
                matrix_valid = 0;
    if (!matrix_valid)
        park("goldsrc-state-matrix-readback-gate-failure");
    (void)ps5log_printf(PS5LOG_MARK,
        "GOLDSRC_STATE_MATRIX_COMPLETE schema=1 frames=%llu cases=%u "
        "readbacks=%u both_slots=true control_pairs=distinct "
        "coverage=opaque+alpha+additive+alpha-test+depth-write+"
        "cull-front-back-none+fog+lightmap tokens=exact guards=intact "
        "errors=%llu",
        (unsigned long long)run.frames_completed,
        GOLDSRC_STATE_MATRIX_CASE_COUNT,
        GOLDSRC_STATE_MATRIX_CASE_COUNT * 2u,
        (unsigned long long)run.telemetry.errors);
#endif
#if defined(PS5_GOLDSRC_LIGHTING_GATE) && \
    !defined(PS5_GOLDSRC_PHASE4_FINAL_GATE)
    int lighting_valid = 1;
    for (uint32_t mode = 0u; mode < GOLDSRC_LIGHTING_MODE_COUNT; ++mode)
        for (uint32_t slot = 0u; slot < 2u; ++slot)
            if (!renderer.goldsrc_lighting_seen[mode][slot] ||
                renderer.goldsrc_lighting_hashes[mode][slot] == 0u ||
                renderer.goldsrc_lighting_bright[mode][slot] == 0u ||
                (mode != GOLDSRC_LIGHTING_MODE_BASE &&
                 renderer.goldsrc_lighting_hashes[mode][slot] ==
                     renderer.goldsrc_lighting_hashes
                         [GOLDSRC_LIGHTING_MODE_BASE][slot]))
                lighting_valid = 0;
    if (!lighting_valid)
        park("goldsrc-lighting-readback-gate-failure");
    (void)ps5log_printf(PS5LOG_MARK,
        "GOLDSRC_LIGHTING_COMPLETE schema=1 frames=%llu modes=%u "
        "readbacks=%u both_slots=true control_pairs=distinct "
        "lightstyles=real-bsp-planes animated_hz=10 "
        "dynamic_lights=face-local-radial atlas_upload=phase3-bounded "
        "face=%u styles=%u styled_faces=%u styled_layers=%u "
        "tokens=exact guards=intact errors=%llu",
        (unsigned long long)run.frames_completed,
        GOLDSRC_LIGHTING_MODE_COUNT,
        GOLDSRC_LIGHTING_MODE_COUNT * 2u,
        renderer.goldsrc_lighting_plan.layout.hit_face,
        renderer.goldsrc_lighting_plan.style_count,
        renderer.goldsrc_lighting_plan.styled_face_count,
        renderer.goldsrc_lighting_plan.styled_layer_count,
        (unsigned long long)run.telemetry.errors);
#endif
#if defined(PS5_GOLDSRC_SPRITE_PARTICLE_GATE) && \
    !defined(PS5_GOLDSRC_PHASE4_FINAL_GATE)
    int effects_valid = 1;
    for (uint32_t mode = 0u; mode < GOLDSRC_EFFECT_MODE_COUNT; ++mode)
        for (uint32_t slot = 0u; slot < 2u; ++slot)
            if (!renderer.goldsrc_effect_seen[mode][slot] ||
                renderer.goldsrc_effect_hashes[mode][slot] == 0u ||
                renderer.goldsrc_effect_bright[mode][slot] == 0u ||
                (mode != GOLDSRC_EFFECT_MODE_CONTROL &&
                 renderer.goldsrc_effect_hashes[mode][slot] ==
                     renderer.goldsrc_effect_hashes
                         [GOLDSRC_EFFECT_MODE_CONTROL][slot]))
                effects_valid = 0;
    for (uint32_t slot = 0u; slot < 2u; ++slot)
        if (renderer.goldsrc_effect_hashes
                [GOLDSRC_EFFECT_MODE_COMBINED][slot] ==
                renderer.goldsrc_effect_hashes
                    [GOLDSRC_EFFECT_MODE_SPRITE][slot] ||
            renderer.goldsrc_effect_hashes
                [GOLDSRC_EFFECT_MODE_COMBINED][slot] ==
                renderer.goldsrc_effect_hashes
                    [GOLDSRC_EFFECT_MODE_PARTICLES][slot])
            effects_valid = 0;
    if (!effects_valid)
        park("goldsrc-sprite-particle-readback-gate-failure");
    (void)ps5log_printf(PS5LOG_MARK,
        "GOLDSRC_SPRITE_PARTICLE_COMPLETE schema=1 frames=%llu "
        "modes=%u readbacks=%u both_slots=true control_pairs=distinct "
        "combined_pairs=distinct sprite_quads=%u alpha_particles=%u "
        "additive_particles=%u batches=alpha+additive "
        "geometry=per-frame-transient camera=locked-proof-view "
        "input_dependency=none tokens=exact guards=intact errors=%llu",
        (unsigned long long)run.frames_completed,
        GOLDSRC_EFFECT_MODE_COUNT, GOLDSRC_EFFECT_MODE_COUNT * 2u,
        GOLDSRC_EFFECT_SPRITE_QUADS,
        GOLDSRC_EFFECT_ALPHA_PARTICLE_QUADS,
        GOLDSRC_EFFECT_ADDITIVE_PARTICLE_QUADS,
        (unsigned long long)run.telemetry.errors);
#endif
#if defined(PS5_GOLDSRC_VISIBILITY_GATE) && \
    !defined(PS5_GOLDSRC_PHASE4_FINAL_GATE)
    int visibility_valid = 1;
    uint64_t visibility_bright_delta_max = 0u;
    for (uint32_t mode = 0u; mode < GOLDSRC_VISIBILITY_MODE_COUNT; ++mode) {
        if (!renderer.goldsrc_visibility_counts_seen[mode] ||
            renderer.goldsrc_visibility_draw_counts[mode] == 0u)
            visibility_valid = 0;
        for (uint32_t slot = 0u; slot < 2u; ++slot) {
            if (!renderer.goldsrc_visibility_seen[mode][slot] ||
                renderer.goldsrc_visibility_hashes[mode][slot] == 0u ||
                renderer.goldsrc_visibility_bright[mode][slot] == 0u)
                visibility_valid = 0;
            if (mode != GOLDSRC_VISIBILITY_MODE_CONTROL) {
                const uint64_t control_bright =
                    renderer.goldsrc_visibility_bright
                        [GOLDSRC_VISIBILITY_MODE_CONTROL][slot];
                const uint64_t mode_bright =
                    renderer.goldsrc_visibility_bright[mode][slot];
                const uint64_t delta = mode_bright > control_bright
                    ? mode_bright - control_bright
                    : control_bright - mode_bright;
                if (delta > visibility_bright_delta_max)
                    visibility_bright_delta_max = delta;
            }
        }
    }
    const uint32_t visibility_control =
        renderer.goldsrc_visibility_draw_counts
            [GOLDSRC_VISIBILITY_MODE_CONTROL];
    const uint32_t visibility_pvs =
        renderer.goldsrc_visibility_draw_counts[GOLDSRC_VISIBILITY_MODE_PVS];
    const uint32_t visibility_frustum =
        renderer.goldsrc_visibility_draw_counts
            [GOLDSRC_VISIBILITY_MODE_FRUSTUM];
    const uint32_t visibility_combined =
        renderer.goldsrc_visibility_draw_counts
            [GOLDSRC_VISIBILITY_MODE_COMBINED];
    if (visibility_pvs >= visibility_control ||
        visibility_frustum >= visibility_control ||
        visibility_combined > visibility_pvs ||
        visibility_combined > visibility_frustum ||
        visibility_bright_delta_max > 64u)
        visibility_valid = 0;
    if (!visibility_valid)
        park("goldsrc-visibility-readback-or-count-gate-failure");
    (void)ps5log_printf(PS5LOG_MARK,
        "GOLDSRC_VISIBILITY_COMPLETE schema=1 frames=%llu modes=%u "
        "readbacks=%u both_slots=true framebuffer_stable=true "
        "bright_delta_max=%llu bright_tolerance=64 "
        "draws=%u,%u,%u,%u reductions=true pvs=real-leaf-rows "
        "frustum=draw-aabb combined=intersection world_model_only=true "
        "camera=locked-bsp-tree input_dependency=none tokens=exact "
        "guards=intact errors=%llu",
        (unsigned long long)run.frames_completed,
        GOLDSRC_VISIBILITY_MODE_COUNT,
        GOLDSRC_VISIBILITY_MODE_COUNT * 2u,
        (unsigned long long)visibility_bright_delta_max,
        visibility_control, visibility_pvs, visibility_frustum,
        visibility_combined, (unsigned long long)run.telemetry.errors);
#endif
#if defined(PS5_GOLDSRC_BRUSH_GATE) && \
    !defined(PS5_GOLDSRC_PHASE4_FINAL_GATE)
    int brush_valid =
        renderer.goldsrc_brush_first_transform_hash != 0u &&
        renderer.goldsrc_brush_last_transform_hash != 0u &&
        renderer.goldsrc_brush_first_transform_hash !=
            renderer.goldsrc_brush_last_transform_hash;
    for (uint32_t mode = 0u; mode < GOLDSRC_BRUSH_MODE_COUNT; ++mode)
        for (uint32_t slot = 0u; slot < 2u; ++slot)
            if (!renderer.goldsrc_brush_seen[mode][slot] ||
                renderer.goldsrc_brush_hashes[mode][slot] == 0u ||
                renderer.goldsrc_brush_bright[mode][slot] == 0u ||
                (mode != GOLDSRC_BRUSH_MODE_CONTROL &&
                 renderer.goldsrc_brush_hashes[mode][slot] ==
                     renderer.goldsrc_brush_hashes
                         [GOLDSRC_BRUSH_MODE_CONTROL][slot]))
                brush_valid = 0;
    for (uint32_t slot = 0u; slot < 2u; ++slot)
        if (renderer.goldsrc_brush_hashes
                [GOLDSRC_BRUSH_MODE_COMBINED][slot] ==
                renderer.goldsrc_brush_hashes
                    [GOLDSRC_BRUSH_MODE_OPAQUE][slot] ||
            renderer.goldsrc_brush_hashes
                [GOLDSRC_BRUSH_MODE_COMBINED][slot] ==
                renderer.goldsrc_brush_hashes
                    [GOLDSRC_BRUSH_MODE_ALPHA][slot] ||
            renderer.goldsrc_brush_hashes
                [GOLDSRC_BRUSH_MODE_COMBINED][slot] ==
                renderer.goldsrc_brush_hashes
                    [GOLDSRC_BRUSH_MODE_ADDITIVE][slot])
            brush_valid = 0;
    if (!brush_valid)
        park("goldsrc-brush-readback-or-transform-gate-failure");
    (void)ps5log_printf(PS5LOG_MARK,
        "GOLDSRC_BRUSH_COMPLETE schema=1 frames=%llu modes=%u "
        "readbacks=%u both_slots=true control_pairs=distinct "
        "combined_pairs=distinct transform_changes=true "
        "entities=real-bsp-submodels transforms=independent "
        "render_modes=opaque+alpha+additive depth_write=opaque-only "
        "texture_residency=shared-bsp camera=locked-proof-view "
        "input_dependency=none tokens=exact guards=intact errors=%llu",
        (unsigned long long)run.frames_completed,
        GOLDSRC_BRUSH_MODE_COUNT, GOLDSRC_BRUSH_MODE_COUNT * 2u,
        (unsigned long long)run.telemetry.errors);
#endif
#if defined(PS5_GOLDSRC_STUDIO_GATE) && \
    !defined(PS5_GOLDSRC_PHASE4_FINAL_GATE)
    int studio_valid =
        renderer.goldsrc_studio_first_pose_hash != 0u &&
        renderer.goldsrc_studio_last_pose_hash != 0u &&
        renderer.goldsrc_studio_first_pose_hash !=
            renderer.goldsrc_studio_last_pose_hash;
    for (uint32_t mode = 0u; mode < GOLDSRC_STUDIO_MODE_COUNT; ++mode)
        for (uint32_t slot = 0u; slot < 2u; ++slot)
            if (!renderer.goldsrc_studio_seen[mode][slot] ||
                renderer.goldsrc_studio_hashes[mode][slot] == 0u ||
                renderer.goldsrc_studio_bright[mode][slot] == 0u ||
                (mode != GOLDSRC_STUDIO_MODE_CONTROL &&
                 renderer.goldsrc_studio_hashes[mode][slot] ==
                     renderer.goldsrc_studio_hashes
                         [GOLDSRC_STUDIO_MODE_CONTROL][slot]))
                studio_valid = 0;
    for (uint32_t slot = 0u; slot < 2u; ++slot)
        if (renderer.goldsrc_studio_hashes
                [GOLDSRC_STUDIO_MODE_COMBINED][slot] ==
                renderer.goldsrc_studio_hashes
                    [GOLDSRC_STUDIO_MODE_TEXTURED][slot] ||
            renderer.goldsrc_studio_hashes
                [GOLDSRC_STUDIO_MODE_COMBINED][slot] ==
                renderer.goldsrc_studio_hashes
                    [GOLDSRC_STUDIO_MODE_CHROME][slot] ||
            renderer.goldsrc_studio_hashes
                [GOLDSRC_STUDIO_MODE_COMBINED][slot] ==
                renderer.goldsrc_studio_hashes
                    [GOLDSRC_STUDIO_MODE_ADDITIVE][slot])
            studio_valid = 0;
    if (!studio_valid)
        park("goldsrc-studio-readback-or-animation-gate-failure");
    (void)ps5log_printf(PS5LOG_MARK,
        "GOLDSRC_STUDIO_COMPLETE schema=1 frames=%llu modes=%u "
        "readbacks=%u both_slots=true control_pairs=distinct "
        "combined_pairs=distinct animation_pose_changes=true "
        "skinning=cpu textures=per-model chrome=normal-generated "
        "additive=separate-pipeline geometry=per-frame-transient "
        "texture_residency=shared-bsp-allocation "
        "camera=locked-proof-view input_dependency=none "
        "tokens=exact guards=intact errors=%llu",
        (unsigned long long)run.frames_completed,
        GOLDSRC_STUDIO_MODE_COUNT, GOLDSRC_STUDIO_MODE_COUNT * 2u,
        (unsigned long long)run.telemetry.errors);
#endif
#ifdef PS5_GOLDSRC_PHASE4_FINAL_GATE
    int phase4_final_valid = run.frames_completed == BSP_GATE_FRAME_COUNT &&
        run.telemetry.errors == 0u &&
        renderer.goldsrc_brush_plan.instance_count ==
            GOLDSRC_BRUSH_PHASE4_SCENE_INSTANCE_COUNT &&
        renderer.goldsrc_brush_plan.draw_counts
            [GOLDSRC_BRUSH_INSTANCE_WATER] == 35u &&
        renderer.goldsrc_brush_plan.index_counts
            [GOLDSRC_BRUSH_INSTANCE_WATER] == 312u &&
        renderer.goldsrc_brush_plan.source_render_modes
            [GOLDSRC_BRUSH_INSTANCE_WATER] == 2u &&
        renderer.goldsrc_brush_plan.draw_counts
            [GOLDSRC_BRUSH_INSTANCE_GLASS] == 6u &&
        renderer.goldsrc_brush_plan.index_counts
            [GOLDSRC_BRUSH_INSTANCE_GLASS] == 36u &&
        renderer.goldsrc_studio_first_pose_hash != 0u &&
        renderer.goldsrc_studio_last_pose_hash != 0u &&
        renderer.goldsrc_studio_first_pose_hash !=
            renderer.goldsrc_studio_last_pose_hash &&
        renderer.goldsrc_brush_first_transform_hash != 0u &&
        renderer.goldsrc_brush_last_transform_hash != 0u &&
        renderer.goldsrc_brush_first_transform_hash !=
            renderer.goldsrc_brush_last_transform_hash;
    for (uint32_t slot = 0u; slot < 2u; ++slot) {
        const GoldSrcLightmapLightingUpdate *const lighting =
            &renderer.goldsrc_lighting_updates[slot];
        const GoldSrcSpriteParticleFrame *const effects =
            &renderer.goldsrc_effect_frames[slot];
        const GoldSrcStudioFrame *const studio =
            &renderer.goldsrc_studio_frames[slot];
        const GoldSrcBrushFrame *const brush =
            &renderer.goldsrc_brush_frames[slot];
        const GoldSrcVisibilityFrame *const visibility =
            &renderer.goldsrc_visibility_frames[slot];
        const GoldSrc2DFrame *const screen =
            &renderer.goldsrc_2d_frames[slot];
        if (!renderer.goldsrc_phase4_final_seen[slot] ||
            renderer.goldsrc_phase4_final_hashes[slot] == 0u ||
            renderer.goldsrc_phase4_final_bright[slot] == 0u ||
            lighting->mode != GOLDSRC_LIGHTING_MODE_COMBINED ||
            lighting->dynamic_luxels == 0u ||
            effects->mode != GOLDSRC_EFFECT_MODE_COMBINED ||
            studio->mode != GOLDSRC_STUDIO_MODE_COMBINED ||
            brush->mode != GOLDSRC_BRUSH_MODE_COMBINED ||
            visibility->mode != GOLDSRC_VISIBILITY_MODE_COMBINED ||
            visibility->selected_draws != 362u ||
            screen->alpha_index_count + screen->additive_index_count !=
                522u)
            phase4_final_valid = 0;
    }
    if (!phase4_final_valid)
        park("goldsrc-phase4-final-integrated-gate-failure");
    (void)ps5log_printf(PS5LOG_MARK,
        "GOLDSRC_PHASE4_FINAL_COMPLETE schema=1 frames=%llu "
        "combined_frames=600 readbacks=2 both_slots=true "
        "world_draws=362 brush_draws=69 studio_draws=12 "
        "effect_draws=3 screen_draws=2 lightstyles=real-bsp-planes "
        "dynamic_lights=face-local-radial sprites=true particles=true "
        "cpu_skinning=true chrome=true brush_entities=true water=true "
        "glass=true pvs=true "
        "frustum=true transient=per-slot animation_changes=true "
        "ownership=fence+videoout tokens=exact guards=intact errors=%llu",
        (unsigned long long)run.frames_completed,
        (unsigned long long)run.telemetry.errors);
#endif
#ifdef PS5_GOLDSRC_VIEWPORT_GATE
    (void)ps5log_printf(PS5LOG_MARK,
        "GOLDSRC_VIEWPORT_GATE_COMPLETE schema=1 frames=%llu "
        "sequence=full-clear,inset-opaque,inset-alpha,full-restore "
        "framebuffer_distinct=true input_required=false "
        "tokens=exact guards=intact errors=%llu",
        (unsigned long long)run.frames_completed,
        (unsigned long long)run.telemetry.errors);
#endif
#endif
#ifdef PS5_TEXTURE_SKY_GATE
    (void)ps5log_printf(PS5LOG_MARK,
        "SKY_PASS_READBACK skip_control_buffer=%016llx "
        "sky_pass_buffer=%016llx bytes=%llu sampler=anisotropic4x "
        "lightmap_pattern=1 paired=true framebuffer_distinct=true "
        "guards=intact frames=%llu",
        (unsigned long long)first_hash,
        (unsigned long long)second_hash,
        (unsigned long long)readback_bytes,
        (unsigned long long)run.frames_completed);
#endif
#ifdef PS5_TEXTURE_MIP_GATE
    (void)ps5log_printf(PS5LOG_MARK,
        "MIP_SAMPLER_READBACK trilinear_buffer=%016llx "
        "anisotropic4x_buffer=%016llx bytes=%llu lightmap_pattern=1 "
        "paired=true framebuffer_distinct=true guards=intact frames=%llu",
        (unsigned long long)first_hash,
        (unsigned long long)second_hash,
        (unsigned long long)readback_bytes,
        (unsigned long long)run.frames_completed);
#endif
#ifdef PS5_TEXTURE_ALPHA_GATE
    (void)ps5log_printf(PS5LOG_MARK,
        "ALPHA_TEST_READBACK opaque_control_buffer=%016llx "
        "alpha_test_buffer=%016llx bytes=%llu sampler=anisotropic4x "
        "lightmap_pattern=1 paired=true framebuffer_distinct=true "
        "guards=intact frames=%llu",
        (unsigned long long)first_hash,
        (unsigned long long)second_hash,
        (unsigned long long)readback_bytes,
        (unsigned long long)run.frames_completed);
#endif
#ifdef PS5_TEXTURE_ACCOUNTING_ENABLED
    BspTextureUploadSummary texture_upload_summary;
    if (bsp_texture_accounting_finalize(
            &renderer.texture_accounting, run.frames_completed,
            &texture_upload_summary) != 0)
        park("texture-accounting-summary-gate-failure");
    (void)ps5log_printf(PS5LOG_MARK,
        "TEXTURE_UPLOAD_SUMMARY schema=1 frames=%llu "
        "transient_bytes_per_frame=%llu "
        "bounded_lightmap_bytes_per_frame=%llu "
        "transient_bytes_total=%llu lightmap_bytes_total=%llu "
        "upload_bytes_total=%llu frame_bytes_min=%llu "
        "frame_bytes_max=%llu full_upload_frames=%llu "
        "bounded_upload_frames=%llu sequence_hash=%016llx "
        "sequence=gap-free accounting=checked-u64",
        (unsigned long long)texture_upload_summary.frames,
        (unsigned long long)
            texture_upload_summary.transient_bytes_per_frame,
        (unsigned long long)
            texture_upload_summary.bounded_lightmap_bytes_per_frame,
        (unsigned long long)texture_upload_summary.transient_bytes_total,
        (unsigned long long)texture_upload_summary.lightmap_bytes_total,
        (unsigned long long)texture_upload_summary.upload_bytes_total,
        (unsigned long long)texture_upload_summary.frame_bytes_min,
        (unsigned long long)texture_upload_summary.frame_bytes_max,
        (unsigned long long)texture_upload_summary.full_upload_frames,
        (unsigned long long)texture_upload_summary.bounded_upload_frames,
        (unsigned long long)texture_upload_summary.sequence_hash);
#endif
#endif
    if (!resource_valid)
        park("resource-render-or-retirement-gate-failure");
#elif defined(PS5_BSP_TEXTURED)
    const int textured_valid =
        input_continuity_valid &&
        first_bright != 0u && second_bright != 0u &&
        renderer.bsp_plan.descriptor_table_dwords ==
            renderer.bsp_bundle.texture_count * BSP_TEXTURE_TABLE_DWORDS;
    if (!textured_valid)
        park("textured-render-or-input-continuity-gate-failure");
#else
    const int noclip_valid =
        input_continuity_valid &&
        renderer.noclip.moving_frames >= BSP_NOCLIP_MIN_MOVING_FRAMES &&
        renderer.noclip.looking_frames >= BSP_NOCLIP_MIN_LOOKING_FRAMES &&
        renderer.noclip.distance_travelled >= 100.0f;
    if (!noclip_valid)
        park("noclip-input-or-movement-gate-failure");
#endif
#ifdef PS5_RESOURCE_FOUNDATION
#ifdef PS5_TEXTURE_PATH
    (void)ps5log_printf(PS5LOG_MARK,
        "BSP_TEXTURE_PATH_LIGHTMAP_COMPLETE frames=%llu "
        "resident_bytes=%llu uploaded_bytes=%llu "
        "patch=%u,%u+%ux%u hit_face=%u "
#ifdef PS5_GOLDSRC_LIGHTING_GATE
        "patterns=base+lightstyle+dlight+combined "
#elif defined(PS5_TEXTURE_MIP_GATE) || defined(PS5_TEXTURE_ALPHA_GATE) || \
    defined(PS5_TEXTURE_SKY_GATE)
        "patterns=paired "
#else
        "patterns=alternating "
#endif
        "readback=gpu-visible surrounding=stable tokens=exact "
        "guards=intact errors=%llu",
        (unsigned long long)run.frames_completed,
        (unsigned long long)
            renderer.texture_accounting.residency.pool_resident_bytes,
        (unsigned long long)
            renderer.texture_accounting.upload.upload_bytes_total,
        renderer.dynamic_lightmap_layout.patch_x,
        renderer.dynamic_lightmap_layout.patch_y,
        renderer.dynamic_lightmap_layout.patch_width,
        renderer.dynamic_lightmap_layout.patch_height,
        renderer.dynamic_lightmap_layout.hit_face,
        (unsigned long long)run.telemetry.errors);
#ifdef PS5_TEXTURE_MIP_GATE
    (void)ps5log_printf(PS5LOG_MARK,
        "BSP_TEXTURE_PATH_MIP_COMPLETE frames=%llu textures=%u "
        "layout=addr-sw-linear t_sharp=mip-aware "
        "samplers=trilinear+anisotropic4x readback=gpu-visible "
        "paired_lightmap=true tokens=exact guards=intact errors=%llu",
        (unsigned long long)run.frames_completed,
        renderer.bsp_bundle.texture_count,
        (unsigned long long)run.telemetry.errors);
#endif
#ifdef PS5_TEXTURE_SKY_GATE
    (void)ps5log_printf(PS5LOG_MARK,
        "BSP_TEXTURE_PATH_SKY_COMPLETE frames=%llu textures=%u draws=%u "
        "pipeline=separate skip_control=gpu-visible "
        "sky_pass=gpu-visible paired_lightmap=true tokens=exact "
        "guards=intact errors=%llu",
        (unsigned long long)run.frames_completed,
        renderer.sky_plan.texture_count, renderer.sky_plan.draw_count,
        (unsigned long long)run.telemetry.errors);
#endif
#ifdef PS5_TEXTURE_ALPHA_GATE
    (void)ps5log_printf(PS5LOG_MARK,
        "BSP_TEXTURE_PATH_ALPHA_COMPLETE frames=%llu textures=%u draws=%u "
        "pipeline=separate opaque_control=gpu-visible "
        "alpha_test=gpu-visible paired_lightmap=true tokens=exact "
        "guards=intact errors=%llu",
        (unsigned long long)run.frames_completed,
        renderer.alpha_test_plan.texture_count,
        renderer.alpha_test_plan.draw_count,
        (unsigned long long)run.telemetry.errors);
#endif
#ifdef PS5_TEXTURE_ACCOUNTING_ENABLED
    (void)ps5log_printf(PS5LOG_MARK,
        "BSP_TEXTURE_PATH_ACCOUNTING_COMPLETE frames=%llu "
        "pool_resident_bytes=%llu texture_payload_bytes=%llu "
        "upload_bytes_total=%llu sequence_hash=%016llx "
        "allocations=6 accounting=exact+gap-free tokens=exact "
        "guards=intact input_dependency=none errors=%llu",
        (unsigned long long)run.frames_completed,
        (unsigned long long)
            renderer.texture_accounting.residency.pool_resident_bytes,
        (unsigned long long)
            renderer.texture_accounting.residency.texture_payload_bytes,
        (unsigned long long)
            renderer.texture_accounting.upload.upload_bytes_total,
        (unsigned long long)
            renderer.texture_accounting.upload.sequence_hash,
        (unsigned long long)run.telemetry.errors);
#ifdef PS5_TEXTURE_FINAL_GATE
    (void)ps5log_printf(PS5LOG_MARK,
        "BSP_TEXTURE_PATH_FINAL_COMPLETE schema=1 frames=%llu "
        "mip_chains=%u opaque_draws=%u alpha_draws=%u sky_draws=%u "
        "pipelines=4 sampler=anisotropic4x dynamic_lightmap=bounded "
        "pool_resident_bytes=%llu texture_payload_bytes=%llu "
        "upload_bytes_total=%llu sequence_hash=%016llx "
        "readback=gpu-visible input_dependency=none tokens=exact "
        "guards=intact errors=%llu",
        (unsigned long long)run.frames_completed,
        renderer.bsp_bundle.texture_count, accounting_opaque_draws,
        accounting_alpha_draws, accounting_sky_draws,
        (unsigned long long)
            renderer.texture_accounting.residency.pool_resident_bytes,
        (unsigned long long)
            renderer.texture_accounting.residency.texture_payload_bytes,
        (unsigned long long)
            renderer.texture_accounting.upload.upload_bytes_total,
        (unsigned long long)
            renderer.texture_accounting.upload.sequence_hash,
        (unsigned long long)run.telemetry.errors);
#endif
#endif
#endif
    (void)ps5log_printf(PS5LOG_MARK,
        "BSP_RESOURCE_SOAK_COMPLETE frames=%llu connected_frames=%llu "
        "read_errors=%llu textures=%u descriptor_dwords=%u "
        "constants=per-frame overlay=transient pipelines=4 "
#ifdef PS5_TEXTURE_ACCOUNTING_ENABLED
        "input_dependency=none "
#endif
        "tokens=exact guards=intact errors=%llu",
        (unsigned long long)run.frames_completed,
        (unsigned long long)renderer.noclip.connected_frames,
        (unsigned long long)renderer.pad_read_errors,
        renderer.bsp_bundle.texture_count,
        renderer.bsp_plan.descriptor_table_dwords,
        (unsigned long long)run.telemetry.errors);
#ifdef PS5_GOLDSRC_2D_GATE
    (void)ps5log_printf(PS5LOG_MARK,
        "GOLDSRC_2D_COMPLETE schema=1 frames=%llu draws_per_frame=2 "
        "passes=alpha+additive projection=orthographic "
        "components=hud+console+menu+font atlas=procedural-rgba8 "
        "geometry=per-frame-transient tokens=exact guards=intact "
        "errors=%llu",
        (unsigned long long)run.frames_completed,
        (unsigned long long)run.telemetry.errors);
#endif
    const uint64_t retire_token = renderer.last_completed_token;
    uint32_t reclaimed = 0u;
#ifdef PS5_TEXTURE_PATH
    if (ps5_resource_pool_release_deferred(
            &resources.resource_pool,
            &resources.dynamic_lightmap_allocations[0], retire_token) !=
            PS5_RESOURCE_POOL_OK ||
        ps5_resource_pool_release_deferred(
            &resources.resource_pool,
            &resources.dynamic_lightmap_allocations[1], retire_token) !=
            PS5_RESOURCE_POOL_OK)
        park("dynamic-lightmap-pool-retirement-gate-failure");
#endif
    if (ps5_resource_pool_release_deferred(
            &resources.resource_pool, &resources.bsp_allocation,
            retire_token) != PS5_RESOURCE_POOL_OK ||
        ps5_resource_pool_release_deferred(
            &resources.resource_pool, &resources.shader_allocation,
            retire_token) != PS5_RESOURCE_POOL_OK ||
        ps5_resource_pool_release_deferred(
            &resources.resource_pool, &resources.depth_allocation,
            retire_token) != PS5_RESOURCE_POOL_OK ||
        ps5_resource_pool_release_deferred(
            &resources.resource_pool, &resources.transient_allocation,
            retire_token) != PS5_RESOURCE_POOL_OK ||
        ps5_resource_pool_reclaim(
            &resources.resource_pool, retire_token, 1,
            &reclaimed) != PS5_RESOURCE_POOL_OK ||
#ifdef PS5_TEXTURE_PATH
        reclaimed != 6u)
#else
        reclaimed != 4u)
#endif
        park("resource-pool-retirement-gate-failure");
    (void)ps5log_printf(PS5LOG_MARK,
        "RESOURCE_POOL_RETIRED token=%llu reclaimed=%u "
        "completion=fence+videoout",
        (unsigned long long)retire_token, reclaimed);
#elif defined(PS5_BSP_TEXTURED)
    (void)ps5log_printf(PS5LOG_MARK,
        "BSP_TEXTURED_SOAK_COMPLETE frames=%llu connected_frames=%llu "
        "moving_frames=%llu looking_frames=%llu distance_milli=%llu "
        "input_changes=%llu input_hash=%016llx read_errors=%llu "
        "textures=%u descriptor_dwords=%u composition=base_x_lightmap "
        "tokens=exact guards=intact errors=%llu",
        (unsigned long long)run.frames_completed,
        (unsigned long long)renderer.noclip.connected_frames,
        (unsigned long long)renderer.noclip.moving_frames,
        (unsigned long long)renderer.noclip.looking_frames,
        (unsigned long long)(renderer.noclip.distance_travelled * 1000.0f),
        (unsigned long long)renderer.noclip.input_changes,
        (unsigned long long)renderer.noclip.input_hash,
        (unsigned long long)renderer.pad_read_errors,
        renderer.bsp_bundle.texture_count,
        renderer.bsp_plan.descriptor_table_dwords,
        (unsigned long long)run.telemetry.errors);
#else
    (void)ps5log_printf(PS5LOG_MARK,
        "BSP_NOCLIP_SOAK_COMPLETE frames=%llu connected_frames=%llu "
        "moving_frames=%llu looking_frames=%llu distance_milli=%llu "
        "input_changes=%llu input_hash=%016llx read_errors=%llu "
        "tokens=exact guards=intact errors=%llu",
        (unsigned long long)run.frames_completed,
        (unsigned long long)renderer.noclip.connected_frames,
        (unsigned long long)renderer.noclip.moving_frames,
        (unsigned long long)renderer.noclip.looking_frames,
        (unsigned long long)(renderer.noclip.distance_travelled * 1000.0f),
        (unsigned long long)renderer.noclip.input_changes,
        (unsigned long long)renderer.noclip.input_hash,
        (unsigned long long)renderer.pad_read_errors,
        (unsigned long long)run.telemetry.errors);
#endif
    park_complete();
#else
    const int readback_valid = first_hash == second_hash &&
        first_bright != 0u && first_bright == second_bright;
    (void)ps5log_printf(readback_valid ? PS5LOG_MARK : PS5LOG_ERR,
        "BSP_READBACK_FNV64 buffer0=%016llx buffer1=%016llx bytes=%llu "
        "match=%s bright_pixels0=%llu bright_pixels1=%llu "
        "geometry_visible=%s guards=intact frames=%llu errors=%llu",
        (unsigned long long)first_hash, (unsigned long long)second_hash,
        (unsigned long long)readback_bytes,
        first_hash == second_hash ? "true" : "false",
        (unsigned long long)first_bright,
        (unsigned long long)second_bright,
        first_bright != 0u && first_bright == second_bright ? "true" : "false",
        (unsigned long long)run.frames_completed,
        (unsigned long long)run.telemetry.errors);
    if (!readback_valid)
        park("fixed-camera-readback-or-visibility-mismatch");
    (void)ps5log_line(PS5LOG_MARK,
        "BSP_GATE1_COMPLETE fixed_camera=true readback_exact=true "
        "geometry_visible=true tokens=exact guards=intact");
    park_complete();
#endif
#endif
#else
    uint64_t last_guard_check = 0u;
    uint64_t last_heartbeat = 0u;
    for (;;) {
        GearsFrameRunnerResult run = {0};
        result = gears_frame_loop_step(&loop);
        if (result != 0 || gears_frame_loop_result(&loop, &run) != 0 ||
            run.state != GEARS_RUN_ACTIVE)
            park("frame-loop-or-ownership-failure");
        if (run.frames_completed >= last_guard_check + 60u) {
            if (!guards_intact())
                park("guard-corruption");
            last_guard_check = run.frames_completed;
        }
        if (run.frames_completed >= last_heartbeat + 3600u) {
            (void)ps5log_printf(PS5LOG_MARK,
                "GEARS_HEARTBEAT frames=%llu max_in_flight=%u "
                "retired_fences=zero tokens=exact guards=intact errors=%llu",
                (unsigned long long)run.frames_completed,
                run.max_frames_in_flight,
                (unsigned long long)run.telemetry.errors);
            last_heartbeat = run.frames_completed;
        }
    }
#endif
}
