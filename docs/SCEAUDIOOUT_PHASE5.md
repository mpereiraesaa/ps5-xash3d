# SceAudioOut PCM output — Phase 5

## Phase 7 live-game audio (2026-09-09, first hardware acceptance)

Operator confirmed "todo perfecto" for the live-game listening test. Engine
run `20260909T200850188Z_PPSA99996_xash3d-engine_0x161f354ed4b13` and renderer
run `20260909T200850288Z_PPSA99996_ps5-xash3d_0x161f35a662700` closed cleanly.
SELF SHA-256: `04261813fb4448db9a012669338ddad4bb507c7c309a53ed746cc5ee8ae1135c`.
Paired renderer validation passed with 18,179 frames, nine reclaims, intact
guards and exact teardown. Audio produced/consumed 13,423,452 source frames,
sent 14,610,688 output frames in 57,073 blocks, padding 129, discarded 0,
output errors 0; drain/close/join each occurred once, worker owned, result 0.

Six underruns remain recorded, not erased or counted as zero: four before the
active-map timer marker and two just after it, with none later in the run.
Loading stalls are a hypothesis, not a demonstrated cause. Operator accepted
this as non-blocking polish after hearing no issue. Map-transition audio,
long-session behavior and underrun mitigation remain unvalidated. Four-blend
Studio hardware coverage is explicitly deferred; it does not block audio.

The operator deferred four-blend Studio hardware coverage to prioritize real
game audio. Build with `XASH_AUDIO=1`, `XASH_AUDIO_GATE=0`, the complete
client/menu/ref_agc stack, no Studio/recovery/weapon-grant probes, and a
300-second map-relative gate. This exercises the engine mixer through SNDDMA,
not the previously accepted deterministic tone pattern.

The client launcher previously added `-nosound` unconditionally. It now does
so only when the generated numeric `PS5_XASH_AUDIO_ENABLED` is zero; the
existing user-selection string macro remains separate. A host contract test
guards this wiring. Graphics-only builds retain their old silent behavior.

First hardware QA: keep Remote Play closed, use moderate TV volume, listen
for menu sounds, NPC speech, machinery and footsteps, and turn/move around a
sound source to check spatial behavior. Report silence, crackles, repetition,
speed/pitch anomalies or interruptions. Avoid map changes in this first run.
Audibility is operator evidence; separately inspect audio init/progress,
non-silent PCM, output errors/underruns/discards and exact drain/close/join,
plus paired renderer teardown. The first-run acceptance above is separate
from the Phase 5 tone evidence below and is not full release audio coverage.

This gate adds native PCM output to the stable dedicated Xash3D host. It proves
the audio half of the PS5 platform layer independently of the Phase 6 client:
the gate pushes a deterministic pattern through the ring, the resampler, the
worker and `libSceAudioOut` now, and the same core backs `SNDDMA_*` when client
mode is enabled.

Scope is PCM through `libSceAudioOut` only. AJM and AAC/MP3/Opus decoding,
AudioOut2, Audio3d, NGS2, AudioIn/microphone, full client/menu/`ref_agc`
integration and Chiaki capture are all outside it. Half-Life already hands the
engine mixer finished PCM; this gate implements the output.

## Provenance and ABI

