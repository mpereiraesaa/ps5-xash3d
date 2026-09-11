# PS5 Xash3D

**Status: PLAYABLE.** PS5 Xash3D is a native PlayStation 5 port of the
GoldSrc-compatible [Xash3D FWGS](https://github.com/FWGS/xash3d-fwgs) engine.
The validated release runs the **Half-Life 1 Steam game data**: it boots the
real client, presents MainUI and `c1a0`, renders world geometry, brush
entities, Studio models, HUD and effects, and accepts DualSense input, audio
and haptics.

This is a GPU-accelerated port: the renderer uses the PS5 AGC/GFX10.13 path,
native GPU-visible resources, shaders, synchronization and VideoOut
presentation. It is not a software renderer or a CPU emulation layer. The
engine still prepares gameplay and model data on the CPU, as any normal game
engine does.

## Release scope

The public release runs on PS5 firmware 12.02 with title ID `PPSA99996`. The
remaining work is normal public soak testing: report regressions, content edge
cases and hardware differences with the logs described below. It is not an
unfinished renderer gate.

| Area | Status |
| --- | --- |
| Native AGC renderer and GPU resource lifetime | Complete |
| Xash3D filesystem, PRX modules and engine integration | Complete |
| MainUI → map transition and `c1a0` gameplay path | Complete |
| BSP/lightmaps, brush entities, Studio models, HUD and effects | Complete |
| DualSense movement/look/actions, modern aim profile and haptics | Complete |
| SceAudioOut playback and engine audio path | Complete |
| Community soak and compatibility reports | Ongoing after release |

`PPSA99998` is not used.

## Getting started

The repository does not include proprietary Sony SDK files or game content.
Provide a legally obtained **Half-Life 1 Steam** game tree privately,
including `valve/`, when building or packaging a title. That is the supported
playable content path today. Xash3D itself supports many other GoldSrc games
inside the same PS5 port: each game needs its own compatible game data/assets
and its corresponding `client.prx` compiled and included in the package. The
engine port is shared; the per-game client module is the extra build step.

Host contracts and the publication audit:

```sh
git submodule update --init --recursive
make test
make audit
```

The public playable profile starts at MainUI with no development timeout or
automatic map command. Build it with private game data and the matching proof
assets:

```sh
XASH_GAME_DATA=/private/path/half-life \
  BSP_INPUT=/private/path/valve/maps/c1a0.bsp \
  STUDIO_INPUT=/private/path/valve/models/barney.mdl \
  make engine-playable-native-release
```

Launch the resulting `PPSA99996` package from the PS5 menu, then use **New
Game** or **Load Game**. The bounded diagnostic targets intentionally retain
their old auto-map behavior for reproducible development evidence.

The reproducible native build and deployment details are in
[`docs/DEVELOPMENT.md`](docs/DEVELOPMENT.md) and
[`docs/RELEASING.md`](docs/RELEASING.md). The production application is
packaged for `PPSA99996`; deployment remains loader-specific.

## Controller

The maintained DualSense profile, button map and optional diagnostic commands
are documented in [`docs/DUALSENSE_CONTROLS.md`](docs/DUALSENSE_CONTROLS.md). R2 is the
primary attack button for every weapon; the D-pad changes weapons and the
right stick controls look. The profile is installed in the writable game data,
not in the read-only package.

## Logs and bug reports

Every run creates copyable logs beside the save/config overlay:

```text
/download0/xash3d/valve/logs/xash3d.log
/download0/xash3d/valve/logs/xash3d-trace.log
```

`xash3d.log` contains the engine console and `xash3d-trace.log` contains the
structured `ps5log/1` records (boot identity, frame/resource markers, errors
and teardown). A writable `/temp0` overlay is used only when `/download0` is
unavailable. The optional development TCP sink improves live diagnostics but
is never required to play; local logging continues when the PC is offline.

When reporting a problem, include the commit or package version, PS5 firmware,
map, a short reproduction, and both log files from the same run. Compress them
if necessary and remove personal network paths or unrelated save data. Do not
paste credentials or proprietary game files into an issue.

## Project map

- [`docs/PLAYABLE_RELEASE.md`](docs/PLAYABLE_RELEASE.md) — final release
  boundary, validation summary and support policy.
- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) — renderer and module layout.
- [`docs/TELEMETRY.md`](docs/TELEMETRY.md) — structured records and ownership
  invariants.
- [`docs/HARDWARE_VALIDATION.md`](docs/HARDWARE_VALIDATION.md) — firmware-scoped
  evidence and reproducibility notes.
- [`docs/DUALSENSE_CONTROLS.md`](docs/DUALSENSE_CONTROLS.md) — DualSense mapping and QA.
- [`docs/DEVELOPMENT.md`](docs/DEVELOPMENT.md) — local workflow and tests.

Focused engineering notes remain under `docs/` for contributors who need to
reproduce a subsystem check; they are reference material, not a second list of
release requirements.

## Contributing

Keep changes small and reproducible. Run `make test` and `make audit`, describe
the PS5 hardware/firmware result when applicable, and attach a local trace for
runtime changes. Pull requests should explain ownership, teardown and any
content assumptions. Do not add SDK binaries, dumps, game assets, generated
ELFs/SELF files or private `dev.conf` files.

## Credits and license

The native shell derives from
[BlackBearReloaded's PS5 Native App Boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate).
Xash3D FWGS and hlsdk-portable remain pinned submodules under their own
licenses. Third-party provenance is listed in [`NOTICE.md`](NOTICE.md).

This project is licensed GPL-3.0-or-later. `PPSA99996` is a local development
identity and is not an official Sony assignment.
