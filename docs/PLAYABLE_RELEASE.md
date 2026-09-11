# Playable release

## Status

Phase 7 is closed. The public state is **PLAYABLE** for Half-Life 1 Steam game
data on PS5 firmware 12.02, title ID `PPSA99996`. The release boundary covers
the native AGC renderer, dynamic Xash3D modules, MainUI-to-map transition,
`c1a0` gameplay, world/brush/Studio rendering, HUD/effects, DualSense input,
SceAudioOut and haptics.

The renderer is fully GPU accelerated through the PS5 AGC/GFX10.13 path,
including GPU-visible resources, shader execution, synchronization and
VideoOut presentation. CPU work remains where expected for engine simulation,
scene preparation and Studio pose generation.

## Evidence boundary

The accepted hardware evidence is recorded in
[`HARDWARE_VALIDATION.md`](HARDWARE_VALIDATION.md) and the phase-specific
documents. Those records preserve the exact firmware, artifact hashes,
telemetry markers and teardown checks used during development. They are
historical evidence, not additional release gates.

## Runtime logs

The engine creates these files on every launch in the writable overlay next to
save/config data:

```text
/download0/xash3d/valve/logs/xash3d.log
/download0/xash3d/valve/logs/xash3d-trace.log
```

The first is the engine console log. The second is the structured `ps5log/1`
trace, including the boot token, artifact identity, resource/frame markers,
errors and teardown. Each launch starts a fresh trace file. If `/download0`
cannot be created, the runtime probes `/temp0` and records the selected path.
The optional TCP development sink is additive; its absence must not disable
local logging or gameplay.

## Public support policy

Half-Life 1 Steam is the supported playable content path in this release.
Other GoldSrc titles run inside the same PS5 Xash3D port when their compatible
game data/assets are staged and the corresponding `client.prx` is compiled and
included in the package. The engine port is shared; the per-game client module
is the additional build step. Do not call a title supported until that module
and its content have been validated on hardware.

For an issue, include the package commit, PS5 firmware, map, reproduction
steps, and both logs from the same run. Remove credentials, private network
paths and unrelated save data before uploading. Community soak reports now
drive maintenance; no new Phase 7 gate is required for ordinary bug reports.
