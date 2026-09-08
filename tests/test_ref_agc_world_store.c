#include "ref_agc_world_store.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct AllocatorState {
    unsigned attempts;
    unsigned allocations;
    unsigned frees;
    unsigned fail_at;
} AllocatorState;

typedef struct VisitState {
    unsigned calls;
    int fail;
    RefAgcWorldView view;
} VisitState;

static void *test_alloc(size_t bytes, void *user)
{
    AllocatorState *state = user;
    ++state->attempts;
    if (state->fail_at != 0u && state->attempts == state->fail_at)
        return NULL;
    ++state->allocations;
    return malloc(bytes);
}

static void test_free(void *memory, void *user)
{
    AllocatorState *state = user;
    ++state->frees;
    free(memory);
}

static int visit(const RefAgcWorldView *view, void *user)
{
    VisitState *state = user;
    ++state->calls;
    state->view = *view;
    return state->fail ? -1 : 0;
}

int main(void)
{
    RefAgcWorldStore store;
    AllocatorState allocation = {0};
    RefAgcWorldAllocator allocator = {test_alloc, test_free, &allocation};
    RefAgcWorldVertex vertices[4] = {
        {{0, 0, 0}, {0, 0}, {0, 0}, 7},
        {{1, 0, 0}, {1, 0}, {1, 0}, 7},
        {{1, 1, 0}, {1, 1}, {1, 1}, 7},
        {{0, 1, 0}, {0, 1}, {0, 1}, 7},
    };
    const uint32_t indices[6] = {0, 1, 2, 0, 2, 3};
    const RefAgcWorldDraw draws[1] = {
        {0, 6, 42, 7, 0x10, REF_AGC_WORLD_DRAW_ALPHA_TEST, {0, 0}},
    };
    RefAgcWorldInput input = {
        "maps/c1a0.bsp", 1u << 29, vertices, 4, indices, 6, draws, 1,
    };
    RefAgcWorldStats stats;
    VisitState visited = {0};
    uint64_t cursor = 0u;

    assert(ref_agc_world_store_init(&store, &allocator) == 0);
    assert(ref_agc_world_store_publish(&store, &input) == 0);
    memset(vertices, 0, sizeof(vertices));
    assert(ref_agc_world_store_visit_changed(
        &store, 0u, visit, &visited, &cursor) == 0);
    assert(cursor == 1u && visited.calls == 1u && visited.view.active);
    assert(strcmp(visited.view.model_name, "maps/c1a0.bsp") == 0);
    assert(visited.view.vertices[1].position[0] == 1.0f);
    assert(visited.view.indices[5] == 3u);
    assert(visited.view.draws[0].texture_handle == 42u);

    visited.fail = 1;
    cursor = 0u;
    assert(ref_agc_world_store_visit_changed(
        &store, 0u, visit, &visited, &cursor) ==
        REF_AGC_WORLD_VISITOR_FAILED);
    assert(cursor == 0u);
    visited.fail = 0;
    assert(ref_agc_world_store_visit_changed(
        &store, 2u, visit, &visited, &cursor) == REF_AGC_WORLD_INVALID);

    allocation.fail_at = allocation.attempts + 2u;
    assert(ref_agc_world_store_publish(&store, &input) ==
           REF_AGC_WORLD_NO_MEMORY);
    allocation.fail_at = 0u;
    assert(ref_agc_world_store_stats(&store, &stats) == 0);
    assert(stats.revision == 1u && stats.publishes == 1u && stats.active);
    assert(stats.vertex_count == 4u && stats.index_count == 6u &&
           stats.draw_count == 1u && stats.content_hash != 0u);

    assert(ref_agc_world_store_clear(&store, "wrong.bsp") ==
           REF_AGC_WORLD_NOT_FOUND);
    assert(ref_agc_world_store_clear(&store, "maps/c1a0.bsp") == 0);
    memset(&visited, 0, sizeof(visited));
    cursor = 1u;
    assert(ref_agc_world_store_visit_changed(
        &store, 1u, visit, &visited, &cursor) == 0);
    assert(cursor == 2u && visited.calls == 1u && !visited.view.active);
    assert(!visited.view.vertices && !visited.view.indices &&
           !visited.view.draws);
    assert(ref_agc_world_store_stats(&store, &stats) == 0);
    assert(stats.clears == 1u && stats.resident_bytes == 0u &&
           stats.peak_resident_bytes > 0u);

    ref_agc_world_store_destroy(&store);
    assert(allocation.allocations == allocation.frees);
    puts("ref_agc world store tests passed");
    return 0;
}
