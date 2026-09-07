# Engine allocator and direct memory — Phase 5

This gate moves the statically linked Xash3D engine, its server/filesystem
modules and its C++ allocation operators onto one application-owned direct
memory arena. It also defines the generation and retirement contract that
Phase 6 `ref_agc` resources must use.

The gate is intentionally not a renderer integration claim. It allocates,
writes and hashes representative command, buffer, texture and depth spans and
proves their ownership transitions. The Phase 2–4 renderer separately proves
real GPU consumption and exact fence/VideoOut retirement. Joining `ref_agc` to
this allocator belongs to Phase 6.

## Root mapping on FW 12.02

The accepted root is 128 MiB and uses the fixed-VA sequence already established
by the laboratory's mapping-only probe:

1. `sceKernelReserveVirtualRange`, 64 KiB alignment;
2. `sceKernelAllocateMainDirectMemory`, type `0x0c`, 64 KiB alignment;
3. `sceKernelMapDirectMemory`, protection `0xf2`, fixed flag `0x10`, mapping
   alignment zero;
4. on teardown, `sceKernelMunmap` once and
   `sceKernelReleaseDirectMemory` once.

The first hardware iteration used protection `0x33`, flags zero and passed the
allocation alignment to `MapDirectMemory`. All three calls returned zero, but
the map selected a different VA from the reserved range. The adapter rejected
that mismatch and rolled back without starting the engine. Fixed mapping is
therefore a measured requirement, not a decorative flag.

## Allocation coverage

The final link wraps `malloc`, `calloc`, `realloc`, `free`, `memalign`,
`aligned_alloc` and `posix_memalign`. The pinned native foundation's
`app_cpp_runtime.cpp` supplies every throwing, nothrow, array, sized and aligned
`operator new/delete` variant; its libc calls are wrapped by the same arena.
The build rejects an ELF that still imports the ordinary `_Zn*`/`_Zd*`
operators.

Pointers returned by a shared system library are not claimed by the arena.
`free` and `realloc` recognize those foreign addresses and forward them to the
real libc functions while counting the event. The accepted run observed zero
foreign calls.

## Arena core

`memory_arena_ps5.c` is platform-neutral and host tested. It provides:

- a locked first-fit arena with 16-byte block granularity, splitting and
  coalescing;
- power-of-two alignment for CPU and GPU-facing allocations;
- generation cookies before each payload and two unaligned-safe 64-bit tail
  guards after it;
- overflow-checked allocation and `calloc` arithmetic;
- realloc growth with content preservation and checked shrink guards;
- stale-handle detection and aggregate live/peak/error counters.

The lock uses acquire/release atomics and the host stress test runs four
concurrent alloc/fill/free loops. The unit tests also cover fragmentation,
aligned allocation, guard corruption, exhaustion and stale generations.

## GPU resource lifetime

A `Ps5GpuAllocation` contains the pointer, block offset, requested bytes and
generation. Its state machine is fail closed:

```text
active --unsubmitted release--> free
active --retire(exact token)--> retiring
retiring --reclaim(same token + completion proof)--> free
```

The hardware gate creates four representative resources:

| Kind | Bytes | Alignment |
| --- | ---: | ---: |
| command | 2,097,152 | 256 |
| buffer | 4,194,304 | 65,536 |
| texture | 8,388,608 | 65,536 |
| depth | 4,194,304 | 65,536 |

Every byte receives a deterministic pattern and contributes to a recorded
FNV-1a hash. All four allocations then move through one exact synthetic
completion token. Calling it synthetic is important: it tests the allocator's
contract, while the Phase 2–4 evidence supplies the real GPU-fence half.

## Process-lifetime ownership

The native host exits with `_exit` to avoid a system error dialog, so C++ static
destructors do not run. After `Host_Main` returns, eight engine-lifetime objects
remain in the accepted build. They total 22,565 bytes; no GPU allocation is
live or retiring.

The root owns this final class explicitly. Shutdown validates every guard,
refuses bulk reclamation if any GPU resource is live, records the exact block
and byte counts, releases the CPU blocks, validates the empty arena, and only
then unmaps and releases the root. This is not hidden as an apparent leak and
does not weaken GPU completion requirements.

The directory index has its own explicit unload before that accounting. Its
two allocations are therefore ordinary balanced frees, not process-lifetime
objects.

## Build and validation

```sh
XASH_GAME_DATA=/private/path/half-life \
PS5LOG_DEV_CONF=/private/path/dev.conf \
XASH_GATE_SECONDS=90 make engine-memory-native-release

python3 tools/validate_engine_boot_evidence.py /path/to/run.json \
  --engine-commit 9aa39ad --hlsdk-commit e277ffa \
  --map c1a0 --memory-gate
```

The build requires all five direct-memory imports, rejects the banned import
ledger, verifies that the memory markers survived into the ELF and refuses
external C++ allocation operators. The validator requires the exact four
resources, unique non-zero generations, valid hashes, zero allocation/guard/
stale errors, balanced retire/reclaim counts, explicit process-lifetime
accounting, an empty post-shutdown arena and exactly one successful call at
each root lifecycle boundary.

## Accepted FW 12.02 evidence

- Run: `20260907T212512180Z_PPSA99996_xash3d-engine_0xc8f58f777975`
- fSELF SHA-256:
  `1a77a5abc51a52f2f23d04ef5bed47edf89852e449eaa3a57a06290daa943552`
- Linked ELF SHA-256:
  `d396471dc8a18b82574ffdda16a4d123a85931e1d37806d0c78d862fc339e2b1`
- Transcript SHA-256:
  `a43462a7e46de35fee6764273ca3e7622375d2e79455aebff93ef3cb6c32166d`
- Engine/hlsdk pins: `9aa39ad` / `e277ffa`
- Root: 134,217,728 bytes; reserve/allocate/map `1/1/1`, all rc 0
- Resources: four, 18,874,368 bytes; combined hash
  `0xc4b367e53de116f7`; guards intact
- Engine: 20,687 allocations, 1,091 reallocations, 35,632,245-byte peak;
  `c1a0` loaded and ran for the bounded 30-second window
- GPU ownership: 4 retires / 4 reclaims, zero live or retiring at shutdown
- Errors: allocation 0, guard 0, stale 0, foreign ownership 0
- Process lifetime: 8 blocks / 22,565 bytes, then exactly reclaimed
- Root teardown: unmap/release `1/1`, both rc 0; final live bytes/objects 0
- Transport: 31 structured records, no gaps, clean
  `BYE reason=xash-engine-boot-complete`

The failed mapping-policy run and the run that exposed process-lifetime objects
remain diagnostic history. Only the final run is classified as the gate pass.
