# Telemetry contract

Every hardware run uses the vendored `native/ps5log` client and structured TCP
protocol `ps5log/1`. The native title reads only `/app0/dev.conf`; the real
configuration is ignored and packaged locally from `dev.conf`. No console log
file, USB mount, `/download0` fallback or filesystem mirror is permitted.

## Required run identity

The title creates one monotonic boot token before AGC initialization. The same
token appears in the HELLO frame and `LOG_BOOT_MONOTONIC_NS`. A run is accepted
only when title, app, token and deployed artifact SHA-256 all match a new PC-side
manifest.

Required opening records:

```text
LOG_SCHEMA=3
LOG_TRANSPORT=ps5log/1 tcp structured
LOG_FS_SINKS=disabled
LOG_BOOT_MONOTONIC_NS=<same token as HELLO>
```

## GPU evidence

The final classifier must observe, for the exact requested frame count:

- render-target clear marker with `color_dma=false`;
- maximum frames in flight equal to two;
- one unique positive 48-bit flip token per frame;
- terminal GPU fence zero before slot reuse;
- matching VideoOut event token before backbuffer reuse;
- intact color/depth guard words;
- zero errors and nonzero consecutive-presentation interval;
- ordered unregister, event removal, VideoOut close, memory release and AGC
  unload;
- `BYE` with a gap-free final sequence.

No submit return value, silence, elapsed timeout or clean TCP close substitutes
for these ownership observations. If telemetry becomes incomplete after submit,
the process parks and retains resources.

The Phase 3 dynamic-lightmap gate additionally emits
`DYNAMIC_LIGHTMAP_READY`, four `DYNAMIC_LIGHTMAP_FRAME` samples,
`DYNAMIC_LIGHTMAP_READBACK` and `BSP_TEXTURE_PATH_LIGHTMAP_COMPLETE`. The
samples separate resident bytes, transient bytes, actual lightmap upload bytes,
the wider aligned acquire span and the cumulative upload total. Acceptance
requires deterministic A/B patch hashes, distinct final GPU buffer hashes,
stable bytes outside the patch, intact guards and six-allocation exact-token
reclamation.

The mip/sampler gate adds `MIP_CHAINS_READY`, four `MIP_SAMPLER_FRAME`
samples, `MIP_SAMPLER_READBACK` and `BSP_TEXTURE_PATH_MIP_COMPLETE`. The chain
record fixes layout order, pitch alignment, level range and aggregate bytes.
The frame samples expose the exact four S# DWORDs. Final trilinear and
anisotropic frames must use the same lightmap pattern and produce different
GPU-visible framebuffer hashes; both dynamic-lightmap slot hashes must remain
equal. The accepted run must also retain every Gate 1 ownership, guard, upload
and exact-token invariant.

The alpha-test gate adds `ALPHA_TEST_READY`, three `ALPHA_TEST_FRAME` samples,
`ALPHA_TEST_READBACK` and `BSP_TEXTURE_PATH_ALPHA_COMPLETE`. The ready record
fixes the `{` texture/draw partition, the selected camera target and the raw
opaque/alpha `DB_SHADER_CONTROL` values. Final control and alpha-test frames
must share the same camera, anisotropic sampler and dynamic-lightmap pattern,
while producing distinct GPU-visible framebuffer hashes. The accepted run must
also preserve all prior ownership, guard, upload and exact-token invariants.

The accounting gate replaces the ad-hoc aggregate with a platform-neutral,
checked-`uint64_t` ledger. `TEXTURE_RESIDENCY_READY` partitions all six
fence-retired allocations and separately identifies the base mip payload,
source lightmap and two live dynamic-lightmap images. The allocation partition
must equal the reported pool-resident total; the three texture payload classes
must equal the texture-resident subtotal without being double-counted into the
pool total.

