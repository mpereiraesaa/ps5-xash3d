# Capability matrix

This matrix describes the current public Xash3D runtime. “Ready” means the
host contract and the release path are implemented; content compatibility still
depends on the game data and its matching `client.prx`.

| Area | Status | Notes |
| --- | --- | --- |
| PS5 title shell | Ready | `PPSA99996`, MainUI launch and ordered teardown. |
| Filesystem | Ready | Directory listing, large reads, case handling and one consistent descriptor family. |
| AGC renderer | Ready | GPU-accelerated world, lightmaps, brush entities, Studio models, HUD and effects. |
| GPU memory | Ready | Direct-memory resources, cache transitions, fences and VideoOut retirement. |
| DualSense | Ready | Movement, look, jump, crouch, use, fire, reload, weapon cycling and contextual ammo helper. |
| Audio | Ready | SceAudioOut PCM ring, resampling, playback and teardown. |
| Haptics | Ready | Short primary-fire pulse through the DualSense output path. |
| Save/load | Ready | Writable overlay is used for saves, config and local logs. |
| Diagnostics | Ready | Structured `ps5log/1` trace plus optional network mirroring. |
| Additional GoldSrc titles | Build per title | Stage compatible data and compile/package that title’s `client.prx`; the engine port is shared. |

## Optional platform imports

The implementation keeps platform libraries where they provide the tested PS5
behavior. Project-owned probes and host tests validate ABI shape, symbol
presence, return codes and teardown rather than replacing those libraries.
Unused optional imports remain out of the release link.

## Contribution rule

Before adding an import or changing a platform contract, add a focused host
test, document the expected ownership, and run `make test && make audit`.
