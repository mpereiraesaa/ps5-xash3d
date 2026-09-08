#include "ref_agc_live_frame.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

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
    };
    RefAgcLiveEntity entity = {
        .index = 7,
        .entity_type = 2,
        .model_type = 1,
        .model_index = 12,
        .render_mode = 0,
        .render_amount = 255,
        .render_color = {255, 128, 64, 255},
        .origin = {1.0f, 2.0f, 3.0f},
        .scale = 1.0f,
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

    assert(ref_agc_live_store_init(&store) == 0);
    strcpy(world.model_name, "maps/c1a0.bsp");
    world.model_type = 1;
    world.model_flags = 1u << 29;
    world.surfaces = 1234;
    world.vertices = 5678;
    world.textures = 92;
    world.has_visibility = 1;
    world.has_lightdata = 1;
    ref_agc_live_set_world(&store, &world);

    strcpy(entity.model_name, "models/barney.mdl");
    /* Xash calls CL_EmitEntities (ClearScene/AddEntity) before V_PreRender
     * calls BeginFrame. The frame reset must preserve that staged scene. */
    ref_agc_live_clear_scene(&store);
    assert(ref_agc_live_add_entity(&store, &entity) == 0);
    ref_agc_live_begin_frame(&store, 1, 41);
    ref_agc_live_set_view(&store, &view, 42);
    assert(ref_agc_live_add_2d(&store, &command) == 0);
    assert(ref_agc_live_publish(&store, 43) == 0);
    assert(ref_agc_live_take_latest(&store, 0, &frame) == 0);
    assert(frame.serial == 1 && frame.map_serial == 1);
    assert(frame.begin_calls == 41 && frame.scene_calls == 42 &&
           frame.end_calls == 43);
    assert(frame.view.valid && frame.view.viewport[2] == 1920);
    assert(frame.world.surfaces == 1234);
    assert(strcmp(frame.world.model_name, "maps/c1a0.bsp") == 0);
    assert(frame.entity_count == 1 && frame.entities[0].index == 7);
    assert(frame.scene_clears == 1);
    assert(strcmp(frame.entities[0].model_name, "models/barney.mdl") == 0);
    assert(frame.command_2d_count == 1 &&
           frame.commands_2d[0].texture == 33);
    assert(ref_agc_live_take_latest(&store, frame.serial, &frame) == 1);

    ref_agc_live_begin_frame(&store, 0, 44);
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

    strcpy(world.model_name, "maps/c1a1.bsp");
    ref_agc_live_set_world(&store, &world);
    ref_agc_live_begin_frame(&store, 1, 46);
    assert(ref_agc_live_publish(&store, 47) == 0);
    assert(ref_agc_live_take_latest(&store, 2, &frame) == 0);
    assert(frame.serial == 3 && frame.map_serial == 2);
    assert(frame.world.serial == 2);
    assert(strcmp(frame.world.model_name, "maps/c1a1.bsp") == 0);

    ref_agc_live_store_destroy(&store);
    puts("ref_agc live frame tests passed");
    return 0;
}
