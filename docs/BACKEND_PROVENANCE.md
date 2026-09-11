# Backend provenance

This repository is the public source boundary for the PS5 Xash3D port. It does
not contain Sony SDK headers, runtime binaries, game data, captured command
buffers or generated SELF/PRX artifacts.

## Project-owned adapters

| Area | Implementation and contract |
| --- | --- |
| Filesystem | `xash/platform_ps5/filesystem_prx_module.c` owns the complete open/read/seek/stat/close family and exposes the engine filesystem through a validated PRX descriptor. |
| Input | `xash/platform_ps5/in_ps5.c` consumes chronological ScePad batches and translates them to Xash axes/buttons. The ABI is documented in [`DUALSENSE_CONTROLS.md`](DUALSENSE_CONTROLS.md). |
| Audio | `xash/platform_ps5/audio_ps5.c`, `audio_pattern_ps5.c` and `audio_gate_ps5.c` own the PCM ring, resampling and SceAudioOut worker. |
| Memory | `xash/platform_ps5/memory_arena_ps5.c`, `mem_ps5.c` and `memory_gate_ps5.c` provide guarded engine allocation and generation-tagged GPU ownership. |
| Timing | `xash/platform_ps5/thread_time_ps5.c` wraps joined/detached workers, mutexes, monotonic clocks and bounded sleeps. |
| Renderer | `src/ref_agc_*`, `src/goldsrc_*` and `native/ps5_agc_native.c` build and submit AGC command streams using GPU-visible resources and exact retirement tokens. |
| Loader | `xash/platform_ps5/prx_loader_ps5.c` validates names, ranges, exports and rollback ownership for application PRXs. |
| Logging | `native/ps5log` writes structured local traces and can mirror them to the optional TCP sink. |

## External research and licenses

The ScePad ABI follows the independently authored
[PS5 native gamepad input research](https://github.com/blackbearreloaded/ps5-native-gamepad-input-research).
The audio contract follows the independently authored
[PS5 audio decoding research](https://github.com/blackbearreloaded/ps5-audio-decoding-research).
Both are used as compatibility references; their source is not vendored.

The native shell follows
[PS5 Native App Boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate).
Xash3D FWGS and hlsdk-portable remain pinned submodules under their own
licenses. See [`NOTICE.md`](../NOTICE.md) for attribution.

## Verification

Host contracts are run with `make test`; `make audit` checks that only
allowlisted, reviewable files are published. Native builds must record the
toolchain revision, artifact hash, firmware, title ID and the local trace path.
The release identity is `PPSA99996`.
