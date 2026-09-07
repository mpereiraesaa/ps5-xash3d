# Phase 5 GPU EOP and VideoOut flip timing

This gate replaces the old pipelined-frame deadline interpretation with one
correlated record per retired frame. It instruments the already accepted
Phase 4 renderer; it does not integrate `ref_agc` or otherwise enter Phase 6.

## Question and boundary

For every submitted frame the gate must answer:

1. when the CPU began the checked submit transaction;
2. what raw GPU clock value was written at end of pipe;
3. when the CPU observed the ownership fence at zero; and
4. when the exact VideoOut event for the frame token was received.

The raw GPU value is reported as ticks. This gate does not assign a frequency
to that counter and does not label it nanoseconds. One timestamp immediately
before `SetFlip` proves ordered end-of-pipe progress and permits inter-frame
GPU-clock deltas. It is not a start/end GPU execution-duration query.

## Command order

`ps5_present_compose_flip_and_fence` appends this order to each completed draw
stream:

```text
draws
  -> RELEASE_MEM data_sel=3 to the slot's 64-bit timestamp
  -> sceAgcDcbSetFlip transaction
  -> RELEASE_MEM data_sel=2 to the slot's ownership fence
```

The timestamp packet is the independently authored eight-DWORD packet proven
by the repository's private cross-title command-stream research. Its stable
public contract is `0xc0064900, 0x06000528, 0x60010000, addr_lo, addr_hi,
0, 0, 0`. The ownership packet remains the existing selector-2 write. The new
packet never replaces or weakens fence plus exact-token retirement.

Each command stream, fence and timestamp lives in the existing main direct
memory mapping. The two fences and two timestamp destinations occupy separate
64-byte cache lines after both command streams. The submit boundary rejects
unaligned destinations and any overlap with the command stream or fence.
Before submit, the CPU writes `UINT64_MAX` to the timestamp destination; a
retired frame is rejected if that sentinel remains or if the raw clock does
not advance strictly from the preceding frame.

## CPU latency definitions

All CPU values use `CLOCK_MONOTONIC`:

- `submit_to_gpu_ns = cpu_gpu_observed_ns - cpu_submit_ns`
- `submit_to_flip_ns = cpu_flip_observed_ns - cpu_submit_ns`
- `gpu_to_flip_ns = cpu_flip_observed_ns - cpu_gpu_observed_ns`

`cpu_submit_ns` is sampled immediately before the checked build-and-submit
call. `cpu_gpu_observed_ns` is sampled after the slot fence is observed at
zero. `cpu_flip_observed_ns` is sampled as soon as the exact VideoOut event is
decoded, before readback or other per-frame accounting. These are CPU-observed
latencies; the raw GPU timestamp remains a separate clock domain.

## Host contracts

`test_ps5_present` pins the optional timestamp packet, packet ordering and
cursor size. `test_ps5_submission` pins the sentinel, address isolation and
transaction behavior. `test_bsp_command_plan` pins cache-line-separated
destinations. `test_ps5_gpu_flip_timing` covers gap detection, CPU ordering,
strict GPU-clock progress, arithmetic and exact aggregate ranges.

The fail-closed evidence validator requires:

- the exact `PPSA99996` / `ps5-xash3d` identity and clean `ps5log/1` stream;
- the private BSP and Studio bundle hashes supplied by the operator;
- 60,000 correlated in-memory records with no sequence gap;
- 60,000 GPU writes and 59,999 strict raw-clock changes;
- monotonic CPU ordering and internally consistent latency arithmetic;
- deterministic structured samples at frame 0 and every 600th retirement;
- the existing Phase 4 completion, exact fences/tokens and intact guards; and
- `BYE reason=gpu-flip-timing-soak-complete`.

## Build

The gate is deliberately layered on the complete Phase 4 scene:

```sh
make bsp-phase5-gpu-flip-timing-native-release \
  BSP_INPUT=/private/path/map.bsp \
  STUDIO_INPUT=/private/path/model.mdl \
  PS5LOG_DEV_CONF=/private/path/dev.conf \
  AMDLLPC=/path/to/amdllpc LLVM_READELF=/path/to/llvm-readelf
```

Private game inputs and generated bundles remain outside publication. The
native output is `dist/PPSA99996/`.

## Hardware acceptance

FW 12.02 run
`20260907T225446311Z_PPSA99996_ps5-xash3d_0xcdd8ce3a668a` passed the
fail-closed validator:

- native ELF / signed fSELF SHA-256:
  `bfbbd5fd89404765e0d52992df4abd1a1699d0e4df6398824748522392740ec1` /
  `fdb489280c1bac1f2489f0449bbf8f914eaf7b4cb2f8436ea11d59abaa72e798`;
- private BSP / Studio bundle SHA-256 and bytes:
  `d66be922584d7537e2dca7233293195d6ae383b22fc7959853537a75815c5cfa`
  / 9,971,952 and
  `d5b3a1f9b5c9035b02e678079b3586a5fe35987d55167dab27868050969b3e31`
  / 93,952;
- transcript / manifest SHA-256:
  `6d987ea639085670e67e06d8c4eec99b53a698318555c7347c85bc9a4e922b97`
  / `74866e6be699bcf1253143a8180140785003d676aea9ded0ba20b306194d859a`;
- 60,000 consecutive records and GPU writes, 59,999 strict changes, zero
  regression, CPU-order error, sequence gap or renderer error;
- raw EOP range 22,580,929,665,192 to 22,681,573,336,968 ticks;
- submit-to-fence min/average/max 899,600 / 16,823,795 / 96,444,947 ns;
- submit-to-flip min/average/max 1,156,515 / 32,754,596 / 96,455,852 ns;
- observed fence-to-flip min/average/max 9,542 / 15,930,800 /
  30,502,938 ns; and
- 101 persisted timing samples, complete Phase 4 render/readback invariants,
  exact fence/token ownership, intact guards and clean gate BYE.

The roughly 32.75 ms average submit-to-flip residence reflects two pipelined
frames. Consecutive presentation throughput remained about 16.81 ms. These
measure different questions, which is why the old frame-deadline counter was
not reused as dropped-frame evidence.

After validation the exact `PPSA99996` helper removed the parked title. An
independent status query observed no BigApp and FTP, `shsrv`, `elfldr` and
`ps5debug` were all healthy. The transactional promotion left no `.new-*` or
`.previous-*` files in the title root.
