# Architecture

PS5 Xash3D is split into a small native shell, platform adapters, the Xash3D
engine and the AGC renderer. The public build contains no proprietary SDK
headers, game data or generated binaries.

## Runtime layers

1. **Native shell** — title lifecycle, PRX loading, logging and ordered teardown.
2. **Platform adapters** — filesystem, ScePad, SceAudioOut, threads, monotonic
   time, direct memory and VideoOut.
3. **Xash3D engine** — map loading, game rules, entities, Studio models, HUD,
   effects and save/load state.
4. **AGC renderer** — GPU-visible resources, shader metadata, command buffers,
   fences, presentation and frame ownership.
5. **Diagnostics** — local `ps5log/1` traces and the optional TCP development
   sink. Local logs remain available when the PC is offline.

## Ownership rules

- GPU-referenced memory remains alive until both its GPU fence and matching
  VideoOut token retire.
- Each PRX owns only the services it acquired and releases them in reverse
  order; ambiguous ownership fails closed.
- Filesystem descriptors use one consistent open/read/seek/close family.
- Input batches are consumed chronologically so short button edges are not lost.
- Audio output has one owner for the SceAudioOut handle and a bounded PCM ring.

## Build boundaries

`xash/build_engine.sh` produces the engine and the application-owned modules.
The public `engine-playable-native-release` profile opens MainUI and lets the
player choose New Game or Load Game. Bounded diagnostic targets remain available
for contributors who need deterministic map or subsystem runs.

The renderer is GPU accelerated through the PS5 AGC/GFX10.13 path. CPU work is
limited to normal engine simulation, scene preparation and command construction;
there is no software-renderer fallback hidden behind the release profile.

See [`BACKEND_PROVENANCE.md`](BACKEND_PROVENANCE.md) for source and ABI
provenance, and [`TELEMETRY.md`](TELEMETRY.md) for record names and ownership
invariants.
