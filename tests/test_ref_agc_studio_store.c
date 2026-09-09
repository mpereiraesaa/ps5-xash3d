#include "../src/ref_agc_studio_store.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct VisitState {
    uint32_t handles[8];
    uint64_t revisions[8];
    int active[8];
    uint32_t count;
} VisitState;

static int visit(const RefAgcStudioView *view, void *user)
{
    VisitState *state = user;
    assert(view && state && state->count < 8u);
    state->handles[state->count] = view->handle;
    state->revisions[state->count] = view->revision;
    state->active[state->count] = view->active;
    if (view->active) {
        assert(view->data && view->bytes == 4u);
        assert(strcmp(view->model_name, "models/a.mdl") == 0 ||
               strcmp(view->model_name, "models/b.mdl") == 0);
    } else {
        assert(!view->data && view->bytes == 0u);
    }
    ++state->count;
    return 0;
}

int main(void)
{
    RefAgcStudioStore store;
    const uint8_t first[] = {1u, 2u, 3u, 4u};
    const uint8_t update[] = {5u, 6u, 7u, 8u};
    const uint8_t second[] = {9u, 10u, 11u, 12u};
    uint32_t first_handle = 0u, second_handle = 0u, reused_handle = 0u;
    uint64_t revision = 0u;
    VisitState visits = {0};
    RefAgcStudioStats stats;

    assert(ref_agc_studio_store_init(&store, NULL) == REF_AGC_STUDIO_OK);
    assert(ref_agc_studio_store_upsert(
        &store, &(RefAgcStudioInput){"models/a.mdl", first, sizeof(first)},
        &first_handle) == REF_AGC_STUDIO_OK);
    assert(first_handle == 1u);
    assert(ref_agc_studio_store_upsert(
        &store, &(RefAgcStudioInput){"models/a.mdl", update, sizeof(update)},
        &reused_handle) == REF_AGC_STUDIO_OK);
    assert(reused_handle == first_handle);
    assert(ref_agc_studio_store_upsert(
        &store, &(RefAgcStudioInput){"models/b.mdl", second, sizeof(second)},
        &second_handle) == REF_AGC_STUDIO_OK);
    assert(second_handle == 2u);
    assert(ref_agc_studio_store_visit_changed(
        &store, 0u, visit, &visits, &revision) == REF_AGC_STUDIO_OK);
    assert(revision == 3u && visits.count == 2u);
    assert(visits.handles[0] == first_handle && visits.revisions[0] == 2u);
    assert(visits.handles[1] == second_handle && visits.revisions[1] == 3u);

    assert(ref_agc_studio_store_free_name(&store, "models/a.mdl") ==
           REF_AGC_STUDIO_OK);
    memset(&visits, 0, sizeof(visits));
    assert(ref_agc_studio_store_visit_changed(
        &store, revision, visit, &visits, &revision) == REF_AGC_STUDIO_OK);
    assert(revision == 4u && visits.count == 1u &&
           visits.handles[0] == first_handle && !visits.active[0]);
    assert(ref_agc_studio_store_upsert(
        &store, &(RefAgcStudioInput){"models/a.mdl", first, sizeof(first)},
        &reused_handle) == REF_AGC_STUDIO_OK);
    assert(reused_handle == first_handle);
    assert(ref_agc_studio_store_stats(&store, &stats) ==
           REF_AGC_STUDIO_OK);
    assert(stats.revision == 5u && stats.creates == 3u &&
           stats.updates == 1u && stats.frees == 1u && stats.active == 2u &&
           stats.peak_active == 2u && stats.resident_bytes == 8u &&
           stats.peak_resident_bytes == 8u);
    ref_agc_studio_store_destroy(&store);
    puts("ref_agc studio store tests passed");
    return 0;
}
