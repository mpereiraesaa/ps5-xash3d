#include "ref_agc_live_frame.h"

#include <assert.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

typedef struct LiveConsumer {
    RefAgcLiveStore *store;
    uint64_t after_serial;
    RefAgcLiveFrame frame;
    int wait_result;
    int consume_result;
} LiveConsumer;

static void *consume_one(void *opaque)
{
    LiveConsumer *consumer = opaque;
    consumer->wait_result = ref_agc_live_wait_latest(
        consumer->store, consumer->after_serial, &consumer->frame);
    if (consumer->wait_result == 0)
        consumer->consume_result = ref_agc_live_mark_consumed(
            consumer->store, consumer->frame.serial);
    return NULL;
}

int main(void)
{
    RefAgcLiveStore store;
    RefAgcLiveFrame frame;
    RefAgcLiveWorld world = {0};
    RefAgcLiveView view = {
        .viewport = {0, 0, 1920, 1080},
        .origin = {128.0f, -64.0f, 32.0f},
        .angles = {10.0f, 90.0f, 0.0f},
        .fov_x = 90.0f,
        .fov_y = 58.7f,
        .view_entity = 1,
        .flags = 1,
        .time_seconds = 12.25,
        .valid = 1,
    };
    RefAgcLiveEntity entity = {
        .index = 7,
        .entity_type = 2,
        .model_type = 1,
        .model_index = 12,
        .studio_handle = 27u,
        .render_mode = 0,
        .render_amount = 255,
        .render_color = {255, 128, 64, 255},
        .origin = {1.0f, 2.0f, 3.0f},
        .scale = 1.0f,
        .first_surface = 120,
        .surface_count = 8,
        .mins = {-16.0f, -16.0f, -36.0f},
        .maxs = {16.0f, 16.0f, 36.0f},
        .radius = 44.0f,
    };
    RefAgcLive2DCommand command = {
        .type = REF_AGC_LIVE_2D_STRETCH_PIC,
        .texture = 33,
        .x = 10.0f,
        .y = 20.0f,
        .width = 100.0f,
        .height = 50.0f,
        .s2 = 1.0f,
        .t2 = 1.0f,
        .color = {255, 255, 255, 255},
    };
    float camera_position[3];
    float camera_forward[3];
    float camera_aspect = 0.0f;

    assert(ref_agc_live_store_init(&store) == 0);
    const uint32_t sky_handles[REF_AGC_LIVE_SKY_SIDES] = {
        11u, 12u, 13u, 14u, 15u, 16u,
    };
    ref_agc_live_set_sky(&store, sky_handles);
    strcpy(world.model_name, "maps/c1a0.bsp");
    world.model_type = 1;
    world.model_flags = 1u << 29;
    world.surfaces = 1234;
    world.vertices = 5678;
    world.textures = 92;
    world.has_visibility = 1;
    world.has_lightdata = 1;
    world.first_surface = 0;
    world.surface_count = 117;
    ref_agc_live_set_world(&store, &world);

    strcpy(entity.model_name, "models/barney.mdl");
    /* Xash calls CL_EmitEntities (ClearScene/AddEntity) before V_PreRender
     * calls BeginFrame. The frame reset must preserve that staged scene. */
    ref_agc_live_clear_scene(&store);
    assert(ref_agc_live_add_entity(&store, &entity) == 0);
    ref_agc_live_begin_frame(&store, 1, 41);
    ref_agc_live_set_canvas(&store, 1920u, 1080u);
    ref_agc_live_set_view(&store, &view, 42);
    ref_agc_live_set_viewmodel(&store, &entity);
    assert(ref_agc_live_add_2d(&store, &command) == 0);
    assert(ref_agc_live_publish(&store, 43) == 0);
    assert(ref_agc_live_take_latest(&store, 0, &frame) == 0);
    assert(frame.serial == 1 && frame.map_serial == 1);
    assert(frame.begin_calls == 41 && frame.scene_calls == 42 &&
           frame.end_calls == 43);
    assert(frame.canvas_width == 1920u && frame.canvas_height == 1080u);
    assert(frame.view.valid && frame.view.viewport[2] == 1920);
    assert(frame.view.time_seconds == 12.25 && !frame.view.paused);
    assert(frame.sky.active && frame.sky.revision == 1u &&
           frame.sky.texture_handles[0] == 11u &&
           frame.sky.texture_handles[5] == 16u);
    assert(frame.world.surfaces == 1234);
    assert(strcmp(frame.world.model_name, "maps/c1a0.bsp") == 0);
    assert(frame.entity_count == 1 && frame.entities[0].index == 7);
    assert(frame.entities[0].first_surface == 120 &&
           frame.entities[0].surface_count == 8 &&
           frame.entities[0].radius == 44.0f);
    assert(frame.world.first_surface == 0 && frame.world.surface_count == 117);
    assert(frame.viewmodel_valid && frame.viewmodel.model_index == 12 &&
           frame.viewmodel.studio_handle == 27u &&
           strcmp(frame.viewmodel.model_name, "models/barney.mdl") == 0);
    assert(frame.scene_clears == 1);
    assert(strcmp(frame.entities[0].model_name, "models/barney.mdl") == 0);
    assert(frame.command_2d_count == 1 &&
           frame.commands_2d[0].texture == 33);
    assert(ref_agc_live_take_latest(&store, frame.serial, &frame) == 1);
    assert(ref_agc_live_view_camera(&view, camera_position,
                                    camera_forward, &camera_aspect) == 0);
    assert(fabsf(camera_position[0] - 128.0f) < 0.0001f);
    assert(fabsf(camera_position[1] - 32.0f) < 0.0001f);
    assert(fabsf(camera_position[2] - 64.0f) < 0.0001f);
    assert(fabsf(camera_forward[0]) < 0.0001f);
    assert(fabsf(camera_forward[1] + 0.1736482f) < 0.0001f);
    assert(fabsf(camera_forward[2] + 0.9848077f) < 0.0001f);
    assert(fabsf(camera_aspect - (16.0f / 9.0f)) < 0.0001f);
    assert(ref_agc_live_world_view_ready(&frame) == 1);
    frame.view.flags = 0u;
    assert(ref_agc_live_world_view_ready(&frame) == 0);
    frame.view.flags = REF_AGC_LIVE_RF_DRAW_WORLD;
    frame.world.surface_count = 0u;
    assert(ref_agc_live_world_view_ready(&frame) == 0);
    frame.world.surface_count = 117u;
    frame.world.first_surface = frame.world.surfaces;
    assert(ref_agc_live_world_view_ready(&frame) == 0);
    frame.world.first_surface = 0u;
    frame.map_serial = 0u;
    assert(ref_agc_live_world_view_ready(&frame) == 0);

    ref_agc_live_begin_frame(&store, 0, 44);
    assert(!store.building.viewmodel_valid);
    ref_agc_live_clear_scene(&store);
    for (unsigned i = 0; i < REF_AGC_LIVE_MAX_ENTITIES; ++i)
        assert(ref_agc_live_add_entity(&store, &entity) == 0);
    assert(ref_agc_live_add_entity(&store, &entity) == -2);
    for (unsigned i = 0; i < REF_AGC_LIVE_MAX_2D_COMMANDS; ++i)
        assert(ref_agc_live_add_2d(&store, &command) == 0);
    assert(ref_agc_live_add_2d(&store, &command) == -2);
    assert(ref_agc_live_publish(&store, 45) == 0);
    assert(ref_agc_live_take_latest(&store, 1, &frame) == 0);
    assert(frame.serial == 2 && frame.entity_count == 2048);
    assert(frame.command_2d_count == 4096);
    assert(frame.dropped_entities == 1 && frame.dropped_2d_commands == 1);
    assert(frame.world.serial == 1);
    ref_agc_live_set_sky(&store, NULL);

    strcpy(world.model_name, "maps/c1a1.bsp");
    ref_agc_live_set_world(&store, &world);
    ref_agc_live_begin_frame(&store, 1, 46);
    assert(ref_agc_live_publish(&store, 47) == 0);
    assert(ref_agc_live_take_latest(&store, 2, &frame) == 0);
    assert(frame.serial == 3 && frame.map_serial == 2);
    assert(frame.world.serial == 2);
    assert(!frame.sky.active && frame.sky.revision == 2u);
    assert(strcmp(frame.world.model_name, "maps/c1a1.bsp") == 0);

    /* The engine publishes at most one frame ahead. A consumer waiting before
     * publication must wake, copy that exact serial, and ACK it monotonically. */
    LiveConsumer consumer = {
        .store = &store,
        .after_serial = frame.serial,
        .wait_result = -99,
        .consume_result = -99,
    };
    pthread_t consumer_thread;
    assert(pthread_create(&consumer_thread, NULL, consume_one, &consumer) == 0);
    ref_agc_live_begin_frame(&store, 0, 48);
    ref_agc_live_set_view(&store, &view, 49);
    assert(ref_agc_live_publish(&store, 50) == 0);
    assert(ref_agc_live_wait_consumed(&store, 4) == 0);
    assert(pthread_join(consumer_thread, NULL) == 0);
    assert(consumer.wait_result == 0 && consumer.consume_result == 0);
    assert(consumer.frame.serial == 4 && consumer.frame.end_calls == 50);
    assert(ref_agc_live_mark_consumed(&store, 5) == -3);
    assert(ref_agc_live_mark_consumed(&store, 3) == -3);

    /* Stop is a wake-up, not a detached flag: both a blocked producer and a
     * blocked consumer can leave deterministically during RefAPI shutdown. */
    consumer.after_serial = 4;
    consumer.wait_result = -99;
    consumer.consume_result = -99;
    assert(pthread_create(&consumer_thread, NULL, consume_one, &consumer) == 0);
    assert(ref_agc_live_request_stop(&store, 0) == 0);
    assert(pthread_join(consumer_thread, NULL) == 0);
    assert(consumer.wait_result == 1 && consumer.consume_result == -99);
    assert(ref_agc_live_publish(&store, 51) == 1);

    ref_agc_live_store_destroy(&store);
    puts("ref_agc live frame tests passed");
    return 0;
}