Every frame is recorded in order before submit. The ledger rejects a skipped or
repeated frame, a changed transient footprint, a first/full lightmap upload
outside the first use of each slot, a changed bounded-patch size or any integer
overflow. Four `TEXTURE_UPLOAD_FRAME` bookends expose the exact component and
cumulative values. `TEXTURE_UPLOAD_SUMMARY` proves the complete configured
sequence through component totals, min/max bytes, exactly two full updates,
all remaining bounded updates and an order-sensitive FNV-64 digest. The strict
accounting and final validators recompute the closed-form totals and require the
final sampled digest to match the summary and completion records. The final
60,000-frame build additionally emits `BSP_TEXTURE_PATH_FINAL_COMPLETE`, which
joins the mip, opaque/alpha/sky draw, pipeline, sampler, accounting, readback,
ownership and guard contracts in one terminal record.

This texture-only gate declares `input_dependency=none` and
`input_gate=not-repeated`. Controller connection and movement remain observed
telemetry, but are not success criteria because the input path did not change
after its earlier hardware gate. This keeps the accounting proof from silently
becoming another DualSense movement proof.

The Phase 4 native-binding checkpoint adds `GOLDSRC_PIPELINES_READY` after all
99 semantic cache entries and nine native shader slots are valid.
`GOLDSRC_STATE_FRAME` identifies the selected opaque and masked BSP keys,
passes, shader variants and dynamic register hashes. The viewport/scissor gate
adds `GOLDSRC_VIEWPORT_READY`, sampled `GOLDSRC_VIEWPORT_FRAME`,
`GOLDSRC_VIEWPORT_GATE_COMPLETE` and `GOLDSRC_PIPELINE_GATE_COMPLETE`.
Acceptance requires the exact full/inset/scissor/restore plans, both real BSP
state keys, the complete catalog and every inherited Phase 3 resource,
readback, guard, fence and VideoOut invariant across exactly 10,000 frames.
Input continuity and presentation-spike criteria are not inherited from the
old noclip gate when they are unrelated to the render-state variable under
test.

The complete matrix adds `GOLDSRC_STATE_MATRIX_READY`, transition-time
`GOLDSRC_STATE_MATRIX_FRAME`, exactly 18
`GOLDSRC_STATE_MATRIX_READBACK` records and one
`GOLDSRC_STATE_MATRIX_COMPLETE`. Each draw record binds its semantic case,
stable key, shader variant and raw blend/depth/raster CX values. Each readback
is taken once per case and backbuffer only after fence zero plus the exact
VideoOut token. Acceptance requires all nine cases, both slots, nonzero visible
pixels, a distinct feature/control hash for every non-opaque case, inherited
resource/lightmap ownership and zero renderer errors through a gap-free BYE.

The orthographic gate adds `GOLDSRC_2D_READY`, two bookend
`GOLDSRC_2D_FRAME` records and `GOLDSRC_2D_COMPLETE`. The frame records bind
screen-space state keys 129/130 to `screen_2d`, report the alpha/additive draw
and index counts, separate HUD/console/menu/font quad counts, deterministic
atlas/layout hashes and the transient bytes owned by that frame slot. The
strict validator requires identical nonzero atlas/layout hashes in both
bookends, exactly two batches and 522 indices, the expected component counts,
10,000 clean frames, exact fence plus VideoOut retirement, intact guards, six
reclaimed resources and the dedicated gap-free completion BYE.

The lighting gate adds `GOLDSRC_LIGHTING_READY`, transition samples in
`GOLDSRC_LIGHTING_FRAME`, exactly eight `GOLDSRC_LIGHTING_READBACK` records and
`GOLDSRC_LIGHTING_COMPLETE`. The ready record binds an actual BSP face, draw,
style IDs, source-sample hash, atlas rectangle and proof camera. Each frame
identifies base, lightstyle, dynamic-light or combined composition, including
the style tick/scale, dynamic luxel count, patch hash and exact bounded upload.
Each mode is read back once per slot only after fence zero and the exact
VideoOut token. Acceptance requires real BSP sample planes, all four modes,
both slots, four distinct same-slot images, a converged final base patch,
inherited resource ownership, intact guards, zero errors and the dedicated
gap-free completion BYE.

