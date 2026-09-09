#ifndef PS5_XASH3D_REF_AGC_WORLD_STORE_H
#define PS5_XASH3D_REF_AGC_WORLD_STORE_H

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

enum {
    REF_AGC_WORLD_NAME_MAX = 64,
    REF_AGC_WORLD_DRAW_ALPHA_TEST = 1u << 0,
    REF_AGC_WORLD_DRAW_SKY = 1u << 1,
    REF_AGC_WORLD_DRAW_TURB = 1u << 2,
    REF_AGC_WORLD_DRAW_LIGHTMAP = 1u << 3,
};

typedef enum RefAgcWorldResult {
    REF_AGC_WORLD_OK = 0,
    REF_AGC_WORLD_INVALID = -1,
    REF_AGC_WORLD_NO_MEMORY = -2,
    REF_AGC_WORLD_NOT_FOUND = -3,
    REF_AGC_WORLD_VISITOR_FAILED = -4,
} RefAgcWorldResult;

typedef void *(*RefAgcWorldAllocFn)(size_t bytes, void *user);
typedef void (*RefAgcWorldFreeFn)(void *memory, void *user);

typedef struct RefAgcWorldAllocator {
    RefAgcWorldAllocFn alloc;
    RefAgcWorldFreeFn free;
    void *user;
} RefAgcWorldAllocator;

/* Pointer-free copy of the vertex attributes required by the native BSP
 * shaders.  Positions already use AGC's Y-up convention. */
typedef struct RefAgcWorldVertex {
    float position[3];
    float base_uv[2];
    float light_uv[2];
    uint32_t surface_id;
} RefAgcWorldVertex;

typedef struct RefAgcWorldDraw {
    uint32_t first_index;
    uint32_t index_count;
    uint32_t texture_handle;
    uint32_t surface_id;
    uint32_t surface_flags;
    uint32_t draw_flags;
    uint32_t reserved[2];
} RefAgcWorldDraw;

typedef struct RefAgcWorldInput {
    const char *model_name;
    uint32_t model_flags;
    const RefAgcWorldVertex *vertices;
    uint32_t vertex_count;
    const uint32_t *indices;
    uint32_t index_count;
    const RefAgcWorldDraw *draws;
    uint32_t draw_count;
    const uint8_t *lightmap_pixels;
    uint32_t lightmap_width;
    uint32_t lightmap_height;
    uint32_t lightmap_row_pitch;
    size_t lightmap_pixel_bytes;
} RefAgcWorldInput;

/* Pointers in a view remain valid only for the duration of its visitor. */
typedef struct RefAgcWorldView {
    uint64_t revision;
    uint64_t content_hash;
    char model_name[REF_AGC_WORLD_NAME_MAX];
    uint32_t model_flags;
    const RefAgcWorldVertex *vertices;
    uint32_t vertex_count;
    const uint32_t *indices;
    uint32_t index_count;
    const RefAgcWorldDraw *draws;
    uint32_t draw_count;
    const uint8_t *lightmap_pixels;
    uint32_t lightmap_width;
    uint32_t lightmap_height;
    uint32_t lightmap_row_pitch;
    size_t lightmap_pixel_bytes;
    uint32_t lightmapped_draw_count;
    uint32_t sky_draw_count;
    uint32_t turbulent_draw_count;
    int active;
} RefAgcWorldView;

typedef struct RefAgcWorldStats {
    uint64_t revision;
    uint64_t publishes;
    uint64_t clears;
    uint64_t content_hash;
    uint32_t vertex_count;
    uint32_t index_count;
    uint32_t draw_count;
    uint32_t lightmap_width;
    uint32_t lightmap_height;
    uint32_t lightmap_row_pitch;
    size_t lightmap_pixel_bytes;
    uint32_t lightmapped_draw_count;
    uint32_t sky_draw_count;
    uint32_t turbulent_draw_count;
    size_t resident_bytes;
    size_t peak_resident_bytes;
    int active;
} RefAgcWorldStats;

typedef int (*RefAgcWorldVisitor)(const RefAgcWorldView *view, void *user);

typedef struct RefAgcWorldStore {
    pthread_mutex_t lock;
    RefAgcWorldAllocator allocator;
    RefAgcWorldVertex *vertices;
    uint32_t *indices;
    RefAgcWorldDraw *draws;
    uint8_t *lightmap_pixels;
    RefAgcWorldStats stats;
    char model_name[REF_AGC_WORLD_NAME_MAX];
    uint32_t model_flags;
    int initialized;
} RefAgcWorldStore;

int ref_agc_world_store_init(RefAgcWorldStore *store,
                             const RefAgcWorldAllocator *allocator);
void ref_agc_world_store_destroy(RefAgcWorldStore *store);
int ref_agc_world_store_publish(RefAgcWorldStore *store,
                                const RefAgcWorldInput *input);
int ref_agc_world_store_clear(RefAgcWorldStore *store,
                              const char *model_name);
int ref_agc_world_store_visit_changed(RefAgcWorldStore *store,
                                      uint64_t after_revision,
                                      RefAgcWorldVisitor visitor,
                                      void *user,
                                      uint64_t *out_revision);
int ref_agc_world_store_stats(RefAgcWorldStore *store,
                              RefAgcWorldStats *out);

#endif
