#ifndef PS5_XASH3D_REF_AGC_STUDIO_STORE_H
#define PS5_XASH3D_REF_AGC_STUDIO_STORE_H

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

enum {
    REF_AGC_STUDIO_MAX = 512,
    REF_AGC_STUDIO_NAME_MAX = 64,
};

typedef enum RefAgcStudioResult {
    REF_AGC_STUDIO_OK = 0,
    REF_AGC_STUDIO_INVALID = -1,
    REF_AGC_STUDIO_NOT_FOUND = -2,
    REF_AGC_STUDIO_FULL = -3,
    REF_AGC_STUDIO_NO_MEMORY = -4,
    REF_AGC_STUDIO_VISITOR_FAILED = -5,
} RefAgcStudioResult;

typedef void *(*RefAgcStudioAllocFn)(size_t bytes, void *user);
typedef void (*RefAgcStudioFreeFn)(void *memory, void *user);

typedef struct RefAgcStudioAllocator {
    RefAgcStudioAllocFn alloc;
    RefAgcStudioFreeFn free;
    void *user;
} RefAgcStudioAllocator;

typedef struct RefAgcStudioInput {
    const char *model_name;
    const void *data;
    size_t bytes;
} RefAgcStudioInput;

/* Data is an owned, immutable copy of the engine-decoded Studio header.
 * Pointers in a view remain valid only for the duration of its visitor. */
typedef struct RefAgcStudioView {
    uint32_t handle;
    uint64_t revision;
    uint64_t content_hash;
    const char *model_name;
    const uint8_t *data;
    size_t bytes;
    int active;
} RefAgcStudioView;

typedef struct RefAgcStudioStats {
    uint64_t revision;
    uint64_t creates;
    uint64_t updates;
    uint64_t frees;
    uint32_t handles_issued;
    uint32_t active;
    uint32_t peak_active;
    size_t resident_bytes;
    size_t peak_resident_bytes;
} RefAgcStudioStats;

typedef int (*RefAgcStudioVisitor)(const RefAgcStudioView *view, void *user);

typedef struct RefAgcStudioEntry {
    char model_name[REF_AGC_STUDIO_NAME_MAX];
    uint8_t *data;
    size_t bytes;
    uint64_t revision;
    uint64_t content_hash;
    int active;
} RefAgcStudioEntry;

typedef struct RefAgcStudioStore {
    pthread_mutex_t lock;
    RefAgcStudioAllocator allocator;
    RefAgcStudioEntry entries[REF_AGC_STUDIO_MAX];
    RefAgcStudioStats stats;
    int initialized;
} RefAgcStudioStore;

int ref_agc_studio_store_init(RefAgcStudioStore *store,
                              const RefAgcStudioAllocator *allocator);
void ref_agc_studio_store_destroy(RefAgcStudioStore *store);
int ref_agc_studio_store_upsert(RefAgcStudioStore *store,
                                const RefAgcStudioInput *input,
                                uint32_t *out_handle);
int ref_agc_studio_store_free(RefAgcStudioStore *store, uint32_t handle);
int ref_agc_studio_store_free_name(RefAgcStudioStore *store,
                                   const char *model_name);
int ref_agc_studio_store_find(RefAgcStudioStore *store,
                              const char *model_name,
                              uint32_t *out_handle);
int ref_agc_studio_store_visit_changed(RefAgcStudioStore *store,
                                       uint64_t after_revision,
                                       RefAgcStudioVisitor visitor,
                                       void *user,
                                       uint64_t *out_revision);
int ref_agc_studio_store_stats(RefAgcStudioStore *store,
                               RefAgcStudioStats *out);

#endif