The Studio gate adds `GOLDSRC_STUDIO_READY`, transition/bookend
`GOLDSRC_STUDIO_FRAME` and `GOLDSRC_STUDIO_DRAW` records, exactly ten
`GOLDSRC_STUDIO_READBACK` records and `GOLDSRC_STUDIO_COMPLETE`. The ready
record binds the private bundle SHA/size from boot to its bone, frame, geometry,
draw, texture and chrome counts. Frame rows expose interpolation endpoints,
blend, pose/skinned hashes and exact transient bytes; draw rows prove isolated
opaque and additive state keys plus instance, draw, index and texture-bind
counts. Acceptance requires control, textured, chrome, additive and combined
modes on both slots, changing poses, distinct feature/control and
combined/isolated images, CPU skinning in the transient ring, shared BSP-pool
residency, exact fence plus VideoOut retirement, six reclaimed resources,
intact guards, zero errors and the dedicated gap-free completion BYE.

The brush gate adds `GOLDSRC_BRUSH_READY`, transition/bookend
`GOLDSRC_BRUSH_FRAME` and `GOLDSRC_BRUSH_DRAW` records, exactly ten
`GOLDSRC_BRUSH_READBACK` records and `GOLDSRC_BRUSH_COMPLETE`. The ready row
binds the real model/entity counts and selected entity, source-mode, classname,
draw and index tuples. Frame rows expose changing independent transform hashes
and brush-only transient bytes; draw rows prove opaque key 4, alpha key 1 and
additive key 2 with exact per-instance counts. Acceptance requires five modes
on both slots, distinct feature/control and combined/isolated images, real BSP
submodels, exact fence plus VideoOut retirement, six reclaimed resources,
intact guards, zero errors and the dedicated gap-free completion BYE.

The visibility gate adds `GOLDSRC_VISIBILITY_READY`, transition/bookend
`GOLDSRC_VISIBILITY_FRAME` and `GOLDSRC_VISIBILITY_DRAW`, exactly eight
`GOLDSRC_VISIBILITY_READBACK` rows and `GOLDSRC_VISIBILITY_COMPLETE`. The ready
row binds the baked world-tree, PVS-row, leaf-reference and draw-bound counts.
Frame rows expose camera leaf, visible leaves, control/selected class counts,
independent PVS/frustum rejection counts and the transient mask hash. Draw rows
bind those counts to the actual filtered opaque, alpha-test and sky passes.
Acceptance requires both slots for control, PVS, frustum and intersection;
strict draw reductions; a nonzero framebuffer whose bright-pixel difference
from control stays within the declared 64-pixel tolerance; exact fence plus
VideoOut retirement; six reclaimed resources; intact guards; zero errors; and
the dedicated gap-free completion BYE.

The final integrated gate adds `GOLDSRC_PHASE4_SCENE_READY`,
`GOLDSRC_PHASE4_FINAL_READY`, four bookend `GOLDSRC_PHASE4_FINAL_FRAME` rows,
two `GOLDSRC_PHASE4_FINAL_READBACK` rows and
`GOLDSRC_PHASE4_FINAL_COMPLETE`. The scene row binds the selected real water
and glass entities to their source modes, draw and index counts. Final frame
rows bind the simultaneous lighting, effects, Studio, brush, visibility and
2D modes to exact draw counts, changing pose/transform hashes and transient
bytes. Acceptance requires the 60,000-frame process, a 600-frame combined
window, both retired slots, real water/glass, all Phase 4 component bookends,
the inherited pool/ring/lightmap/pipeline/2D completion markers, exact
fence/VideoOut ownership, intact guards, six reclaimed allocations, zero
errors and `goldsrc-phase4-final-soak-complete` as the gap-free BYE reason.

