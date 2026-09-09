#ifndef PS5_XASH3D_REF_AGC_LIVE_FRAME_H
#define PS5_XASH3D_REF_AGC_LIVE_FRAME_H

#include <pthread.h>
#include <stdint.h>
#include "ref_agc_studio_lighting.h"

/* Upstream R_StudioLerpMovement timing; preserve its bounded-time extrapolation
 * rather than clamping to [0,1]. Stale/equal timestamps select current state. */
static inline float ref_agc_studio_movement_fraction(
    double time, double animtime, double previous_animtime)
{
    if (!__builtin_isfinite(time) || !__builtin_isfinite(animtime) ||
        !__builtin_isfinite(previous_animtime))
        return 1.0f;
    if (time < animtime + 1.0 && animtime != previous_animtime)
        return (float)((time - animtime) / (animtime - previous_animtime));
    return 1.0f;
}

enum {
    REF_AGC_LIVE_MAX_ENTITIES = 2048,
    REF_AGC_LIVE_MAX_2D_COMMANDS = 4096,
    REF_AGC_LIVE_MODEL_NAME = 64,
    REF_AGC_LIVE_SKY_SIDES = 6,
    REF_AGC_LIVE_MAX_STUDIO_POSES = 32,
    REF_AGC_LIVE_MAX_STUDIO_BONES = 128,
    REF_AGC_LIVE_RF_DRAW_WORLD = 1u << 0,
    REF_AGC_LIVE_ENTITY_NORMAL = 0,
    REF_AGC_LIVE_MODEL_BRUSH = 0,
    REF_AGC_LIVE_MODEL_SPRITE = 1,
    REF_AGC_LIVE_MODEL_ALIAS = 2,
    REF_AGC_LIVE_MODEL_STUDIO = 3,
};

typedef enum RefAgcLive2DCommandType {
    REF_AGC_LIVE_2D_MODE = 1,
    REF_AGC_LIVE_2D_STRETCH_PIC = 2,
    REF_AGC_LIVE_2D_FILL_RGBA = 3,
} RefAgcLive2DCommandType;

typedef struct RefAgcLiveView {
    int32_t viewport[4];
    float origin[3];
    float angles[3];
    float fov_x;
    float fov_y;
    int32_t view_entity;
    uint32_t flags;
    double time_seconds;
    uint32_t paused;
    uint32_t sampling_probe_mode; /* opt-in QA: 0 normal, 1 base, 2 light, 3 solid */
    uint32_t valid;
} RefAgcLiveView;

typedef struct RefAgcLiveSky {
    uint64_t revision;
    uint32_t texture_handles[REF_AGC_LIVE_SKY_SIDES];
    uint32_t active;
} RefAgcLiveSky;

typedef struct RefAgcLiveWorld {
    uint64_t serial;
    char model_name[REF_AGC_LIVE_MODEL_NAME];
    int32_t model_type;
    uint32_t model_flags;
    uint32_t surfaces;
    uint32_t vertices;
    uint32_t edges;
    uint32_t textures;
    uint32_t leafs;
    uint32_t has_visibility;
    uint32_t has_lightdata;
    uint32_t first_surface;
    uint32_t surface_count;
    float mins[3];
    float maxs[3];
} RefAgcLiveWorld;

typedef struct RefAgcLiveEntity {
    int32_t index;
    int32_t entity_type;
    int32_t model_type;
    int32_t model_index;
    uint32_t studio_handle;
    uint32_t studio_pose; /* one-based owned pose index; zero means absent */
    int32_t sequence;
    int32_t body;
    int32_t skin;
    int32_t render_mode;
    int32_t render_amount;
    int32_t render_fx;
    uint32_t effects;
    uint8_t render_color[4];
    float origin[3];
    float angles[3];
    float scale;
    float frame;
    uint32_t first_surface;
    uint32_t surface_count;
    float mins[3];
    float maxs[3];
    float radius;
    char model_name[REF_AGC_LIVE_MODEL_NAME];
} RefAgcLiveEntity;

typedef struct RefAgcLiveStudioPose {
    uint32_t bones;
    float frame;
    RefAgcStudioLighting lighting;
    float matrices[REF_AGC_LIVE_MAX_STUDIO_BONES][3][4];
} RefAgcLiveStudioPose;

