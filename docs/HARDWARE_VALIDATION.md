# Runtime validation

This note records the release boundary for the public PS5 Xash3D package. The
current identity is title ID `PPSA99996` on PS5 firmware 12.02. It is a concise
reference for contributors; detailed private captures and console dumps remain
outside the repository.

## Release checks

- MainUI launches from the PS5 menu without an automatic map or development
  timeout.
- New Game and Load Game reach `c1a0` with the Half-Life 1 Steam data tree.
- World geometry, lightmaps, brush entities, Studio models, HUD, particles,
  decals and chapter captions render through the AGC/GFX10.13 path.
- DualSense movement, look, actions, weapon cycling, contextual ammo helper,
  reload and haptics are active through the profile in
  [`DUALSENSE_CONTROLS.md`](DUALSENSE_CONTROLS.md).
- SceAudioOut playback, save/load persistence and ordered PRX teardown are
  active in the same package.

## Reproducing a run

Build with private game data, then launch the resulting package from the PS5
menu:

```sh
XASH_GAME_DATA=/private/path/half-life \
  BSP_INPUT=/private/path/valve/maps/c1a0.bsp \
  STUDIO_INPUT=/private/path/valve/models/barney.mdl \
  make engine-playable-native-release
```

Each launch writes a fresh engine log and structured trace beside save/config
data:

```text
/download0/xash3d/valve/logs/xash3d.log
/download0/xash3d/valve/logs/xash3d-trace.log
```

If `/download0` is unavailable, the runtime records a `/temp0` fallback. A
network sink may mirror records for live development but is not required.

## Reporting

Include the package commit, firmware, map, reproduction steps and both log
files from the same run. Remove credentials, private network paths and game
assets before publishing. New reports are compatibility maintenance; the
release does not require a separate historical gate sequence.