## Phase 5 ScePad evidence

The dedicated-engine input gate announces `pad_gate=1` in `XASH_BOOT`, then
emits `XASH_PAD_INIT` with the selected foreground user, pad handle and
64-record `scePadRead` policy. `XASH_PAD_GENERATION` records controller
assignment changes. Transition-only `XASH_PAD_ACTION` rows record active and
neutral movement/look plus pressed/released jump, crouch, use and fire.

`XASH_PAD_SUMMARY` closes the accounting over polls, records, maximum batch,
connection/interception state, event totals, action edges and read errors.
`XASH_PAD_TEARDOWN` proves the handle close and conditional UserService
termination. `XASH_PAD_COMPLETE` is accepted only when all six gameplay
actions occurred, reads remained error-free, ownership is exact and
`pass=1`. The engine must then report a zero result and close with a gap-free
`BYE`; a timeout, visual movement or process disappearance is not sufficient.

## Phase 5 SceAudioOut evidence

The dedicated-engine audio gate announces `audio_gate=1` in `XASH_BOOT`, then
records which user the artifact opened the port for in `XASH_AUDIO_USER`
(`system` or `foreground`, never an unrecorded fallback). `XASH_AUDIO_INIT`
carries user/type/index/handle, the three acquisition results, the volume flags
and value, and the input rate, output rate, format, channels and grain.
`XASH_AUDIO_RING_READY` adds ring capacity, prime level, staging sizes and the
147/160 ratio. `XASH_AUDIO_PATTERN` publishes the deterministic sequence and its
expected source hash, one `XASH_AUDIO_PATTERN_SEGMENT` row per segment.

`XASH_AUDIO_PROGRESS` appears at the first block and then every 64 blocks —
start, a few progress rows and a summary, never a line per block.
`XASH_AUDIO_UNDERRUN` fires once per starved episode; priming and the terminal
zero-fill are not underruns and never emit it.

`XASH_AUDIO_SUMMARY` closes the accounting: frames produced, consumed and sent,
blocks, deliberate silence, underruns, terminal padding, discarded frames, ring
wraps, producer rebases, high-water mark, the full format description, the PCM
hash before and after the resampler, and the Output error count.
`XASH_AUDIO_TEARDOWN` proves worker ownership with exactly one drain, one close
and one join. `XASH_AUDIO_COMPLETE` is accepted only when the consumed hash
equals the generated pattern hash, `sent` equals the exact resampled count plus
terminal padding, underruns and Output errors are zero, `ownership=exact` and
`pass=1`.

A successful `sceAudioOutOutput` returns the number of frames it accepted (256
at this grain) and so does the NULL drain, so `drain_rc` is checked for
non-negative rather than zero. The engine must then report a zero result and
close with a gap-free `BYE`. Telemetry alone does not accept the gate: the
operator's auditory confirmation is external evidence tied to the run id,
because the device cannot assert `audible=true` about itself.

## Phase 5 direct-memory evidence

The allocator gate announces `memory_gate=1` in `XASH_BOOT`.
`XASH_MEMORY_BEGIN` fixes the root size and allocation surface.
`XASH_MEMORY_RESOURCE` records kind, bytes, alignment, generation, content hash
and active ownership for command, buffer, texture and depth spans.
`XASH_MEMORY_COMPLETE` requires all four to return through exact-token
retirement, with no live resource, failed guard or allocation failure.

After `Host_Main`, `XASH_MEMORY_SUMMARY` separates live GPU state (which must be
zero) from CPU objects deliberately owned until process teardown. The latter
are recorded as `process_lifetime_cpu` and `process_lifetime_bytes`; the counts
must equal the live CPU totals. `XASH_MEMORY_TEARDOWN` then proves those exact
objects were reclaimed, the arena became empty, and reserve/allocate/map/
unmap/release each occurred exactly once with result zero. Acceptance requires
`memory_pass=1`, the normal engine completion BYE and the matching immutable
manifest; a successful mapping call or process exit alone is insufficient.

