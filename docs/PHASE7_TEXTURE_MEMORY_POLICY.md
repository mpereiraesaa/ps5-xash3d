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

## Availability-query hardware smoke (2026-09-09)

The local FW 12.02 dump resolves `sceKernelAvailableDirectMemorySize` through
NID `C0f7TJcbfac` at module offset 0x19b40. Its wrapper forwards three 64-bit
inputs and writes two optional 64-bit outputs; `sceKernelGetDirectMemorySize`
returns a 64-bit capacity. This static check justified an opt-in read-only
probe, not acceptance from SDK exports alone. No dump bytes are published.

`XASH_TEXTURE_MEMORY_PROBE=1` now logs before/after the existing renderer heap
allocation. It does not change allocation size or feed the planner yet.
The first build stopped at packaging because XASH_GAME_DATA incorrectly named
`assets`; nothing was deployed from that failed invocation. The successful
build leaves already-deployed retail data untouched, and the canonical nine
artifacts were transactionally deployed with exact raw FTP SHA-256 checks.

Accepted smoke runs:

- Engine: `20260909T121055270Z_PPSA99996_xash3d-engine_0x147df064e605f`
- Renderer: `20260909T121055326Z_PPSA99996_ps5-xash3d_0x147df09c045e3`
- SELF: `6e3106d282a5f2410f91141f8767a76b491535a0db0d28e221816f7e0aeb133c`
- Renderer ELF: `9253808a7135c7b2d419a2bfb21f1a78ae6494733be5125132e34a556583cbbb`
- Renderer PRX: `c7b84f899f50679fc48915249881d3cf0163b28ffc76f296074f7cc483113632`

Both calls return zero. Capacity is 12,884,901,888 bytes. Before the heap,
the returned block begins at 136,314,880 and has 12,748,587,008 bytes. After
allocating 253,427,712 bytes at that start, the block begins at 389,742,592
and has 12,495,159,296 bytes: exact start/size deltas match the allocation.
This establishes a usable availability observation in this application
context, not that capacity is free memory or that fragmented free blocks have
been summed. The policy must label largest-block availability distinctly and
must not claim total-free bytes from this single output.

The paired lightmap/2D/menu/brush/Studio validator passes 2,005 frames and exact
ownership. Thirty active-map seconds end naturally with clean BYEs, exact
engine root and pad teardown, and renderer resource release. Independent
post-run status finds no BigApp and four healthy services. No Remote Play or
operator movement was used. All host tests and publication audit pass.

Next: connect configurable capacity to this measured domain, account for the
shared heap and reserve, then validate explicit/automatic policy, exhaustion
and partial-allocation rollback. The texture-memory gate remains open.
