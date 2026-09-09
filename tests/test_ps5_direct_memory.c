#include "../src/ps5_direct_memory.h"

#include <assert.h>
#include <stdint.h>

static int step;
static int fail_at;
static int releases;
static unsigned char arena[0x4000];

static int reserve_virtual(void **address, size_t bytes, int flags,
                           size_t alignment)
{
    assert(++step == 1 && bytes == sizeof(arena) && flags == 0);
    assert(alignment == sizeof(arena));
    *address = arena;
    return fail_at == step ? -1 : 0;
}
static int allocate_direct(size_t bytes, size_t alignment, int type,
                           int64_t *offset)
{
    assert(++step == 2 && bytes == sizeof(arena));
    assert(alignment == sizeof(arena) && type == 3);
    *offset = 0x8000;
    return fail_at == step ? -1 : 0;
}
static int map_direct(void **address, size_t bytes, int protection,
                      int flags, int64_t offset, size_t alignment)
{
    assert(++step == 3 && *address == arena && bytes == sizeof(arena));
    assert(protection == 0x33 && flags == 0 && offset == 0x8000);
    assert(alignment == sizeof(arena));
    return fail_at == step ? -1 : 0;
}
static int unmap_direct(void *address, size_t bytes)
{
    assert(++step == 4 && address == arena && bytes == sizeof(arena));
    return fail_at == step ? -1 : 0;
}
static int release_direct(int64_t offset, size_t bytes)
{
    ++releases;
    assert(offset == 0x8000 && bytes == sizeof(arena));
    if (step < 4)
        ++step;
    else
        assert(++step == 5);
    return fail_at == step ? -1 : 0;
}

static int loose_alloc_fail, loose_map_fail, loose_release_fail, loose_null_map;
static int loose_alloc_calls, loose_map_calls, loose_release_calls;
static int loose_allocate(size_t bytes, size_t alignment, int type, int64_t *offset)
{
    assert(bytes == sizeof(arena) && alignment == sizeof(arena) && type == 12);
    ++loose_alloc_calls;
    *offset = 0x8000;
    return loose_alloc_fail ? -1 : 0;
}
static int loose_map(void **address, size_t bytes, int protection, int flags,
                     int64_t offset, size_t alignment)
{
    assert(!*address && bytes == sizeof(arena) && protection == 0x33 && !flags);
    assert(offset == 0x8000 && alignment == sizeof(arena));
    ++loose_map_calls;
    *address = loose_null_map ? NULL : arena;
    return loose_map_fail ? -1 : 0;
}
static int loose_release(int64_t offset, size_t bytes)
{
    assert(offset == 0x8000 && bytes == sizeof(arena));
    ++loose_release_calls;
    return loose_release_fail ? -1 : 0;
}
static void test_allocate_map(void)
{
    const struct ps5_direct_memory_ops ops = {
        NULL, loose_allocate, loose_map, NULL, loose_release
    };
    struct ps5_direct_memory memory;
    for (int scenario = 0; scenario < 5; ++scenario) {
        loose_alloc_fail = scenario == 1;
        loose_map_fail = scenario == 2 || scenario == 3;
        loose_release_fail = scenario == 3;
        loose_null_map = scenario == 4;
        loose_alloc_calls = loose_map_calls = loose_release_calls = 0;
        int result = ps5_direct_memory_allocate_map(
            &memory, &ops, sizeof(arena), sizeof(arena), 12, 0x33);
        assert(loose_alloc_calls == 1);
        assert(loose_map_calls == (scenario != 1));
        assert(loose_release_calls == (scenario >= 2));
        if (scenario == 0) {
            assert(result == 0 && memory.allocated && memory.mapped && !memory.retain);
        } else if (scenario == 1) {
            assert(result == PS5_DIRECT_MEMORY_ALLOCATE_FAILED);
            assert(!memory.allocated && !memory.mapped);
        } else if (scenario == 3) {
            assert(result == PS5_DIRECT_MEMORY_RELEASE_FAILED);
            assert(memory.allocated && !memory.mapped && memory.retain);
            assert(memory.offset == 0x8000); /* retain exact cleanup identity */
        } else {
            assert(result == PS5_DIRECT_MEMORY_MAP_FAILED);
            assert(!memory.allocated && !memory.mapped && !memory.retain);
            assert(memory.offset == -1 && memory.address == NULL);
        }
    }
}

int main(void)
{
    test_allocate_map();
    const struct ps5_direct_memory_ops ops = {
        reserve_virtual, allocate_direct, map_direct,
        unmap_direct, release_direct,
    };
    struct ps5_direct_memory memory;
    step = fail_at = releases = 0;
    assert(ps5_direct_memory_open(&memory, &ops, sizeof(arena), sizeof(arena),
                                  3, 0x33) == PS5_DIRECT_MEMORY_OK);
    assert(memory.mapped && memory.allocated && !memory.retain);
    assert(ps5_direct_memory_close(&memory, &ops, 0) ==
           PS5_DIRECT_MEMORY_OWNED_BY_GPU);
    assert(memory.mapped && memory.allocated && memory.retain);

    step = fail_at = releases = 0;
    assert(ps5_direct_memory_open(&memory, &ops, sizeof(arena), sizeof(arena),
                                  3, 0x33) == PS5_DIRECT_MEMORY_OK);
    assert(ps5_direct_memory_close(&memory, &ops, 1) == PS5_DIRECT_MEMORY_OK);
    assert(!memory.mapped && !memory.allocated && releases == 1);

    step = releases = 0;
    fail_at = 3;
    assert(ps5_direct_memory_open(&memory, &ops, sizeof(arena), sizeof(arena),
                                  3, 0x33) == PS5_DIRECT_MEMORY_MAP_FAILED);
    assert(!memory.mapped && !memory.allocated && releases == 1);
    assert(ps5_direct_memory_open(&memory, &ops, 3u, 2u, 3, 0x33) ==
           PS5_DIRECT_MEMORY_PRECONDITION);
    return 0;
}