## Phase 5 thread/time evidence

The gate announces `thread_time_gate=1` in `XASH_BOOT`.
`XASH_THREAD_TIME_BEGIN` fixes the worker, iteration, clock and sleep sample
counts. `XASH_THREAD_RESULT` records every create/join/detach return code,
worker completions, distinct identities, mutex lifecycle/errors and the exact
locked counter. Acceptance requires create/join/detach `2/1/1`, two distinct
completed workers, counter `32768`, exact ownership and `pass=1`.

`XASH_CLOCK_RESULT` identifies `CLOCK_MONOTONIC` and reports reads, positive
advances, minimum step, span, errors and regressions. It must contain 8,192
reads, at least one advance and no error or regression.

Eight `XASH_SLEEP_RESULT` rows cover `nanosleep` and `usleep` at 1, 2, 5 and
10 ms. Each row carries 16 samples plus minimum, average, p95, maximum, errors
and early-wake count; all rows must pass. `XASH_THREAD_TIME_COMPLETE` closes
the aggregate ownership and timing result. The engine must then load its normal
workload, emit `thread_time_pass=1` in `XASH_EXIT`, and close with a gap-free
`BYE reason=xash-engine-boot-complete`.

## Phase 5 GPU/flip timing evidence

The renderer gate starts with `GPU_FLIP_TIMING_BEGIN`, which fixes the frame
count, raw GPU clock unit, selector-3 `RELEASE_MEM` packet, command order,
monotonic CPU clock and exact VideoOut-event observation point.

Every frame is checked in memory for consecutive `frame/slot/token`, strict
GPU timestamp progress and CPU order. `GPU_FLIP_TIMING_SAMPLE` persists frame
0 and every 600th retirement with the three CPU timestamps, the raw GPU EOP
tick and immediate inter-frame tick delta, plus the derived submit-to-fence,
submit-to-flip and fence-to-flip latencies. The GPU clock is deliberately
reported as `raw-ticks`, never nanoseconds.

`GPU_FLIP_TIMING_SUMMARY` requires 60,000 records and writes, 59,999 changes,
zero regressions, CPU-order errors and sequence gaps, and min/average/max CPU
latencies. Ownership remains `fence+exact-videoout-event`. Acceptance also
requires the integrated Phase 4 completion and a gap-free
`BYE reason=gpu-flip-timing-soak-complete`.

## Phase 5 libc-shim evidence

The engine artifact announces `libc_shim_gate=1` in `XASH_BOOT`.
`XASH_LIBC_SHIM_BEGIN` fixes the exact symbol set to
`__assert,getpwuid,dladdr`. Exactly three `XASH_LIBC_SHIM_RESULT` records then
prove the project-owned assertion formatter/reporter contract, the fixed `ps5`
identity for the requested uid, and the zeroed `dladdr` result that selects
whereami's `argv[0]` fallback. `XASH_LIBC_SHIM_END pass=1` closes the probe.

Acceptance additionally requires that the dynamic symbol table contain none
of those three names, while the full symbol table retains their local project
definitions. `XASH_EXIT` must carry `libc_shim_gate=1 libc_shim_pass=1`, the
ordinary engine workload must succeed and the immutable transcript must end
with a gap-free `BYE reason=xash-engine-boot-complete`. The clean probe checks
assert formatting and reporter selection without deliberately terminating the
hardware run; host tests and the retained `noreturn` implementation cover the
termination path.

## Phase 6 server PRX evidence

The combined dynamic-module gate announces `filesystem_prx=1 server_prx=1` in
`XASH_BOOT`. The server `XASH_PRX_LOAD` record must identify
`/app0/sce_module/server.prx`, four bounded mappings, the derived descriptor
count and a zero explicit start result. `XASH_SERVER_PRX_READY` binds lifecycle
state 1 to the actual engine-export count.

