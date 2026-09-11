# Half-Life for PS5

Native PS5 port of [Xash3D FWGS](https://github.com/FWGS/xash3d-fwgs) running
the **Half-Life 1 Steam** game data. The current public package is playable
from the PS5 menu with MainUI, New Game, Load Game, saves, DualSense input,
audio, haptics and GPU-accelerated rendering.

![Half-Life MainUI on PS5](assets/screenshots/half-life-mainui.png)

## Highlights

- Native PlayStation 5 application with title ID `PPSA99996`.
- AGC/GFX10.13 renderer using the PS5 GPU for world geometry, lightmaps, brush
  entities, Studio models, HUD, particles, decals and effects.
- Full Half-Life 1 Steam `c1a0` gameplay path, including menu transitions and
  save/load persistence.
- DualSense movement, look, modern aim profile, weapon cycling, contextual
  current-ammo helper, reload and short firing haptics.
- SceAudioOut playback with a bounded PCM ring and orderly teardown.
- Local engine and structured traces written beside save/config data for
  reproducible community bug reports.
- The same engine can host other GoldSrc titles when their compatible data and
  corresponding compiled `client.prx` are packaged for that title.

The port is GPU accelerated: rendering, GPU-visible resources, synchronization
and VideoOut presentation use the PS5 AGC path. CPU work remains for normal
engine simulation, scene preparation and command construction.

## Requirements

This repository contains source and build tooling, not Sony SDK files or game
content. You need:

- A compatible PS5 homebrew environment and loader.
- The Prospero toolchain used by the project.
- A legally obtained Half-Life 1 Steam installation, supplied privately at
  build time with its complete `valve/` tree.

## Build and run

Initialize dependencies and run the host checks:

```sh
git submodule update --init --recursive
make test
make audit
```

Build the public interactive profile from a private Half-Life installation:

```sh
export XASH_GAME_DATA=/absolute/path/to/half-life
make native-release
```

`XASH_GAME_DATA` is the private game-data root. `BSP_INPUT` and `STUDIO_INPUT`
are optional build-time overrides. By default, the host tools read
`valve/maps/c1a0.bsp` and `valve/models/barney.mdl` below that root, then bake
them into the renderer's `map.ps5bsp` and `model.ps5mdl` bundles. They are not
additional runtime configuration files and are never committed to this
repository.

Install the resulting `PPSA99996` package with your loader and launch it from
the PS5 menu. The public profile opens MainUI; choose **New Game** or **Load
Game**. It does not issue a development auto-map or timeout. Bounded diagnostic
targets remain available for contributors who need deterministic subsystem
runs.

See [`docs/DEVELOPMENT.md`](docs/DEVELOPMENT.md) and
[`docs/RELEASING.md`](docs/RELEASING.md) for packaging and deployment details.

## Controls

The complete current mapping is in
[`docs/DUALSENSE_CONTROLS.md`](docs/DUALSENSE_CONTROLS.md). The essential layout
is:

| DualSense | Action |
| --- | --- |
| Left stick | Move |
| Right stick | Look |
| R2 / R1 | Primary / secondary attack |
| Cross / Circle | Jump / use |
| Square / Triangle | Reload / flashlight when equipped |
| L1 / R3 | Crouch |
| D-pad | Weapon cycling; up gives current ammo |
| Options / Create | Menu / pause |

## Logs and bug reports

Each launch creates fresh files in the writable overlay:

```text
/download0/xash3d/valve/logs/xash3d.log
/download0/xash3d/valve/logs/xash3d-trace.log
```

If `/download0` is unavailable, the runtime records a `/temp0` fallback. The
engine log contains console output; the trace contains structured `ps5log/1`
records for boot, resources, frames, input, audio, errors and teardown. The
optional network sink is additive and never required for play.

When reporting a problem, include the package commit, PS5 firmware, map,
reproduction steps and both logs from the same run. Remove credentials, private
network paths, save data and game assets before uploading.

## Project layout

```text
docs/             Build, release, architecture, controls and telemetry notes
xash/             Xash3D engine build and PS5 platform adapters
src/              AGC renderer, GoldSrc resources and runtime bridges
native/           Native shell, logging and AGC bindings
shaders/          Project-authored AGC shader sources
tests/            Host contracts and artifact validators
```

## Contributing

Keep changes focused and reproducible. Run `make test` and `make audit`, explain
ownership and teardown for platform changes, and never commit SDK files, dumps,
game assets or generated SELF/PRX binaries.

## Credits and license

Xash3D FWGS and hlsdk-portable remain pinned submodules under their own
licenses. The native shell follows
[BlackBearReloaded's PS5 Native App Boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate).
See [`NOTICE.md`](NOTICE.md) for third-party attribution.

This project is licensed GPL-3.0-or-later. It is an unofficial port and is not
affiliated with Valve, Sony or the original Half-Life rights holders.
