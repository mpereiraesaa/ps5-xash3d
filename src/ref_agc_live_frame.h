#ifndef PS5_XASH3D_REF_AGC_LIVE_FRAME_H
#define PS5_XASH3D_REF_AGC_LIVE_FRAME_H

#include <pthread.h>
#include <stdint.h>

enum {
    REF_AGC_LIVE_MAX_ENTITIES = 2048,
    REF_AGC_LIVE_MAX_2D_COMMANDS = 4096,
    REF_AGC_LIVE_MODEL_NAME = 64,
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
    uint32_t valid;
} RefAgcLiveView;

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
    float mins[3];
    float maxs[3];
} RefAgcLiveWorld;

typedef struct RefAgcLiveEntity {
    int32_t index;
    int32_t entity_type;
    int32_t model_type;
    int32_t model_index;
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
    char model_name[REF_AGC_LIVE_MODEL_NAME];
} RefAgcLiveEntity;

typedef struct RefAgcLive2DCommand {
    uint32_t type;
    int32_t render_mode;
    int32_t texture;
    uint32_t enabled;
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
    uint64_t serial;
    uint64_t map_serial;
    uint64_t begin_calls;
    uint64_t scene_calls;
    uint64_t end_calls;
    RefAgcLiveWorld world;
    RefAgcLiveView view;
    RefAgcLiveEntity entities[REF_AGC_LIVE_MAX_ENTITIES];
    RefAgcLive2DCommand commands_2d[REF_AGC_LIVE_MAX_2D_COMMANDS];
    uint32_t entity_count;
    uint32_t command_2d_count;
    uint32_t dropped_entities;
    uint32_t dropped_2d_commands;
    uint32_t clear_scene;
    uint32_t scene_clears;
} RefAgcLiveFrame;

typedef struct RefAgcLiveStore {
    pthread_mutex_t publish_lock;
    RefAgcLiveWorld current_world;
    RefAgcLiveFrame building;
    RefAgcLiveFrame published;
    uint64_t next_frame_serial;
    uint64_t next_map_serial;
    uint64_t scene_serial;
    uint64_t last_begin_scene_serial;
    int initialized;
} RefAgcLiveStore;

int ref_agc_live_store_init(RefAgcLiveStore *store);
void ref_agc_live_store_destroy(RefAgcLiveStore *store);
void ref_agc_live_set_world(RefAgcLiveStore *store,
                            const RefAgcLiveWorld *world);
void ref_agc_live_begin_frame(RefAgcLiveStore *store, int clear_scene,
                              uint64_t begin_calls);
void ref_agc_live_clear_scene(RefAgcLiveStore *store);
int ref_agc_live_add_entity(RefAgcLiveStore *store,
                            const RefAgcLiveEntity *entity);
void ref_agc_live_set_view(RefAgcLiveStore *store,
                           const RefAgcLiveView *view,
                           uint64_t scene_calls);
int ref_agc_live_add_2d(RefAgcLiveStore *store,
                        const RefAgcLive2DCommand *command);
int ref_agc_live_publish(RefAgcLiveStore *store, uint64_t end_calls);
int ref_agc_live_take_latest(RefAgcLiveStore *store, uint64_t after_serial,
                             RefAgcLiveFrame *out);

#endif
