# Phase 7 texture-memory policy — work in progress

Baseline: merged PR #25 (`3167fc6`). The accepted graphics/resource run still
uses an 80-MiB GPU texture arena. That is a scene-validation allocation, not
a PS5 hardware limit or an accepted finished-port memory policy.

## Requirements

1. Observe memory available to this application's direct-memory allocator.
   Distinguish total capacity, free bytes and largest usable contiguous block.
   Verify the ABI and execute a bounded hardware smoke before trusting a new
   platform import. SDK symbol presence alone does not establish correctness.
2. Provide explicit texture capacity and an automatic mode. Preserve a reserve
   for other allocations and account for the other resources in the shared
   contiguous renderer heap. Reject impossible explicit requests; do not
   silently lower texture quality or shrink a user-specified capacity.
3. Connect the selected capacity to allocation, cache bounds, telemetry and
   the evidence validator. A successful observation does not guarantee a
   subsequent allocation succeeds; handle that race and allocation/map failures.
4. Preserve fence/VideoOut retirement and exact teardown. Exercise exhaustion
   and partial-initialization rollback, not just successful allocation.
5. Validate the normal menu-to-c1a0 baseline and a larger explicit capacity in
   incremental FW 12.02 runs. Preserve artifact hashes, structured telemetry and
   paired resource evidence. Merge only after host and hardware acceptance.

## Implemented preparation (2026-09-09)

`src/ref_agc_memory_budget.[ch]` supplies an allocation-free checked planner.
It consumes a valid observation, fixed-heap size, outside reserve, minimum
capacity, alignment, explicit capacity or automatic share. It limits the
complete heap by contiguous availability and leaves the requested reserve.
Invalid/unmeasured/insufficient inputs leave an empty output. Percentage math
is overflow-safe. No platform defaults are established by the test fixtures.

Host tests cover automatic and explicit selection, fragmentation, preserved
reserve, unavailable measurements, inconsistent observations, alignment,
oversized requests and UINT64_MAX arithmetic. The targeted test and its
ASan/UBSan build pass. This planner is not yet wired into native allocation:
the deployed build and its accepted 80-MiB baseline are unchanged.

The pinned SDK stub exports `sceKernelAvailableDirectMemorySize`,
`sceKernelDirectMemoryQuery` and `sceKernelGetDirectMemorySize`. The public
headers searched so far do not establish their ABI/semantics. Existing local
research identifies the capacity query but does not prove a free-memory
measurement. Next: establish the appropriate query contract and smoke it in
the application context, then integrate the planner.

Also found: native `allocate_direct` reserves physical memory before mapping,
but its mapping-error return does not itself release the allocation; callers
set ownership flags only after success. Review and test this rollback path
as part of the integration, without changing the accepted successful path
without regression evidence.

## Following tasks (unchanged scope)

After closing this gate: chapter-title/HUD blending, real game audio, full
Studio/viewmodel fidelity, then optional valve_hd mounting and resource QA.
Those tasks and the rest of Phase 7 are not accepted by these host tests.
