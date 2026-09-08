#ifndef REF_AGC_TEXTURE_STORE_H
#define REF_AGC_TEXTURE_STORE_H

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

enum {
    REF_AGC_TEXTURE_MAX = 4096,
    REF_AGC_TEXTURE_NAME_MAX = 128,
};

typedef enum RefAgcTextureResult {
    REF_AGC_TEXTURE_OK = 0,
    REF_AGC_TEXTURE_INVALID = -1,
    REF_AGC_TEXTURE_NOT_FOUND = -2,
    REF_AGC_TEXTURE_FULL = -3,
    REF_AGC_TEXTURE_NO_MEMORY = -4,
    REF_AGC_TEXTURE_TOO_SMALL = -5,
    REF_AGC_TEXTURE_VISITOR_FAILED = -6,
} RefAgcTextureResult;

typedef void *(*RefAgcTextureAllocFn)(size_t bytes, void *user);
typedef void (*RefAgcTextureFreeFn)(void *memory, void *user);

typedef struct RefAgcTextureAllocator {
    RefAgcTextureAllocFn alloc;
    RefAgcTextureFreeFn free;
    void *user;
} RefAgcTextureAllocator;

typedef struct RefAgcTextureInput {
    const char *name;
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    uint32_t format;
    uint32_t flags;
    uint32_t mip_count;
    int sampler_clamp;
    const void *pixels;
    size_t pixel_bytes;
} RefAgcTextureInput;

typedef struct RefAgcTextureView {
    uint32_t handle;
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    uint32_t format;
    uint32_t flags;
    uint32_t mip_count;
    int sampler_clamp;
    uint64_t revision;
    uint64_t content_hash;
    size_t pixel_bytes;
    const uint8_t *pixels;
    const char *name;
    int active;
} RefAgcTextureView;

typedef struct RefAgcTextureStats {
    uint64_t revision;
    uint64_t creates;
    uint64_t updates;
    uint64_t frees;
    uint32_t handles_issued;
    uint32_t active;
    uint32_t peak_active;
    size_t resident_bytes;
    size_t peak_resident_bytes;
} RefAgcTextureStats;

typedef int (*RefAgcTextureVisitor)(const RefAgcTextureView *view, void *user);

typedef struct RefAgcTextureEntry {
    char name[REF_AGC_TEXTURE_NAME_MAX];
    uint8_t *pixels;
    size_t pixel_bytes;
    uint64_t revision;
    uint64_t content_hash;
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    uint32_t format;
    uint32_t flags;
    uint32_t mip_count;
    int sampler_clamp;
    int active;
} RefAgcTextureEntry;

typedef struct RefAgcTextureStore {
    pthread_mutex_t lock;
    RefAgcTextureAllocator allocator;
    RefAgcTextureEntry entries[REF_AGC_TEXTURE_MAX];
    RefAgcTextureStats stats;
    int initialized;
} RefAgcTextureStore;

int ref_agc_texture_store_init(RefAgcTextureStore *store,
                               const RefAgcTextureAllocator *allocator);
void ref_agc_texture_store_destroy(RefAgcTextureStore *store);

int ref_agc_texture_store_upsert(RefAgcTextureStore *store,
                                 const RefAgcTextureInput *input,
                                 int update, uint32_t *out_handle);
int ref_agc_texture_store_free(RefAgcTextureStore *store, uint32_t handle);
int ref_agc_texture_store_find(RefAgcTextureStore *store, const char *name,
                               uint32_t *out_handle);
int ref_agc_texture_store_get(RefAgcTextureStore *store, uint32_t handle,
                              RefAgcTextureView *out);
int ref_agc_texture_store_copy_pixels(RefAgcTextureStore *store,
                                      uint32_t handle, void *destination,
                                      size_t capacity, size_t *out_bytes);
int ref_agc_texture_store_visit_changed(RefAgcTextureStore *store,
                                        uint64_t after_revision,
                                        RefAgcTextureVisitor visitor,
                                        void *user,
                                        uint64_t *out_revision);
int ref_agc_texture_store_stats(RefAgcTextureStore *store,
                                RefAgcTextureStats *out);

#endif