The six-argument `sceAudioOutOpen` signature, the grain/rate/format
constraints, the 0 dB volume array, the blocking `Output` pacing and the
"no short final block, hold the tail and zero-fill it" rule come from the
independently authored, device-tested
[ps5-audio-decoding-research](https://github.com/blackbearreloaded/ps5-audio-decoding-research)
at commit `2c81f17910be6e7b26d05ae50f50adb0211581c2` (GPL-3.0). That evidence is
**FW 6.02**. It was used to design the backend; no symbol here is marked
`HW PASS` until this port exercises it on FW 12.02. The declarations in
`include/ps5_platform.h` and the C core are this repository's own code; no
vendor header is included and no reference source is vendored.

`tests/test_ps5_platform_abi.c` pins the call signatures and every constant the
accepted artifact used, so an ABI drift cannot reach hardware silently.

## Accepted configuration

| Parameter | Value |
| --- | --- |
| Port | main, type `0`, index `0` |
| User | system `0xff` (`XASH_AUDIO_USER=foreground` is the explicit alternative) |
| Rate | 48000 Hz |
| Format | `1` — signed 16-bit stereo, interleaved |
| Grain | 256 frames |
| Block | 512 `int16_t`, left/right alternating |
| Volume | flags `3`, eight entries of `0x8000` |

`sceAudioOutInit` runs on every open rather than behind a cached flag, so the
recorded `init_rc` is always real evidence instead of a remembered zero.

## The 44.1 kHz problem

Xash3D mixes at `SOUND_DMA_SPEED` (44100) and AudioOut accepts only 48000 or
192000 Hz. Assigning `snd.format.speed = 48000` is **not** enough: the engine
computes mixahead in `s_main.c`, stream timing in `s_stream.c`, raw-sample
stepping and the default sound rate in `s_load.c` from the `SOUND_DMA_SPEED`
macro directly, not from `snd.format`. The upstream submodule is therefore left
alone and the PS5 layer converts on the way out:

```text
44100 / 48000 = 147 / 160
```

`audio_ps5.c` runs a continuous linear-interpolation resampler with fixed
phase. `resample_prev` and `resample_frac` persist across calls, so output frame
*k* sits at input position *k* × 147/160 without a discontinuity at block or
chunk boundaries, and the phase returns to zero every 147-in/160-out cycle. For
*N* source frames the exact output count is `ceil((N - 1) × 160 / 147)`: priming
consumes one frame, and each emitted frame consumes at most one more.

The rate pair is reduced by its GCD at init, the interpolation product is
64-bit, and a configuration whose staged output could not fit the accumulator is
rejected up front instead of dropping frames at runtime.

## Architecture

Two pieces, deliberately separated.

**1. `xash/platform_ps5/audio_ps5.c` — the AudioOut core.** Client independent
and host tested. It owns the producer/consumer cursors, the resampler, the
worker and the counters. Ownership is explicit:

- the **producer** owns the ring memory and the published frame counter;
- the **worker** owns the handle and is the only caller of `Output`, the
  NULL-drain and `Close`;
- the mutex is **never** held across the blocking `sceAudioOutOutput`.

A step stages up to 1024 source frames under the lock, notifies the producer,
releases the lock, and only then resamples and submits whole 256-frame blocks.
`PS5_AudioWorkerStep` is exposed as a test seam so the ring, resampler, block
and underrun behaviour are all covered deterministically without a thread; the
worker thread does nothing but loop on it.

`PS5_AudioInit` acquires on the caller's thread — init, open, volume — so a
failure is reported synchronously to `SNDDMA_Init`, and any failed step rolls
the whole sequence back (a failed volume closes the handle exactly once, while
no worker exists yet). The handle then belongs to the worker alone.

**2. `xash/platform_ps5/s_ps5.c` — the SNDDMA binding.** A thin adapter over
Xash3D's own DMA ring, not a second implementation:

- `snd.samples` is **mono** samples, so the ring holds `snd.samples >> 1` stereo
  frames, and the default `s_samplecount` of `0x8000` gives 32768 frames — a
  power of two, which the masked ring index requires;
- `snd.paintedtime` is the produced stereo-frame count **and** the ring index
  the engine writes at (`S_TransferPaintBuffer` masks it with
  `(snd.samples >> 1) - 1`), which is exactly the masking the core applies to
  its consumer cursor;
- `snd.samplepos` is the read cursor in mono samples and `S_GetSoundtime`
  derives `soundtime` from it, so consuming *N* source frames advances it by
  *N* × 2 with wrap;
- `SNDDMA_BeginPainting` takes the core lock, excluding the worker while the
  mixer writes; `SNDDMA_Submit` publishes `snd.paintedtime` and releases it.

Past `0x40000000` the engine chops `paintedtime` back to one buffer length.
That makes the producer counter jump backwards, which a monotonic publish would
ignore and the consumer would then starve forever. `PS5_AudioRebase` restarts
both cursors and counts the event, so the case is handled and visible rather
than latent.

The cursor arithmetic itself lives in the core (`PS5_AudioAdvanceSamplePos`,
`PS5_AudioRingFrames`, `PS5_AudioIsRebase`) so the host tests cover it without
the engine header set, leaving the binding trivial.

## Accounting

The counters distinguish states that are easy to conflate:

| Counter | Meaning |
| --- | --- |
| `produced` / `consumed` | source frames published by the mixer / taken by the worker |
| `sent` / `blocks` | 48 kHz frames and whole blocks the port accepted |
| `silent` | source frames that were deliberately zero |
| `underruns` | episodes where the worker had no source while playing |
| `padding` | terminal zero-fill, one partial block at most |
| `discarded` | frames left pending by the bounded terminal drain |
| `wraps` / `high_water` | ring cursor passes and peak pending frames |

Priming is not an underrun, and an underrun is counted once per episode however
many times the worker spins. An underrun never fabricates a silence block:
zero-fill happens **only** on the final partial block, which is why
`padding < grain` always holds. `wraps` counts how many times the read cursor
passed the end of the ring, not how many copies happened to straddle it — an
aligned chunk size never splits and would otherwise report zero forever.

## Shutdown

Exact order, with the handle never touched from the main thread:

1. stop further publication and ask the worker to finish (`stop_requested`);
2. the worker consumes the valid queue within `PS5_AUDIO_DRAIN_STEPS`, counting
   anything still pending as `discarded` rather than dropping it silently;
3. it zero-fills the one partial block and submits it;
4. `sceAudioOutOutput(handle, NULL)` once, to drain;
5. `sceAudioOutClose(handle)` once;
6. the main thread joins exactly once;
7. ring, staging and synchronisation are released.

A run shorter than the prime level still plays out: the terminal path promotes
`PRIMING` to `PLAYING` before draining. An `Output` failure is terminal — the
state stays `FAILED`, the error propagates out of `PS5_AudioShutdown`, no
padding is invented, and the handle still closes exactly once. `PS5_AudioShutdown`
is idempotent: repeated calls never double-drain, double-close or double-free.

## Build and gates

```sh
git submodule update --init third_party/xash3d-fwgs third_party/hlsdk-portable

# Minimal ABI smoke: open, volume, a few whole blocks, drain, close.
XASH_GAME_DATA=/private/path/half-life \
PS5LOG_DEV_CONF=/private/path/dev.conf \
XASH_AUDIO_GATE_FRAMES=4096 XASH_GATE_SECONDS=90 make engine-audio-native-release

# Full audible gate: ring, worker, resampler and the three-segment pattern.
XASH_GAME_DATA=/private/path/half-life \
PS5LOG_DEV_CONF=/private/path/dev.conf \
XASH_GATE_SECONDS=90 make engine-audio-native-release
```

The link refuses an artifact that does not import all five accepted symbols,
and refuses one that imports `sceAudioOut2*`, `sceAudio3d*`, `sceNgs2*`,
`sceAjm*`, `sceAudioIn*` or `sceAudiodec*` — those are outside this gate and are
listed as banned in `xash/ps5_import_evidence.json`. It also verifies every
`XASH_AUDIO_*` marker survived into the ELF, because a patch that does not
compile makes a "the marker never fired" conclusion worthless.

`make engine-audio-client-link` substitutes `s_ps5.c` for `s_stub.c` in a client
build (`XASH_SOUND=99`, so `s_stub.c` compiles itself out and the two never both
define `SNDDMA_*`). **This is a compile/link proof only.** It is not Phase 6
integration and carries no hardware claim.

## The gate pattern

`audio_pattern_ps5.c` generates an unmistakable sequence at the engine's mix
rate. Generation is integer only — a Q16 phase accumulator driving a triangle
wave — so the host and the console agree on every sample and one expected hash
covers both.

| Segment | Frames | Content |
| --- | --- | --- |
| 0 | 26460 | 0.600 s low tone, 220 Hz |
| 1 | 13230 | 0.300 s deliberate silence |
| 2 | 26460 | 0.600 s high tone, 880 Hz |

Totals the console must reproduce exactly:

- source: **66150** frames (1.500 s at 44.1 kHz), 13230 of them silent
- source hash: `0x9fd6b8c32bb54595`
- resampled: **71999** frames — `ceil(66149 × 160 / 147)`
- output: **282** blocks, **72192** frames, **193** terminal padding frames

## Acceptance

`tools/validate_engine_boot_evidence.py --audio-gate` checks the port contract,
the recorded user variant, the power-of-two ring, the 147/160 ratio, whole-block
submission, the exact `sent == expected + padding` relation, hash agreement
between the generated pattern and the consumed PCM, zero underruns, zero Output
errors, zero discarded frames, exactly one drain/close/join owned by the worker,
and that progress telemetry is not emitted per block.

```sh
python3 tools/validate_engine_boot_evidence.py /path/to/run.json \
  --engine-commit 9aa39ad --hlsdk-commit e277ffa --map c1a0 --audio-gate
```

Acceptance additionally requires the operator to confirm having **heard** the
correct sequence. That confirmation is external evidence tied to the run id and
is deliberately not derivable from the transcript: the device cannot assert
`audible=true` about itself. Keep Chiaki/Remote Play closed during the run — it
can change audio routing.

## Accepted FW 12.02 evidence

- Run: `20260907T194413175Z_PPSA99996_xash3d-engine_0xc372db81ccc6`
- fSELF SHA-256: `febef3a565810dd18565a3dfc707506a2fbbad0dd1d55e077cf91a5f540b7f74`
- Linked ELF SHA-256: `f6db533ac53728e86768c03c0b1b08e033ce3514348f8ea69cf8a9d9b3b9884e`
- Transcript SHA-256: `f949a2d173b82c9415e3adb3f2c458947cf4600c98e254217d7b598c407a10bb`
- Engine/hlsdk pins: `9aa39ad` / `e277ffa`
- Port: user `0xff` (**system**), type `0`, index `0`, handle `0x20000000`
- Acquisition: `sceAudioOutInit`=0, `sceAudioOutOpen`=`0x20000000`,
  `sceAudioOutSetVolume`=0 with flags `3` and eight `0x8000` entries
- Source: 66150 frames at 44100 Hz, hash `0x9fd6b8c32bb54595` — equal to the
  independently generated pattern hash
- Output: 72192 frames in 282 whole 256-frame blocks, hash `0xfbcae52a8b451ae1`
- Conversion: `expected_resampled=71999` + `padding=193` = 72192, exactly the
  147/160 relation
- Ring: capacity 8192 frames, prime 1024, 8 wraps, high-water 8192
- Silence carried through: 13233 frames (13230 deliberate plus 3 zero crossings)
- Underruns 0, Output errors 0, discarded 0, rebases 0
- Teardown: `drain_calls=1`, `close_calls=1`, `join_calls=1`, `owner=worker`,
  `close_rc=0`, `result=0`
- `XASH_AUDIO_COMPLETE ... ownership=exact pass=1`, `XASH_EXIT result=0`, clean
  gap-free `ps5log/1` BYE (`xash-engine-boot-complete`)
- Operator confirmation: the low tone, the gap and the higher tone were heard in
  that order. This is external evidence tied to the run id above; the device
  never asserts it about itself.

The five AudioOut symbols are `HW PASS` in `xash/ps5_import_evidence.json` from
this run onwards.

Repeated with the identical artifact in run
`20260907T195320217Z_PPSA99996_xash3d-engine_0xc3f2392f8174` (transcript
`278602c1f84d7396e5a0e0f3ed5863a14b0d7bf5c1a95fe35c8ff4d93b4cd462`), which
reproduced every counter and both PCM hashes bit for bit and was confirmed
audible again. A rebuild from the merged tree yields the same ELF and fSELF
hashes, so the published code is the accepted artifact.

### Measured on FW 12.02, not taken from the FW 6.02 reference

- **`sceAudioOutOutput` returns the number of frames it accepted**, not zero:
  256 at this grain, and the NULL drain returns 256 as well. Success is
  therefore non-negative; treating a positive return as a failure rejects a
  healthy port. The reference documents no return semantics.
- **The system user `0xff` is accepted for the main port.** The
  `XASH_AUDIO_USER=foreground` variant was not needed and was not used; the
  marker records which one the artifact carried either way.
- The handle is a large positive value (`0x20000000`), so handle checks must
  test for negative rather than assume a small index.

### Two defects the first hardware run exposed

Run `20260907T193832820Z_..._0xc3239cf379dc` transported the pattern correctly —
identical frame, block, padding and silent-frame counts — but reported
`pass=0`, for two reasons worth recording:

1. **The consumed-PCM hash started at zero.** `PS5_AudioInit` zeroes its state
   with `memset`, which left `source_hash` and `output_hash` at `0` instead of
   the FNV offset basis, so the reported hash could never equal the pattern
   hash. It was diagnosed without guessing: the silent-frame count matched the
   host exactly (13233 both), which ruled out corrupted content, and hashing
   the pattern from seed `0` on the host reproduced the device value bit for
   bit. `tests/test_ps5_audio.c` now compares the core's reported hash against
   an independently computed FNV over the published frames — the coverage gap
   that let a zero-seeded hash reach hardware.
2. **`XASH_AUDIO_GATE_FRAMES` silently did nothing.** The build printed
   `audio_gate_frames=4096` while `audio_gate_ps5.c` never included the
   generated `ps5_xash_build.h`, so the knob's `#ifndef` default of `0` always
   won and the full sequence ran. The build echoing a value is not evidence
   that the compiled code honours it.
