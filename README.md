# PS5 Xash3D

**PS5 Xash3D is the native PlayStation 5 port of the GoldSrc-compatible
[Xash3D FWGS](https://github.com/FWGS/xash3d-fwgs) engine, built graphics
first on the console's AGC interface.** It drives the **PS5 GPU** with
independently authored `gfx1013` shaders and a fence-retired resource model,
and it grows in hardware-proven phases until the engine boots with an `ref_agc`
renderer and loads real GoldSrc content.

This repository continues from
[`mpereiraesaa/ps5-agc-gears`](https://github.com/mpereiraesaa/ps5-agc-gears)
at its commit `cbff264`, with full history. That repository is now frozen as
the standalone Gears demo; every phase of the port lands here.

## Where the port stands

| Phase | State | Hardware proof (one PS5, firmware 12.02) |
| --- | --- | --- |
| 0 — Renderer foundation | Complete | Two frames in flight, exact GPU fences and VideoOut tokens, 60,000-frame Gears soak at 59.94 fps |
| 1 — BSP viewer with noclip | Complete | `c1a0`, 3,611 draws, 164 base textures plus lightmap, DualSense movement, clean 60,000-frame textured gate |
| 2 — Resource foundation | Complete | Fence-retired pool, two-slot transient ring, V#/T#/S# descriptors, per-frame constants, clean 60,000-frame gate |
| 3 — Texture path | Complete | Dynamic lightmap, deterministic mip chains with trilinear/anisotropic filtering, alpha test, sky pass, exact accounting, final 60,000-frame soak with zero errors |
| 4 — GoldSrc render states | Complete | All eight implementation gates passed independently, then the complete water/glass/effects/Studio/HUD scene passed a 60,000-frame integrated FW 12.02 soak with exact ownership and zero errors |
| 5 — Platform layer | Complete | The dedicated Xash3D engine boots, indexes the complete 4,823-entry asset tree and loads `c1a0`; input, audio, memory, pthread/time, GPU/flip timing and the project-owned assert/identity/address shims have exact FW 12.02 evidence |
| 6 — Engine integration | In progress | The loader and dynamic `filesystem_stdio` gates passed on FW 12.02 with the complete 4,823-entry tree, large/mixed-case reads and exact unload; server, menu, client and `ref_agc` remain |
| 7 — Playable and release | Later | Gameplay, performance and level-transition soaks, reproducible release |

The final Phase 3 soak ran 60,000 frames uninterrupted with 122 mip chains,
2,915 opaque, 137 alpha-test and 158 sky draws per frame, 68,731,904 resident
bytes, exact upload accounting, intact guards and a clean teardown. The exact
evidence boundary of every gate is recorded in
[`docs/HARDWARE_VALIDATION.md`](docs/HARDWARE_VALIDATION.md) and the phase
documents below.

## Build and test

Run all host contracts and the fail-closed publication audit:

```sh
make test
make audit
```

Compile the public shader sources with an LLPC build that supports GFX1013:

```sh
make shaders AMDLLPC=/path/to/amdllpc LLVM_READELF=/path/to/llvm-readelf
```

The validated compiler is the public
[`mpereiraesaa/llpc` GFX1013 commit](https://github.com/mpereiraesaa/llpc/commit/23a0757d922da7eb06c6a626d48553e0fb99fde0),
built against the pinned
[`GPUOpen-Drivers/llvm-project` commit](https://github.com/GPUOpen-Drivers/llvm-project/commit/8fd93e26cf9b1235fc9573b68b96233818be0ed4).

Bake a GoldSrc map you own into the versioned upload bundle and build the
hardware-bearing texture-path title:

```sh
make bsp-bundle BSP_INPUT=/private/path/map.bsp
make bsp-texture-final-native-release BSP_INPUT=/private/path/map.bsp \
  PS5LOG_DEV_CONF=/private/path/dev.conf \
  AMDLLPC=/path/to/amdllpc LLVM_READELF=/path/to/llvm-readelf

make bsp-phase4-lighting-native-release BSP_INPUT=/private/path/map.bsp \
  PS5LOG_DEV_CONF=/private/path/dev.conf \
  AMDLLPC=/path/to/amdllpc LLVM_READELF=/path/to/llvm-readelf

make bsp-phase4-sprite-particles-native-release \
  BSP_INPUT=/private/path/map.bsp \
  PS5LOG_DEV_CONF=/private/path/dev.conf \
  AMDLLPC=/path/to/amdllpc LLVM_READELF=/path/to/llvm-readelf
```

`.bsp`, `.wad` and `.ps5bsp` files are ignored and never release inputs. The
build pins and verifies its public native foundation, always targets
`-gfxip=10.1.3`, derives shader metadata from PAL notes, links/signs the native
executable and packages `dist/PPSA99996/`. Generated binaries, local telemetry
configuration and deployment material are excluded from publication.
Deployment remains loader-specific.

Build the Xash3D dedicated engine boot title (Phase 5 gate 1). It needs no
shader compiler; game data you own is staged privately and never committed:

```sh
git submodule update --init third_party/xash3d-fwgs third_party/hlsdk-portable
XASH_GAME_DATA=/private/path/half-life PS5LOG_DEV_CONF=/private/path/dev.conf \
  make engine-boot-native-release
```

The result lands in `dist/engine-boot/PPSA99996/`. See
[`docs/ENGINE_BOOT_PHASE5.md`](docs/ENGINE_BOOT_PHASE5.md) for the gate
contract and the hardware acceptance rules.

Build the first Phase 6 gate, which exercises Xash's `COM_*` API against a
real application-owned PRX while retaining the proven static modules:

```sh
XASH_GAME_DATA=/private/path/half-life PS5LOG_DEV_CONF=/private/path/dev.conf \
  XASH_GATE_SECONDS=15 make engine-prx-loader-native-release
```

Its descriptor ABI, rollback ownership and accepted FW 12.02 evidence are in
[`docs/PRX_LOADER_PHASE6.md`](docs/PRX_LOADER_PHASE6.md).

Build the second Phase 6 gate, which moves only `filesystem_stdio` into an
application-owned PRX and retains the static server rollback point:

```sh
XASH_GAME_DATA=/private/path/half-life PS5LOG_DEV_CONF=/private/path/dev.conf \
  XASH_REF=none XASH_GATE_SECONDS=15 \
  make engine-filesystem-prx-native-release
```

The module ABI, shared-libc allocator contract, complete-tree workload and
accepted evidence are in
[`docs/FILESYSTEM_PRX_PHASE6.md`](docs/FILESYSTEM_PRX_PHASE6.md).

Build the ScePad gate on the same dedicated host. The foreground DualSense
must exercise movement, look, jump, crouch, use and fire before the bounded
gate exits:

```sh
XASH_GAME_DATA=/private/path/half-life PS5LOG_DEV_CONF=/private/path/dev.conf \
  XASH_GATE_SECONDS=120 make engine-pad-native-release
```

The ABI, Xash mapping, batching policy, neutralization rules and accepted FW
12.02 run are recorded in [`docs/SCEPAD_PHASE5.md`](docs/SCEPAD_PHASE5.md).

Build the SceAudioOut gate on the same dedicated host. It plays a deterministic
low tone, silence and high tone through the main port, so an operator has to be
in front of the console with Remote Play closed:

```sh
XASH_GAME_DATA=/private/path/half-life PS5LOG_DEV_CONF=/private/path/dev.conf \
  XASH_GATE_SECONDS=90 make engine-audio-native-release
```

The port contract, the 44.1 to 48 kHz conversion, the ownership rules and the
accepted FW 12.02 run are recorded in
[`docs/SCEAUDIOOUT_PHASE5.md`](docs/SCEAUDIOOUT_PHASE5.md).

Build the direct-memory gate. It routes the statically linked engine and C++
operators through one guarded arena, exercises representative GPU-resource
lifetimes, loads `c1a0`, then proves empty ordered teardown:

```sh
XASH_GAME_DATA=/private/path/half-life PS5LOG_DEV_CONF=/private/path/dev.conf \
  XASH_GATE_SECONDS=90 make engine-memory-native-release
```

The fixed-VA mapping policy, allocator/GPU ownership model and accepted FW
12.02 run are recorded in
[`docs/DIRECT_MEMORY_PHASE5.md`](docs/DIRECT_MEMORY_PHASE5.md).

Build the thread/time gate. It exercises joined and detached workers, mutex
ownership, `CLOCK_MONOTONIC`, `nanosleep` and the engine's historical `usleep`
surface before loading the normal engine workload:

```sh
XASH_GAME_DATA=/private/path/half-life PS5LOG_DEV_CONF=/private/path/dev.conf \
  XASH_GATE_SECONDS=30 make engine-thread-time-native-release
```

The exact lifecycle, timing acceptance bounds and accepted FW 12.02 run are
recorded in [`docs/THREAD_TIME_PHASE5.md`](docs/THREAD_TIME_PHASE5.md).

Build the GPU/flip timing gate on the complete Phase 4 scene. It writes one
raw GPU clock timestamp at end of pipe per frame and correlates it with the
CPU submit boundary, ownership fence and exact VideoOut event:

```sh
make bsp-phase5-gpu-flip-timing-native-release \
  BSP_INPUT=/private/path/map.bsp STUDIO_INPUT=/private/path/model.mdl \
  PS5LOG_DEV_CONF=/private/path/dev.conf \
  AMDLLPC=/path/to/amdllpc LLVM_READELF=/path/to/llvm-readelf
```

The measurement definitions and fail-closed 60,000-frame acceptance contract
are in [`docs/GPU_FLIP_TIMING_PHASE5.md`](docs/GPU_FLIP_TIMING_PHASE5.md).

Build the final Phase 5 libc-shim gate. It proves that `__assert`, `getpwuid`
and `dladdr` are local project definitions rather than dynamic imports, then
exercises their non-destructive contracts before the normal `c1a0` workload:

```sh
XASH_GAME_DATA=/private/path/half-life PS5LOG_DEV_CONF=/private/path/dev.conf \
  XASH_GATE_SECONDS=30 make engine-libc-shims-native-release
```

The exact semantics, symbol audit and accepted FW 12.02 run are recorded in
[`docs/LIBC_SHIMS_PHASE5.md`](docs/LIBC_SHIMS_PHASE5.md).

## Design and scope

The renderer covers native initialization, direct memory, color/depth surfaces,
shader metadata, command composition, submission, synchronization, VideoOut
presentation and teardown, plus the BSP bundle format, resource pool,
transient ring, descriptor builders and the Phase 3 texture pipelines. Every
gate is one variable at a time, host-tested first and then proven on hardware
with structured `ps5log/1` telemetry and immutable run manifests.

This repository contains no proprietary Sony SDK files, game material, dumps,
shaders or command buffers, and no jailbreak or payload-delivery
implementation. Hardware results apply only to the console and firmware
actually tested.

Useful references:

- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) — renderer structure
- [`docs/BSP_VIEWER_PHASE1.md`](docs/BSP_VIEWER_PHASE1.md) — BSP bundle, flat/textured draws and noclip
- [`docs/BSP_RESOURCE_FOUNDATION_PHASE2.md`](docs/BSP_RESOURCE_FOUNDATION_PHASE2.md) — fence-retired resources and transient rendering
- [`docs/BSP_TEXTURE_PATH_PHASE3.md`](docs/BSP_TEXTURE_PATH_PHASE3.md) — lightmap, mips, alpha test, sky and accounting gates
- [`docs/ENGINE_BOOT_PHASE5.md`](docs/ENGINE_BOOT_PHASE5.md) — Xash3D engine boot gate: static modules, PS5 backend, evidence
- [`docs/SCEPAD_PHASE5.md`](docs/SCEPAD_PHASE5.md) — native ScePad contract, Xash mapping and hardware evidence
- [`docs/SCEAUDIOOUT_PHASE5.md`](docs/SCEAUDIOOUT_PHASE5.md) — native SceAudioOut contract, the 44.1 to 48 kHz conversion and hardware evidence
- [`docs/DIRECT_MEMORY_PHASE5.md`](docs/DIRECT_MEMORY_PHASE5.md) — direct-memory engine arena, GPU lifetime contract and exact teardown
- [`docs/THREAD_TIME_PHASE5.md`](docs/THREAD_TIME_PHASE5.md) — pthread ownership, monotonic clock and sleep-granularity gate
- [`docs/GPU_FLIP_TIMING_PHASE5.md`](docs/GPU_FLIP_TIMING_PHASE5.md) — GPU end-of-pipe timestamps and exact VideoOut flip latency
- [`docs/LIBC_SHIMS_PHASE5.md`](docs/LIBC_SHIMS_PHASE5.md) — project-owned assert, identity and address-fallback shims
- [`docs/PRX_LOADER_PHASE6.md`](docs/PRX_LOADER_PHASE6.md) — application-owned PRX loader, descriptor ABI and hardware evidence
- [`docs/FILESYSTEM_PRX_PHASE6.md`](docs/FILESYSTEM_PRX_PHASE6.md) — dynamic filesystem module, allocator ABI and complete-tree hardware evidence
- [`docs/GOLDSRC_RENDER_STATES_PHASE4.md`](docs/GOLDSRC_RENDER_STATES_PHASE4.md) — Phase 4 state space, gate order and current checkpoint
- [`docs/BACKEND_PROVENANCE.md`](docs/BACKEND_PROVENANCE.md) — source provenance boundary
- [`docs/HARDWARE_VALIDATION.md`](docs/HARDWARE_VALIDATION.md) — hardware evidence
- [`docs/TELEMETRY.md`](docs/TELEMETRY.md) — runtime observability contract
- [`docs/DEVELOPMENT.md`](docs/DEVELOPMENT.md) — development workflow
- [`docs/RELEASING.md`](docs/RELEASING.md) — reproducible releases

## Credits and license

The native shell derives from
[BlackBearReloaded's PS5 Native App Boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate).
The renderer foundation and the Gears reference scene come from
`ps5-agc-gears`; the gear geometry adapts Mesa's MIT-licensed `es2gears`.
Exact provenance and attribution are recorded in [`NOTICE.md`](NOTICE.md).
All shaders and AGC integration in this repository are independently authored
and source reproducible. Xash3D FWGS and hlsdk-portable are pinned Git submodules under
`third_party/`, each under its own license; neither is vendored or modified.

Licensed GPL-3.0-or-later. The application identity `PPSA99996` is a local
development identifier dedicated to PS5 Xash3D, not an official Sony
assignment. The frozen Gears demo retains its separate `PPSA99997` identity.