Immediately after `GiveFnptrsToDll`, `XASH_SERVER_PRX_ABI` requires the three
bits for `pfnCVarGetPointer`, `pfnCVarRegister` and `gpGlobals`.
`XASH_SERVER_PRX_ABI_SMOKE` then bookends two non-mutating calls from module
code back into the engine; each must complete with `result=1 pass=1`. The
ordinary raw console must still prove `Spawn Server: c1a0` and a started player
server, so a table that merely contains non-null pointers is not accepted.

At shutdown `XASH_SERVER_PRX_STATE` precedes the exact server unload.
`XASH_SERVER_PRX_COMPLETE` requires `active_modules=1`, proving the filesystem
module remained owned while the game DLL shut down. Its own state/unload/
completion records must then reduce the active count to zero. Acceptance
requires `server_prx=1` in `XASH_EXIT`, zero structured errors, the bounded
timeout and the normal gap-free completion BYE.

## Phase 6 MainUI menu PRX evidence

The menu gate announces `mode=client filesystem_prx=1 server_prx=1 menu_prx=1`.
`XASH_MENU_PRX_READY` binds state 1 to the two MainUI exports. The base and
extended ABI records require 16 and 12 callbacks, engine masks 63 and 15, and
non-null globals. Init, activation, at least one visible redraw and a non-black
`XASH_FRAME` prove execution rather than descriptor presence.

The optional legacy `libvgui_support.prx` miss is an explicit bounded warning
with complete rollback and `fallback=client-probe`; any `ERR` or `ERROR` record
still rejects the run. Shutdown must unload server, menu and filesystem in
that order with active-module counts 2, 1 and 0. `XASH_MENU_PRX_COMPLETE`
requires one init/shutdown, positive redraw/activation counts, both ABI passes
and exact ownership before the gap-free completion BYE.

## Phase 6 GoldSrc client PRX evidence

The client gate announces `mode=client filesystem_prx=1 server_prx=1
menu_prx=1 client_prx=1`. `XASH_PRX_LOAD` must identify
`/app0/sce_module/client.prx`, a bounded segment count, 48 descriptor entries
and zero start/load results. `XASH_CLIENT_PRX_READY` and the later state record
bind lifecycle state 1 to 42 actual GoldSrc exports.

`XASH_CLIENT_PRX_API` requires interface version 7, the six-bit host table
mask 63 and the four-bit module callback mask 15. Exactly two
`XASH_CLIENT_PRX_ABI_SMOKE` records prove live cvar and game-directory calls
from PRX code. One paired init and shutdown, positive video-init/frame/redraw
counts, raw Half-Life DLL and `Spawn Server: c1a0` proof, and non-black
software frames distinguish execution from descriptor presence.

Shutdown must unload server, menu, client and filesystem in that order with
active-module counts 3, 2, 1 and 0. `XASH_CLIENT_PRX_COMPLETE` requires both
ABI passes, one initialize/init/video-init/shutdown, positive frame/redraw
counts and exact ownership. `XASH_EXIT` retains every PRX flag before the
gap-free completion BYE. During this map-focused gate, the preserved menu API
and lifecycle remain mandatory but the standalone menu gate is the authority
for visible menu redraw.

## Phase 6 `ref_agc.prx` evidence

The final gate announces `mode=client ref=agc` and all five PRX flags in
`XASH_BOOT`. `XASH_REF_AGC_PRX_READY` fixes the module identity, RefAPI version
18 and the native backend before the engine table is bound. At shutdown,
`XASH_REF_AGC_PRX_STATE` must report state 5, engine mask 63, 600 frames,
nonzero frame hash/bright-pixel count, positive live renderer callback counts,
balanced begin/end calls and zero runtime/teardown results.

