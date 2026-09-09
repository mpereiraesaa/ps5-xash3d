#include "ref_agc_live_frame.h"

#include <math.h>
#include <string.h>

static void copy_name(char out[REF_AGC_LIVE_MODEL_NAME], const char *name)
{
    size_t length = 0u;
    if (!name) {
        out[0] = '\0';
        return;
    }
    while (length + 1u < REF_AGC_LIVE_MODEL_NAME && name[length])
        ++length;
    memcpy(out, name, length);
    out[length] = '\0';
}

int ref_agc_live_store_init(RefAgcLiveStore *store)
{
    if (!store)
        return -1;
    memset(store, 0, sizeof(*store));
    if (pthread_mutex_init(&store->publish_lock, NULL) != 0)
        return -2;
    if (pthread_cond_init(&store->frame_ready, NULL) != 0) {
        (void)pthread_mutex_destroy(&store->publish_lock);
        return -3;
    }
    if (pthread_cond_init(&store->frame_consumed, NULL) != 0) {
        (void)pthread_cond_destroy(&store->frame_ready);
        (void)pthread_mutex_destroy(&store->publish_lock);
        return -4;
    }
    store->initialized = 1;
    return 0;
}

void ref_agc_live_store_destroy(RefAgcLiveStore *store)
{
    if (!store || !store->initialized)
        return;
    (void)pthread_cond_destroy(&store->frame_consumed);
    (void)pthread_cond_destroy(&store->frame_ready);
    (void)pthread_mutex_destroy(&store->publish_lock);
    memset(store, 0, sizeof(*store));
}

void ref_agc_live_set_world(RefAgcLiveStore *store,
                            const RefAgcLiveWorld *world)
{
    if (!store || !store->initialized || !world)
        return;
    store->current_world = *world;
    copy_name(store->current_world.model_name, world->model_name);
    store->current_world.serial = ++store->next_map_serial;
    store->building.world = store->current_world;
    store->building.map_serial = store->current_world.serial;
}

void ref_agc_live_set_sky(
    RefAgcLiveStore *store,
    const uint32_t texture_handles[REF_AGC_LIVE_SKY_SIDES])
{
    if (!store || !store->initialized)
        return;
    if (texture_handles)
        for (uint32_t side = 0u; side < REF_AGC_LIVE_SKY_SIDES; ++side)
            if (texture_handles[side] == 0u)
                return;
    memset(store->current_sky.texture_handles, 0,
           sizeof(store->current_sky.texture_handles));
    store->current_sky.active = 0u;
    if (texture_handles) {
        for (uint32_t side = 0u; side < REF_AGC_LIVE_SKY_SIDES; ++side) {
            store->current_sky.texture_handles[side] =
                texture_handles[side];
        }
        store->current_sky.active = 1u;
    }
    ++store->current_sky.revision;
    store->building.sky = store->current_sky;
}

void ref_agc_live_begin_frame(RefAgcLiveStore *store, int clear_scene,
                              uint64_t begin_calls)
{
    if (!store || !store->initialized)
        return;
    store->building.serial = 0;
    store->building.world = store->current_world;
    store->building.sky = store->current_sky;
    store->building.map_serial = store->current_world.serial;
    memset(&store->building.view, 0, sizeof(store->building.view));
    store->building.command_2d_count = 0;
    store->building.dropped_2d_commands = 0;
    store->building.clear_scene = clear_scene != 0;
    store->building.begin_calls = begin_calls;
    store->building.scene_calls = 0;
    store->building.end_calls = 0;
    store->building.scene_clears =
        store->scene_serial != store->last_begin_scene_serial;
    store->last_begin_scene_serial = store->scene_serial;
}

void ref_agc_live_clear_scene(RefAgcLiveStore *store)
{
    if (!store || !store->initialized)
        return;
    store->building.entity_count = 0;
    store->building.dropped_entities = 0;
    store->scene_serial++;
    store->building.scene_clears++;
}

int ref_agc_live_add_entity(RefAgcLiveStore *store,
                            const RefAgcLiveEntity *entity)
{
    RefAgcLiveEntity *target;
    if (!store || !store->initialized || !entity)
        return -1;
    if (store->building.entity_count >= REF_AGC_LIVE_MAX_ENTITIES) {
        store->building.dropped_entities++;
        return -2;
    }
    target = &store->building.entities[store->building.entity_count++];
    *target = *entity;
    copy_name(target->model_name, entity->model_name);
    return 0;
}

void ref_agc_live_set_view(RefAgcLiveStore *store,
                           const RefAgcLiveView *view,
                           uint64_t scene_calls)
{
    if (!store || !store->initialized || !view)
        return;
    store->building.view = *view;
    store->building.view.valid = 1;
    store->building.scene_calls = scene_calls;
}

int ref_agc_live_add_2d(RefAgcLiveStore *store,
                        const RefAgcLive2DCommand *command)
{
    if (!store || !store->initialized || !command)
        return -1;
    if (store->building.command_2d_count >= REF_AGC_LIVE_MAX_2D_COMMANDS) {
        store->building.dropped_2d_commands++;
        return -2;
    }
    store->building.commands_2d[store->building.command_2d_count++] = *command;
    return 0;
}

