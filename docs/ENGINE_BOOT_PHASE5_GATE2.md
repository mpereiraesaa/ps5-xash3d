# Xash3D client engine boot — historical Phase 6 pre-gate diagnostic

> This is the diagnostic snapshot that exposed the Phase 5 filesystem/libc
> blockers. Those blockers and all Phase 5 gates are closed; Phase 6 has since
> passed the loader, filesystem, server, MainUI, GoldSrc client and `ref_agc`
> gates. This file remains the historical pre-gate diagnostic; the accepted
> final Phase 6 boundary is documented in `REF_AGC_PRX_PHASE6.md`.

This branch brings the engine up in **client mode** without a display as an
early Phase 6 integration harness. It builds and boots; it is not a completed
Phase 5 gate. The harness exposed a real Phase 5 filesystem blocker, so this
document records the measured evidence while keeping the plan boundary clear.

## What works

`XASH_MODE=client make engine-boot-native-release` builds, links and boots a
title that is far larger than the dedicated one:

- the engine compiled with the full `engine/client` tree, the bundled codecs
  (opus, ogg, vorbis, opusfile) and the `MultiEmulator` protocol shim;
- `mainui` (menu), the hlsdk-portable client (`cl_dll`, 42 of 50 named exports
  defined), and `ref_null` plus `ref_soft` as statically linked modules, each
  reduced to its export table and resolved through `lib_static.c`;
- a headless video backend (`xash/platform_ps5/vid_ps5.c`, `XASH_VIDEO=99`)
  that satisfies the window/`SW_*`/present hooks and hashes each presented
  software frame into telemetry;
- the renderer chosen with `-ref soft` (or `null`), sound disabled (`-nosound`).

The title boots through `ps5log/1`, loads `filesystem_stdio`, mounts `valve`
and begins `FS_AddGameHierarchy`. All modules link with no undefined symbols
(193 SDK imports) and the large-allocation threshold drops to 64 KiB for the
client's heavier small-allocation load.

## Resolved blocker: a libc `strcasestr` fault exposed by palette loading

Bisection on FW 12.02, one variable at a time, same engine sources:

| Data staged under `valve/` | Mode | Result |
| --- | --- | --- |
| gate-1 subset (cfg, `c1a0.bsp`, models, sprites, events, scripts, sentences) | dedicated | pass (gate 1) |
| subset + all 9 WADs | dedicated | pass, spawns `c1a0` |
| subset + WADs + `gfx/` + `resource/` | dedicated | **crash** |
| subset + WADs + `gfx/` + `resource/` | client | **crash** |
| full retail `valve/` (541 MB) | dedicated | **crash** |
| full retail `valve/` | client | **crash** |

The historical trigger was the content of `gfx/` or `resource/`, independent
of client vs dedicated mode and earlier than `Loading renderer`. The affected
builds produced a hard `SIGSEGV` (`si_code=1`, `addr=0`)
during the tail of `FS_AddGameHierarchy`/`FS_InitStdio`, right after the four
`Adding directory` lines and before `Dll loaded`. No `Sys_Error`/`Host_Error`
console line precedes it, so it is a genuine memory fault, not the engine's
`longjmp` error path.

The fault signature was byte-identical across the affected builds:
`pc=0x7eeffa2d0`, `rax=pc`, `rsp=0` (the kernel does not populate `mc_rsp`
for this fault), `rbp=0x300`, `rdi=rsi=0`, faulting thread `0x880f4c540` with
its stack at `~0x7eeff0000`. `pc` is in a system module that
`sceKernelGetModuleList` does not enumerate (only `eboot.bin` and `libc.prx`
come back), i.e. inside a system module, operating on a bad
pointer the engine handed it while scanning `gfx/` or `resource/`.

## Progress on hardware (2026-09-07)

The client build is not a single blocker but a serial bring-up. With the crash
reporter, the allocator NULL-return probe and a directory-open trace (behind
`PS5_XASH_FS_TRACE`), the runs establish:

1. **It is not the libc heap.** The probe emits `XASH_LIBC_OOM` the instant
   `malloc`/`calloc`/`realloc` returns NULL; it never fired before any crash.
   So `sceLibcHeapSize` being absent from the SDK is not the cause, and Ghidra
   on `libSceLibcInternal` for the heap size would be the wrong lead here.
2. **Initial localization placed the fault after the `valve/gfx` scan.** The
   directory-open trace showed the fault after `opendir(/app0/xash3d/valve/gfx)`
   (index-backed, 8 entries), while the engine resolves `gfx/palette.lmp`
   through `FS_FixFileCase`. Run
   `20260907T141702358Z_PPSA99996_xash3d-engine_0xb19844082dbc` reaches the end
   of that indexed listing and logs a successful directory-search match before
   a hard `SIGSEGV` at `pc=0x7eeffa2d0`, with unusable `mc_rsp=0`.

   Symbolization corrects the earlier caller attribution: runtime return address
   `0x5a1699` is the instruction after `FS_FindFile` in the static
   `FS_LoadFile_` helper from `filesystem/io.c`; it is **not**
   `FS_OpenReadFile` (whose corresponding return site is `0x5a0704`). The fault
   PC is 16 bytes above the caller's frame pointer and is consistent with bad
   control flow, but the available signal context does not by itself prove
   which buffer was overwritten or even that the synthetic `dirent` producer
   is at fault. The observed eight records are internally consistent; the full
   filesystem ABI and bounds still require explicit guards and host tests.
3. **With minimal data (subset + WADs, no `gfx/`), the client reaches the
   renderer.** Console gets to `Dll loaded`, `execing video.cfg` and
   `Loading renderer: soft -> ref_soft`. So the engine, `filesystem_stdio`,
   the hlsdk client and the module tables all work; the client path is sound.
4. **Second blocker is renderer bring-up.** `ref_soft` faults during its init
   (different `pc=0x8800a5b88`). Switching to `ref_null` gets further, through
   renderer load into client HUD/font loading (`failed to load console font`,
   expected with a null renderer), then faults again in that region. Both are
   separate from the `gfx/` FS crash.
5. **The filesystem lifecycle is correct for `gfx/palette.lmp`.** Reproducible
   generated-source instrumentation proves `FS_FindFile` returns a bounded,
   NUL-terminated path with its 64-byte canary intact. Run
   `20260907T153636232Z_PPSA99996_xash3d-engine_0xb5efc0ffe3e0` records
   `real_length=768`, allocation of 769 bytes, a 768-byte read ending at
   position 768, `FS_Close` returning zero, the caller size write, and the
   buffer returning successfully. This rejects the proposed split descriptor
   namespace: libc `lseek`, `read` and `close` all operate correctly on the
   descriptor returned by the PS5 `open` shim.
6. **The actual fault was `strcasestr` routed through
   `libScePosixForWebKit`.** Run
   `20260907T154117469Z_PPSA99996_xash3d-engine_0xb6313bc05f6a` reaches the
   `Image_LoadLMP` callback at runtime address `0x4017d0` and faults on its
   first case-insensitive substring test. Disassembly and the dynamic
   relocation table identify that call as imported `strcasestr`. The PS5
   build had asserted `HAVE_STRCASESTR=1` without a target runtime test.
7. **The portable Xash implementation fixes the crash.** Building with
   `HAVE_STRCASESTR=0` removes the dynamic import and includes `Q_stristr`.
   Artifact `b622cec5561f1cfb49731e6cad9b58cad49480afd970ee8fe9e6e858952666dc`
   loads and expands the palette, returns from the image loader, loads the
   Half-Life DLL, and exits cleanly through the engine error path in run
   `20260907T154452596Z_PPSA99996_xash3d-engine_0xb663524c9f61`. The apparent
   next `delta.lst` false negative was deployment state, not an engine fault:
   the console still had the earlier 530-entry curated `.dirindex` although
   the local release contained the complete file.