typedef struct RefAgcLive2DCommand {
    uint32_t type;
    int32_t render_mode;
    int32_t texture;
    uint32_t enabled; /* MODE: enable 2D; draw commands: independent alpha test */
    float x;
    float y;
    float width;
    float height;
    float s1;
    float t1;
    float s2;
    float t2;
    uint8_t color[4];
} RefAgcLive2DCommand;

typedef struct RefAgcLiveFrame {
    uint16_t studio_light_gamma[1024];
    uint64_t serial;
    uint64_t map_serial;
    uint64_t begin_calls;
    uint64_t scene_calls;
    uint64_t end_calls;
    uint32_t canvas_width;
    uint32_t canvas_height;
    RefAgcLiveWorld world;
    RefAgcLiveSky sky;
    RefAgcLiveView view;
    RefAgcLiveEntity viewmodel;
    RefAgcLiveEntity entities[REF_AGC_LIVE_MAX_ENTITIES];
    RefAgcLiveStudioPose studio_poses[REF_AGC_LIVE_MAX_STUDIO_POSES];
    uint32_t studio_pose_count;
    RefAgcLive2DCommand commands_2d[REF_AGC_LIVE_MAX_2D_COMMANDS];
    uint32_t entity_count;
    uint32_t command_2d_count;
    uint32_t dropped_entities;
    uint32_t dropped_2d_commands;
    uint32_t clear_scene;
    uint32_t scene_clears;
    uint32_t viewmodel_valid;
} RefAgcLiveFrame;

typedef struct RefAgcLiveStore {
    pthread_mutex_t publish_lock;
    pthread_cond_t frame_ready;
    pthread_cond_t frame_consumed;
    RefAgcLiveWorld current_world;
    RefAgcLiveSky current_sky;
    RefAgcLiveFrame building;
    RefAgcLiveFrame published;
    uint64_t next_frame_serial;
    uint64_t next_map_serial;
    uint64_t consumed_serial;
    uint64_t scene_serial;
    uint64_t last_begin_scene_serial;
    int stop_requested;
    int stop_result;
    int initialized;
} RefAgcLiveStore;

int ref_agc_live_store_init(RefAgcLiveStore *store);
void ref_agc_live_store_destroy(RefAgcLiveStore *store);
void ref_agc_live_set_world(RefAgcLiveStore *store,
                            const RefAgcLiveWorld *world);
void ref_agc_live_set_canvas(RefAgcLiveStore *store,
                             uint32_t width, uint32_t height);
void ref_agc_live_set_sky(
    RefAgcLiveStore *store,
    const uint32_t texture_handles[REF_AGC_LIVE_SKY_SIDES]);
void ref_agc_live_begin_frame(RefAgcLiveStore *store, int clear_scene,
                              uint64_t begin_calls);
void ref_agc_live_clear_scene(RefAgcLiveStore *store);
int ref_agc_live_add_entity(RefAgcLiveStore *store,
                            const RefAgcLiveEntity *entity);
void ref_agc_live_set_viewmodel(RefAgcLiveStore *store,
                                const RefAgcLiveEntity *viewmodel);
void ref_agc_live_set_view(RefAgcLiveStore *store,
                           const RefAgcLiveView *view,
                           uint64_t scene_calls);
int ref_agc_live_add_2d(RefAgcLiveStore *store,
                        const RefAgcLive2DCommand *command);
int ref_agc_live_publish(RefAgcLiveStore *store, uint64_t end_calls);
int ref_agc_live_take_latest(RefAgcLiveStore *store, uint64_t after_serial,
                             RefAgcLiveFrame *out);
int ref_agc_live_wait_latest(RefAgcLiveStore *store, uint64_t after_serial,
                             RefAgcLiveFrame *out);
int ref_agc_live_mark_consumed(RefAgcLiveStore *store, uint64_t serial);
int ref_agc_live_wait_consumed(RefAgcLiveStore *store, uint64_t serial);
int ref_agc_live_request_stop(RefAgcLiveStore *store, int result);

int ref_agc_live_view_camera(const RefAgcLiveView *view,
                             float position[3], float forward[3],
                             float *aspect_ratio);
int ref_agc_live_world_view_ready(const RefAgcLiveFrame *frame);

#endif
