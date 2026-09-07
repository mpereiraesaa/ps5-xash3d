# Threads and monotonic time — Phase 5

This gate validates the pthread and time surface used by the Xash3D platform
layer on PS5 FW 12.02. It runs before the ordinary engine workload and does not
claim renderer integration: GPU end-of-pipe timestamps and VideoOut flip
latency are the following Phase 5 gate.

## Ownership contract

The core creates exactly two distinct workers. One is joined once; one is
detached once and signals completion through an atomic counter. Both perform
16,384 increments under one mutex, yielding exactly 32,768. Initialization,
creation, join/detach, completion and mutex destruction must all succeed, and
the detached worker must finish before its stack-owned state is destroyed.

The five-second detached-worker deadline is a failure bound, not normal pacing.
No worker or mutex survives the gate.

## Clock and sleep contract

The core samples `CLOCK_MONOTONIC` 8,192 times. Successful acceptance requires
zero call errors, zero regressions and at least one positive advance. It then
measures 16 samples at each of 1, 2, 5 and 10 ms for both `nanosleep` and
`usleep`, recording minimum, average, p95 and maximum latency.

A sample fails if it errors, wakes more than 50 microseconds early or lasts
500 ms or more. The tolerance admits measurement jitter but not a broken or
wrong-unit implementation. `Platform_Sleep` uses the same EINTR-safe
`nanosleep` policy and avoids the previous millisecond-to-microsecond overflow.

## Build and validation

```sh
XASH_GAME_DATA=/private/path/half-life \
PS5LOG_DEV_CONF=/private/path/dev.conf \
XASH_GATE_SECONDS=30 make engine-thread-time-native-release

python3 tools/validate_engine_boot_evidence.py /path/to/run.json \
  --engine-commit 9aa39ad --hlsdk-commit e277ffa \
  --map c1a0 --thread-time-gate
```

The build requires the exact dynamic imports for `clock_gettime`, `nanosleep`,
`usleep`, pthread create/join/detach/self/equal and mutex init/lock/unlock/
destroy. It also proves all five telemetry marker families remain embedded in
the ELF. Export presence is not treated as runtime proof.

## Accepted FW 12.02 evidence

- Run: `20260907T220548886Z_PPSA99996_xash3d-engine_0xcb2ce47a2f65`
- fSELF SHA-256:
  `cd691f19664e44cd8cd6cfb9b019f5b6794f7a8410470a77ba86aec11e95bdde`
- Linked ELF SHA-256:
  `3e22c9f8d686dee19f94a4780e7494ccc6e0e9312c98662b854ee4c4e7b75bbf`
- Transcript/manifest SHA-256:
  `45a5cb16d0f1fd2123db8075626c2007e7a01948609f14a1f31fc7a01146a50b`
- Engine/hlsdk pins: `9aa39ad` / `e277ffa`; map `c1a0`
- Thread lifecycle: create 2, join 1, detach 1; two distinct completions;
  every return code and mutex error count zero
- Locked counter: 32,768 / 32,768
- Clock: 8,192 reads, 8,191 advances, zero errors/regressions, minimum
  positive step 801 ns, observed span 7,221,259 ns
- `nanosleep` p95: 1.111290 / 2.113287 / 5.112222 / 10.118920 ms
  for 1/2/5/10 ms
- `usleep` p95: 1.120431 / 2.114570 / 5.113105 / 10.115792 ms for
  1/2/5/10 ms
- Sleep errors / early wakes: 0 / 0 across 128 total samples
- Engine: complete 4,823-entry index, `c1a0` loaded, bounded 30-second run;
  `Host_Main` result 0
- Memory inheritance: 35,632,244-byte peak, final empty arena, exact one-time
  reserve/allocate/map/unmap/release, zero failures
- Transport: 37 structured records, no gaps, clean
  `BYE reason=xash-engine-boot-complete`

The immutable validator accepted the manifest with `--thread-time-gate`.
Temporary deployment backups were then removed after an exact remote listing;
the passing `PPSA99996` executable remains installed. Neither Chiaki nor the
Gears identities were used or changed.