8. **The complete retail filesystem path passes.** A transactional game-data
   deployment promoted 4,741 files (555,437,162 bytes) and a 4,823-entry index
   after full-size checks plus SHA-256 verification of `.dirindex`,
   `valve/delta.lst`, and `valve/gfx/palette.lmp`. Client trace artifact
   `a1925f37995ed6af8268d703bd8f8ee29e364c6c76fc12ef1d0d7747777b7678`
   produced run
   `20260907T155915636Z_PPSA99996_xash3d-engine_0xb72c42a8f42f`, which resolves
   and reads `delta.lst` twice at exactly 12,565 bytes, executes `c1a0`, remains
   active for the 90-second gate, and closes with `XASH_EXIT result=0`, zero
   large-allocation failures, and a gap-free `BYE`. The validated rollback was
   then removed; no `.xash3d.staging-*` or `.xash3d.previous-*` trees remain.
9. **The remaining optional libc helpers pass an explicit FW 12.02
   pre-flight.** `XASH_LIBC_SMOKE=1` forces volatile indirect calls to
   `strcasecmp`, `strnlen`, `strlcpy` and `strlcat` before engine startup and
   makes the build reject an ELF that does not retain all four imports and
   both telemetry markers. The ELF
   `f40d7c2b3cad0f56e96ef974785cbc53b4c6512bf3dd05b871ef985ed4aec7a1`
   imports those four symbols and has no `strcasestr` dynamic symbol. Signed
   artifact
   `3aa7835949b1dd0f98de9fc8d6a9dec36fc16c68460617304b20eabb7cb5ce9f`
   produced clean run
   `20260907T162442485Z_PPSA99996_xash3d-engine_0xb88fc0cf77a3`: every
   representative string operation returned its expected value, the engine
   spawned `c1a0`, the 20-second gate ended with `XASH_EXIT result=0`, and the
   manifest contains a gap-free `BYE`. Consequently the four validated
   `HAVE_*` settings remain enabled; only `HAVE_STRCASESTR=0` is required.

The policy is evidence-driven rather than a blanket avoidance of Prospero
libraries. A system implementation stays selected when it passes an actual
target smoke test. Ghidra and firmware dumps are the next inspection layer
when a symbol fails or its ABI/provider is ambiguous, but an exported stub or
static disassembly alone is not runtime acceptance.

The linked-ELF audit is now automated by
`xash/tools/audit_dyn_imports.py` and the evidence ledger
`xash/ps5_import_evidence.json`. The smoke ELF currently contains 167 dynamic
imports: 21 have hardware evidence, three known-unavailable imports are
explicitly guarded/dormant, and 143 remain labelled `EXPORTED ONLY` until a
future Phase 5 path exercises or individually probes them. This is the desired
honest state: every import is visible, and no stub-table entry is silently
treated as proof.

The provider column is derived independently from the SDK stubs. It maps
`strcasecmp`, `strnlen`, `strlcpy` and `strlcat` to
`libSceLibcInternal.so`, while the rejected `strcasestr` symbol exists in
`libScePosixForWebKit.so`, not `libSceLibcInternal.so`. Provider mapping is
useful routing evidence but remains distinct from execution evidence.

Also observed: with case-sensitive directories (our target returns true from
`Platform_GetDirectoryCaseSensitivity`), the engine re-scans directories per
file lookup, so our `opendir` was called ~11,670 times before the renderer.
Not fatal, but the per-directory case-fix cache is not being reused as
expected; worth confirming it is not repopulating every lookup.

## Prioritized blockers at this snapshot

1. `ref_soft` init fault, then the `ref_null` HUD/font fault. Decide whether
   the gate-2 harness uses `ref_null` (accepting no textures) purely to prove
   the frame loop, and defer real rasterization to `ref_agc` in gate 3.
2. The excessive re-scan (case-fix cache reuse).

## Planned iteration at this snapshot

1. Preserve the complete-tree filesystem run as the Phase 5 FS evidence and
   keep the generated trace path selectable for future single-file probes.
2. Continue the Phase 5 platform gates (input, audio, direct memory,
   threads/time, frametime telemetry, and libc shims) independently of this
   Phase 6 client integration harness.
3. Resume `ref_agc` renderer integration only under the Phase 6 boundary.

A curated render set (subset + WADs + `gfx/` + `resource/`) is sufficient to
reproduce the historical libc fault. The accepted complete-tree filesystem
gate uses the full 541 MB dataset and its 4,823-entry index.
