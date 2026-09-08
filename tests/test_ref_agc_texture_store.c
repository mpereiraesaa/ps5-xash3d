#include "ref_agc_texture_store.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct TestAllocator {
    unsigned attempts;
    unsigned allocations;
    unsigned frees;
    int fail_next;
} TestAllocator;

typedef struct Visits {
    uint32_t handles[8];
    uint64_t revisions[8];
    int active[8];
    unsigned count;
    int fail_at;
} Visits;

static void *test_alloc(size_t bytes, void *user)
{
    TestAllocator *allocator = (TestAllocator *)user;
    ++allocator->attempts;
    if (allocator->fail_next) {
        allocator->fail_next = 0;
        return NULL;
    }
    ++allocator->allocations;
    return malloc(bytes);
}

static void test_free(void *memory, void *user)
{
    TestAllocator *allocator = (TestAllocator *)user;
    ++allocator->frees;
    free(memory);
}

static int visit(const RefAgcTextureView *view, void *user)
{
    Visits *visits = (Visits *)user;
    if (visits->fail_at >= 0 && (int)visits->count == visits->fail_at)
        return -1;
    assert(visits->count < 8u);
    visits->handles[visits->count] = view->handle;
    visits->revisions[visits->count] = view->revision;
    visits->active[visits->count] = view->active;
    ++visits->count;
    return 0;
}

int main(void)
{
    RefAgcTextureStore store;
    TestAllocator allocator_state = {0};
    const RefAgcTextureAllocator allocator = {
        test_alloc, test_free, &allocator_state,
    };
    const uint8_t checker[] = {1, 2, 3, 4, 5, 6, 7, 8};
    const uint8_t update[] = {9, 10, 11, 12};
    RefAgcTextureInput input = {
        .name = "#c1a0:labwall.mip",
        .width = 2,
        .height = 1,
        .depth = 1,
        .format = 3,
        .flags = 0x1200,
        .mip_count = 1,
        .pixels = checker,
        .pixel_bytes = sizeof(checker),
    };
    RefAgcTextureView view;
    RefAgcTextureStats stats;
    uint32_t first = 0, duplicate = 0, blank = 0;
    uint8_t copied[8] = {0};
    size_t copied_bytes = 0;

    assert(ref_agc_texture_store_init(&store, &allocator) == 0);
    assert(ref_agc_texture_store_upsert(&store, &input, 0, &first) == 0);
    assert(first == 1u);
    assert(ref_agc_texture_store_upsert(
        &store, &input, 0, &duplicate) == 0 && duplicate == first);
    assert(allocator_state.allocations == 1u);
    assert(ref_agc_texture_store_get(&store, first, &view) == 0);
    assert(strcmp(view.name, input.name) == 0 && view.width == 2u);
    assert(view.pixel_bytes == sizeof(checker) && view.active);
    assert(ref_agc_texture_store_copy_pixels(
        &store, first, copied, sizeof(copied) - 1u, &copied_bytes) ==
        REF_AGC_TEXTURE_TOO_SMALL);
    assert(copied_bytes == sizeof(checker));
    assert(ref_agc_texture_store_copy_pixels(
        &store, first, copied, sizeof(copied), &copied_bytes) == 0);
    assert(memcmp(copied, checker, sizeof(checker)) == 0);

    input.width = 1;
    input.pixels = update;
    input.pixel_bytes = sizeof(update);
    assert(ref_agc_texture_store_upsert(&store, &input, 1, &duplicate) == 0);
    assert(duplicate == first && allocator_state.frees == 1u);
    assert(ref_agc_texture_store_get(&store, first, &view) == 0);
    const uint64_t updated_hash = view.content_hash;
    assert(view.width == 1u && view.pixel_bytes == sizeof(update));

    input.name = "*blank";
    input.width = 2;
    input.height = 2;
    input.pixels = NULL;
    input.pixel_bytes = 16;
    assert(ref_agc_texture_store_upsert(&store, &input, 0, &blank) == 0);
    assert(blank == 2u);
    memset(copied, 0xff, sizeof(copied));
    assert(ref_agc_texture_store_copy_pixels(
        &store, blank, copied, sizeof(copied), &copied_bytes) ==
        REF_AGC_TEXTURE_TOO_SMALL);
    uint8_t blank_pixels[16];
    memset(blank_pixels, 0xff, sizeof(blank_pixels));
    assert(ref_agc_texture_store_copy_pixels(
        &store, blank, blank_pixels, sizeof(blank_pixels), &copied_bytes) == 0);
    for (unsigned i = 0; i < sizeof(blank_pixels); ++i)
        assert(blank_pixels[i] == 0u);

    Visits visits = {.fail_at = -1};
    uint64_t visited_revision = 0;
    assert(ref_agc_texture_store_visit_changed(
        &store, 0, visit, &visits, &visited_revision) == 0);
    assert(visits.count == 2u && visits.handles[0] == first);
    assert(visits.revisions[0] == 2u && visits.handles[1] == blank);
    assert(visited_revision == 3u);

    allocator_state.fail_next = 1;
    input.name = "#missing:update";
    assert(ref_agc_texture_store_upsert(&store, &input, 1, &duplicate) ==
           REF_AGC_TEXTURE_NOT_FOUND);
    input.name = "#c1a0:labwall.mip";
    assert(ref_agc_texture_store_upsert(&store, &input, 1, &duplicate) ==
           REF_AGC_TEXTURE_NO_MEMORY);
    assert(ref_agc_texture_store_get(&store, first, &view) == 0);
    assert(view.content_hash == updated_hash && view.revision == 2u);

    assert(ref_agc_texture_store_free(&store, first) == 0);
    assert(ref_agc_texture_store_free(&store, first) ==
           REF_AGC_TEXTURE_NOT_FOUND);
    assert(ref_agc_texture_store_get(&store, first, &view) ==
           REF_AGC_TEXTURE_NOT_FOUND);
    memset(&visits, 0, sizeof(visits));
    visits.fail_at = -1;
    assert(ref_agc_texture_store_visit_changed(
        &store, 3u, visit, &visits, &visited_revision) == 0);
    assert(visits.count == 1u && visits.handles[0] == first &&
           visits.revisions[0] == 4u && !visits.active[0]);
    assert(ref_agc_texture_store_visit_changed(
        &store, 5u, visit, &visits, &visited_revision) ==
        REF_AGC_TEXTURE_INVALID);

    assert(ref_agc_texture_store_stats(&store, &stats) == 0);
    assert(stats.revision == 4u && stats.creates == 2u &&
           stats.updates == 1u && stats.frees == 1u);
    assert(stats.handles_issued == 2u && stats.active == 1u &&
           stats.peak_active == 2u &&
           stats.resident_bytes == 16u && stats.peak_resident_bytes == 20u);

    memset(&visits, 0, sizeof(visits));
    visits.fail_at = 0;
    assert(ref_agc_texture_store_visit_changed(
        &store, 0u, visit, &visits, &visited_revision) ==
        REF_AGC_TEXTURE_VISITOR_FAILED);
    assert(visited_revision == 0u);

    ref_agc_texture_store_destroy(&store);
    assert(allocator_state.attempts == allocator_state.allocations + 1u);
    assert(allocator_state.allocations == allocator_state.frees);
    puts("ref_agc texture store tests passed");
    return 0;
}