The separate `ps5-xash3d` stream starts within one second of the engine stream.
It must announce `REF_AGC_RUNTIME_READY`, run exactly 600 combined Phase 4
frames, produce two nonzero GPU buffer hashes and bright-pixel counts, retain
exact fence/VideoOut tokens, reclaim all six resources and close VideoOut,
direct memory and AGC exactly before its own gap-free BYE. This pairing keeps
engine ABI evidence and actual native presentation evidence distinct but
inseparable for acceptance.

The engine then unloads server, menu, client, renderer and filesystem with
active-module counts 4, 3, 2, 1 and 0. `XASH_EXIT` retains `ref_agc_prx=1` and
the engine stream closes normally. `tools/validate_ref_agc_prx_evidence.py`
checks both immutable manifests, their transcript hashes, start-time
correlation, private asset identities and the complete ownership chain.

## Phase 7 live frame capture and camera consumption

The 26-export `ref_agc.prx` capture checkpoint appends live producer evidence to
`XASH_REF_AGC_PRX_STATE`: `live_frames`, `live_view_frames`,
`live_view_hash`, `live_view_changes`, `live_map_serial`, `world_surfaces`,
`entity_peak`, `draw2d_peak`, `dropped_entities` and `dropped_2d`. The first
six positive counters distinguish a populated engine snapshot from mere
callback activity. Both dropped counters must be zero. `live_frames` must equal
`end_calls`, and `live_view_frames` may not exceed it.

The current 31-export consumer adds `REF_AGC_LIVE_FRAME_INPUT`,
`REF_AGC_LIVE_CONSUMED` and `REF_AGC_LIVE_COMPLETE`. Its final engine state
reports `consumed_frames`, `consumed_serial`, `consumed_view_frames`,
`consumed_camera_hash` and `consumed_camera_changes`. Acceptance requires the
producer and consumer frame/view totals to match, final serial to equal the
frame total, both camera hashes to be nonzero, a populated map and world, and
zero drops.

An acknowledgement is valid only after that serial's GPU fence is zero and its
exact VideoOut flip token has arrived. This live bridge therefore limits the
outstanding producer transaction to one native frame even though the reusable
standalone renderer supports two frames in flight. `REF_AGC_LIVE_COMPLETE`
must join the matched counts with two nonzero GPU hashes, nonzero bright
pixels, all six resources reclaimed, intact guards, zero errors and
`ownership=fence+videoout+ack`; teardown then closes with the gap-free BYE
reason `ref-agc-live-complete`.

These fields prove that the live camera affected every accepted native frame.
Entity and 2D counts remain capture evidence until their actual engine resource
and draw translations are bound to GPU-visible hashes.

## Continuous-runtime closure

The production runtime uses one persistent frame state machine and emits a
heartbeat every 3,600 completed frames. It has no frame chunks or automatic
exit. When the user invokes PS5 **Close Game**, the system terminates the title
and the stream may end without BYE; the PC records that as an operator-closed
runtime session. Automated soaks use the exact same binary and let the
supervisor close the exact title after observing the requested continuous frame
target and healthy invariants. Historical finite runs remain reproducible
evidence, not production architecture.

Current frame records report average GPU/VideoOut waits plus average and maximum
intervals between consecutive completed presentations. The 17 ms budget count
is evaluated on that interval, not on prepare-to-retire latency. Older captures'
`deadline_misses` field used the latter and must not be interpreted as dropped
frames.

## Development workflow

Run the companion `ps5logd` server on the PC, copy `dev.conf.example` to the
ignored `dev.conf`, fill in the development PC IPv4 address and package it as
`/app0/dev.conf`. Never add that file to the repository or bake its address into
the executable.

Alternatively, keep the configuration elsewhere and pass it only to the
ignored native artifact:

```sh
PS5LOG_DEV_CONF=/absolute/private/dev.conf make native
```

The builder copies that file to `dist/PPSA99996/dev.conf`; it never copies it
into the source tree.
