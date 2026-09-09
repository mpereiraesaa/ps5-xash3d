#include "ref_agc_memory_budget.h"

#include <string.h>

int ref_agc_memory_budget_plan(const RefAgcMemoryObservation *observation,
                              const RefAgcMemoryBudgetRequest *request,
                              RefAgcMemoryBudget *out)
{
    if (!out)
        return REF_AGC_MEMORY_BUDGET_INVALID;
    memset(out, 0, sizeof(*out));
    if (!observation || !request || !request->alignment ||
        (request->alignment & (request->alignment - 1u)) ||
        !request->minimum_texture_bytes || !request->auto_percent ||
        request->auto_percent > 100u ||
        request->fixed_heap_bytes % request->alignment ||
        request->minimum_texture_bytes % request->alignment ||
        request->texture_bytes % request->alignment)
        return REF_AGC_MEMORY_BUDGET_INVALID;
    if (!observation->valid)
        return REF_AGC_MEMORY_BUDGET_UNMEASURED;
    if (observation->largest_block_bytes > observation->free_bytes)
        return REF_AGC_MEMORY_BUDGET_INVALID;
    if (request->reserve_bytes > observation->free_bytes ||
        request->fixed_heap_bytes > observation->free_bytes - request->reserve_bytes ||
        request->fixed_heap_bytes > observation->largest_block_bytes)
        return REF_AGC_MEMORY_BUDGET_INSUFFICIENT;

    const uint64_t eligible = observation->free_bytes - request->reserve_bytes -
                              request->fixed_heap_bytes;
    const uint64_t contiguous = observation->largest_block_bytes -
                                request->fixed_heap_bytes;
    const uint64_t limit = (eligible < contiguous ? eligible : contiguous) &
                           ~(request->alignment - 1u);
    /* Quotient/remainder avoids overflow even for UINT64_MAX observations. */
    uint64_t selected = request->texture_bytes;
    if (!selected) {
        selected = (eligible / 100u) * request->auto_percent +
                   ((eligible % 100u) * request->auto_percent) / 100u;
        if (selected > limit)
            selected = limit;
        selected &= ~(request->alignment - 1u);
    }
    if (selected < request->minimum_texture_bytes || selected > limit ||
        selected > SIZE_MAX || request->fixed_heap_bytes > SIZE_MAX - selected)
        return REF_AGC_MEMORY_BUDGET_INSUFFICIENT;

    out->texture_bytes = selected;
    out->heap_bytes = request->fixed_heap_bytes + selected;
    out->remaining_bytes = observation->free_bytes - out->heap_bytes;
    out->texture_limit_bytes = limit;
    return REF_AGC_MEMORY_BUDGET_OK;
}
