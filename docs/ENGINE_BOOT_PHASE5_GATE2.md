# Xash3D client engine boot — Phase 5 gate 2 (in progress)

Gate 2 brings the engine up in **client mode** without a display, as the
correctness harness before the AGC renderer of gate 3. It builds and boots;
it does not pass yet. This document records the working machinery and the one
blocker, so the next iteration starts with the exact target.

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

## The blocker: a data-triggered SIGSEGV in the filesystem scan

Bisection on FW 12.02, one variable at a time, same engine sources:

| Data staged under `valve/` | Mode | Result |
| --- | --- | --- |
| gate-1 subset (cfg, `c1a0.bsp`, models, sprites, events, scripts, sentences) | dedicated | pass (gate 1) |
| subset + all 9 WADs | dedicated | pass, spawns `c1a0` |
| subset + WADs + `gfx/` + `resource/` | dedicated | **crash** |
| subset + WADs + `gfx/` + `resource/` | client | **crash** |
| full retail `valve/` (541 MB) | dedicated | **crash** |
| full retail `valve/` | client | **crash** |

So the trigger is the content of `gfx/` or `resource/`, it is **independent of
client vs dedicated mode**, and it is not the renderer (the crash precedes
`Loading renderer`). The crash is a hard `SIGSEGV` (`si_code=1`, `addr=0`)
during the tail of `FS_AddGameHierarchy`/`FS_InitStdio`, right after the four
`Adding directory` lines and before `Dll loaded`. No `Sys_Error`/`Host_Error`
console line precedes it, so it is a genuine memory fault, not the engine's
`longjmp` error path.

The fault signature is byte-identical across every build:
`pc=0x7eeffa2d0`, `rax=pc`, `rsp=0` (the kernel does not populate `mc_rsp`
for this fault), `rbp=0x300`, `rdi=rsi=0`, faulting thread `0x880f4c540` with
its stack at `~0x7eeff0000`. `pc` is in a system module that
`sceKernelGetModuleList` does not enumerate (only `eboot.bin` and `libc.prx`
come back), i.e. inside `libSceLibcInternal`/`libkernel`, operating on a bad
pointer the engine handed it while scanning `gfx/` or `resource/`.

## Next iteration

1. The crash reporter now walks the faulting thread's stack (from `mc_rsp`,
   falling back to the handler frame) and prints every return address inside
   `eboot.bin` as an offset from `main`, so the next run yields a backtrace
   that `llvm-symbolizer --obj build/engine-boot/llvm-pie.elf` resolves.
2. Split the trigger: stage subset + WADs + `gfx/` only, then + `resource/`
   only, to name the directory, then bisect to the file.
3. Likely areas given the timing: `FS_InitStdio` enumerating game
   subdirectories (`listdirectory` + `FS_SysFolderExists` + `FS_ParseGameInfo`)
   or the per-directory case-fix cache (`FS_PopulateDirEntries` in
   `filesystem/dir.c`) over a directory whose entries our index-backed
   `readdir` returns; verify our synthesized `dirent` records for `gfx/env`,
   `gfx/vgui`, `gfx/shell` and the `resource/` tree against what the engine
   expects (`d_reclen` rounding, `d_type`, name termination).

Game data for this gate does not need the full 541 MB; a curated render set
(subset + WADs + `gfx/` + `resource/`) reproduces the blocker and is faster to
iterate.