int ref_agc_live_publish(RefAgcLiveStore *store, uint64_t end_calls)
{
    if (!store || !store->initialized)
        return -1;
    store->building.serial = ++store->next_frame_serial;
    store->building.end_calls = end_calls;
    if (pthread_mutex_lock(&store->publish_lock) != 0)
        return -2;
    if (store->stop_requested) {
        (void)pthread_mutex_unlock(&store->publish_lock);
        return store->stop_result != 0 ? store->stop_result : 1;
    }
    store->published = store->building;
    (void)pthread_cond_broadcast(&store->frame_ready);
    (void)pthread_mutex_unlock(&store->publish_lock);
    return 0;
}

int ref_agc_live_take_latest(RefAgcLiveStore *store, uint64_t after_serial,
                             RefAgcLiveFrame *out)
{
    int result = 0;
    if (!store || !store->initialized || !out)
        return -1;
    if (pthread_mutex_lock(&store->publish_lock) != 0)
        return -2;
    if (store->published.serial <= after_serial)
        result = 1;
    else
        *out = store->published;
    (void)pthread_mutex_unlock(&store->publish_lock);
    return result;
}

int ref_agc_live_wait_latest(RefAgcLiveStore *store, uint64_t after_serial,
                             RefAgcLiveFrame *out)
{
    int result = 0;
    if (!store || !store->initialized || !out)
        return -1;
    if (pthread_mutex_lock(&store->publish_lock) != 0)
        return -2;
    while (store->published.serial <= after_serial &&
           !store->stop_requested) {
        if (pthread_cond_wait(&store->frame_ready,
                              &store->publish_lock) != 0) {
            result = -3;
            break;
        }
    }
    if (result == 0) {
        if (store->published.serial > after_serial)
            *out = store->published;
        else
            result = store->stop_result != 0 ? store->stop_result : 1;
    }
    (void)pthread_mutex_unlock(&store->publish_lock);
    return result;
}

int ref_agc_live_mark_consumed(RefAgcLiveStore *store, uint64_t serial)
{
    int result = 0;
    if (!store || !store->initialized || serial == 0u)
        return -1;
    if (pthread_mutex_lock(&store->publish_lock) != 0)
        return -2;
    if (serial > store->published.serial || serial < store->consumed_serial)
        result = -3;
    else {
        store->consumed_serial = serial;
        (void)pthread_cond_broadcast(&store->frame_consumed);
    }
    (void)pthread_mutex_unlock(&store->publish_lock);
    return result;
}

int ref_agc_live_wait_consumed(RefAgcLiveStore *store, uint64_t serial)
{
    int result = 0;
    if (!store || !store->initialized || serial == 0u)
        return -1;
    if (pthread_mutex_lock(&store->publish_lock) != 0)
        return -2;
    if (serial > store->published.serial)
        result = -3;
    while (result == 0 && store->consumed_serial < serial &&
           !store->stop_requested) {
        if (pthread_cond_wait(&store->frame_consumed,
                              &store->publish_lock) != 0)
            result = -4;
    }
    if (result == 0 && store->consumed_serial < serial)
        result = store->stop_result != 0 ? store->stop_result : 1;
    (void)pthread_mutex_unlock(&store->publish_lock);
    return result;
}

int ref_agc_live_request_stop(RefAgcLiveStore *store, int result)
{
    if (!store || !store->initialized)
        return -1;
    if (pthread_mutex_lock(&store->publish_lock) != 0)
        return -2;
    store->stop_requested = 1;
    if (result != 0 && store->stop_result == 0)
        store->stop_result = result;
    (void)pthread_cond_broadcast(&store->frame_ready);
    (void)pthread_cond_broadcast(&store->frame_consumed);
    (void)pthread_mutex_unlock(&store->publish_lock);
    return 0;
}

int ref_agc_live_view_camera(const RefAgcLiveView *view,
                             float position[3], float forward[3],
                             float *aspect_ratio)
{
    const float degrees_to_radians =
        3.14159265358979323846f / 180.0f;
    float pitch;
    float yaw;
    float cp;
    float sp;
    float cy;
    float sy;
    if (!view || !position || !forward || !aspect_ratio || !view->valid ||
        view->viewport[2] <= 0 || view->viewport[3] <= 0 ||
        !isfinite(view->origin[0]) || !isfinite(view->origin[1]) ||
        !isfinite(view->origin[2]) || !isfinite(view->angles[0]) ||
        !isfinite(view->angles[1]) || !isfinite(view->angles[2]))
        return -1;
    pitch = view->angles[0] * degrees_to_radians;
    yaw = view->angles[1] * degrees_to_radians;
    cp = cosf(pitch);
    sp = sinf(pitch);
    cy = cosf(yaw);
    sy = sinf(yaw);
    /* Match bake_bsp.py's GoldSrc Z-up -> AGC Y-up conversion exactly:
     * (x, y, z) -> (x, z, -y). */
    position[0] = view->origin[0];
    position[1] = view->origin[2];
    position[2] = -view->origin[1];
    forward[0] = cp * cy;
    forward[1] = -sp;
    forward[2] = -cp * sy;
    *aspect_ratio = (float)view->viewport[2] / (float)view->viewport[3];
    return isfinite(*aspect_ratio) && *aspect_ratio > 0.0f ? 0 : -1;
}

int ref_agc_live_world_view_ready(const RefAgcLiveFrame *frame)
{
    return frame && frame->map_serial != 0u &&
        frame->world.surfaces != 0u && frame->view.valid &&
        (frame->view.flags & REF_AGC_LIVE_RF_DRAW_WORLD) != 0u &&
        frame->view.viewport[2] > 0 && frame->view.viewport[3] > 0;
}
