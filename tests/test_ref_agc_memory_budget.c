#include "ref_agc_memory_budget.h"

#include <assert.h>
#include <stdio.h>

#define MIB UINT64_C(1048576)

int main(void)
{
    RefAgcMemoryObservation observation = {2048 * MIB, 1536 * MIB, 1};
    RefAgcMemoryBudgetRequest request = {0, 256 * MIB, 128 * MIB,
                                       16 * MIB, 50, 65536};
    RefAgcMemoryBudget budget;
    assert(ref_agc_memory_budget_plan(&observation, &request, &budget) == 0);
    assert(budget.texture_bytes == 832 * MIB);
    assert(budget.heap_bytes == 960 * MIB);
    assert(budget.remaining_bytes == 1088 * MIB);

    /* Fragmentation limits the shared contiguous heap, not just textures. */
    observation.largest_block_bytes = 256 * MIB;
    assert(ref_agc_memory_budget_plan(&observation, &request, &budget) == 0);
    assert(budget.texture_bytes == 128 * MIB);
    request.texture_bytes = 256 * MIB;
    assert(ref_agc_memory_budget_plan(&observation, &request, &budget) ==
           REF_AGC_MEMORY_BUDGET_INSUFFICIENT);
    assert(budget.texture_bytes == 0 && budget.heap_bytes == 0);
    request.texture_bytes = 80 * MIB; /* compatibility regression, not a cap */
    assert(ref_agc_memory_budget_plan(&observation, &request, &budget) == 0);
    assert(budget.texture_bytes == 80 * MIB);

    observation.valid = 0;
    assert(ref_agc_memory_budget_plan(&observation, &request, &budget) ==
           REF_AGC_MEMORY_BUDGET_UNMEASURED);
    observation.valid = 1;
    observation.free_bytes = 100 * MIB;
    observation.largest_block_bytes = 100 * MIB;
    assert(ref_agc_memory_budget_plan(&observation, &request, &budget) ==
           REF_AGC_MEMORY_BUDGET_INSUFFICIENT);
    observation.largest_block_bytes++;
    assert(ref_agc_memory_budget_plan(&observation, &request, &budget) ==
           REF_AGC_MEMORY_BUDGET_INVALID);

    observation.free_bytes = observation.largest_block_bytes = UINT64_MAX;
    request.texture_bytes = 0;
    request.auto_percent = 100;
    assert(ref_agc_memory_budget_plan(&observation, &request, &budget) == 0);
    assert(budget.remaining_bytes >= request.reserve_bytes);
    assert(budget.texture_bytes % request.alignment == 0);
    request.texture_bytes = UINT64_MAX;
    assert(ref_agc_memory_budget_plan(&observation, &request, &budget) ==
           REF_AGC_MEMORY_BUDGET_INVALID);
    request.texture_bytes = 0;
    request.alignment = 3;
    assert(ref_agc_memory_budget_plan(&observation, &request, &budget) ==
           REF_AGC_MEMORY_BUDGET_INVALID);
    assert(ref_agc_memory_budget_plan(NULL, NULL, &budget) ==
           REF_AGC_MEMORY_BUDGET_INVALID);
    assert(budget.texture_bytes == 0);
    puts("ref_agc memory budget tests passed");
    return 0;
}
