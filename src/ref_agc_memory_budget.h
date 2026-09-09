#ifndef REF_AGC_MEMORY_BUDGET_H
#define REF_AGC_MEMORY_BUDGET_H

#include <stddef.h>
#include <stdint.h>

/* A successful platform observation, not total installed RAM. All byte
 * counts refer to the same allocatable direct-memory domain. */
typedef struct RefAgcMemoryObservation {
    uint64_t free_bytes;
    uint64_t largest_block_bytes;
    int valid;
} RefAgcMemoryObservation;

typedef struct RefAgcMemoryBudgetRequest {
    uint64_t texture_bytes; /* zero selects automatic capacity */
    uint64_t reserve_bytes; /* left available for allocations outside this heap */
    uint64_t fixed_heap_bytes; /* other renderer resources, including padding */
    uint64_t minimum_texture_bytes;
    uint32_t auto_percent; /* share of eligible free memory, 1..100 */
    uint64_t alignment;
} RefAgcMemoryBudgetRequest;

typedef struct RefAgcMemoryBudget {
    uint64_t texture_bytes;
    uint64_t heap_bytes;
    uint64_t remaining_bytes;
    uint64_t texture_limit_bytes;
} RefAgcMemoryBudget;

enum RefAgcMemoryBudgetResult {
    REF_AGC_MEMORY_BUDGET_OK = 0,
    REF_AGC_MEMORY_BUDGET_INVALID = -1,
    REF_AGC_MEMORY_BUDGET_UNMEASURED = -2,
    REF_AGC_MEMORY_BUDGET_INSUFFICIENT = -3
};

/* Does not allocate or promise that an observation remains current. A real
 * allocation must still succeed; never silently shrink an explicit request. */
int ref_agc_memory_budget_plan(const RefAgcMemoryObservation *observation,
                              const RefAgcMemoryBudgetRequest *request,
                              RefAgcMemoryBudget *out);

#endif
