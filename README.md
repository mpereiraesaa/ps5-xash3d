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
| 5 — Platform layer | In progress, engine/FS/ScePad passed | The dedicated Xash3D engine boots, indexes the complete 4,823-entry asset tree and loads `c1a0`; the ScePad backend then proved batched movement, look, jump, crouch, use and fire with exact teardown. AudioOut is next |
| 6 — Engine integration | Later | Modular Xash3D boot: `ref_agc`, menu, client, server and filesystem as application-owned PRX modules |
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

Build the ScePad gate on the same dedicated host. The foreground DualSense
must exercise movement, look, jump, crouch, use and fire before the bounded
gate exits:

```sh
XASH_GAME_DATA=/private/path/half-life PS5LOG_DEV_CONF=/private/path/dev.conf \
  XASH_GATE_SECONDS=120 make engine-pad-native-release
```

The ABI, Xash mapping, batching policy, neutralization rules and accepted FW
12.02 run are recorded in [`docs/SCEPAD_PHASE5.md`](docs/SCEPAD_PHASE5.md).

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
