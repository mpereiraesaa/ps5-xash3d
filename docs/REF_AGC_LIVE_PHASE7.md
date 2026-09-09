# Phase 7 live RefAPI frame bridge and first consumer

Phase 6 proved RefAPI 18 module loading and the independent Phase 4 AGC
backend. It deliberately did not claim that the backend consumed the engine's
live world, entities, camera or UI. Phase 7 begins by making that boundary
explicit and measurable.

## Producer contract

`ref_agc.prx` owns a `RefAgcLiveStore`. The engine thread stages visible
entities through `R_ClearScene` and `R_AddEntity`, then captures the view pass
through `GL_RenderFrame` and 2D commands through `R_Set2DMode`,
`R_DrawStretchPic` and `FillRGBA`. `R_EndFrame` publishes one coherent frame
under a short mutex.

The ordering is intentional. Xash calls `CL_EmitEntities` before
`V_PreRender` calls `R_BeginFrame`, so `R_BeginFrame` resets only the current
view and 2D list. It must not erase the already staged entity list. A host test
reproduces this exact order.

Published frames copy all transient values. They do not retain engine-owned
entity or view pointers. Each entity includes its transform, animation and
GoldSrc render state plus a bounded model name. The world snapshot includes a
map serial, model identity, geometry counts, bounds and the presence of light
and visibility data. The bridge is bounded to 2,048 entities and 4,096 2D
commands and records every rejected item.

`PS5_RefAgcTakeLiveFrame` is the internal consumer boundary for the native AGC
backend. The 31-export camera-consumer module also exposes an exact acknowledgement
boundary: the producer publishes one immutable serial, the native thread takes
it, renders and retires its GPU fence plus matching VideoOut token, and only
then acknowledges that serial. The engine never overwrites an unacknowledged
snapshot.

The first consumer translates the live GoldSrc view into the coordinate system
used by the Phase 4 renderer. The BSP baker maps `(x, y, z)` to `(x, z, -y)`,
so both camera position and direction use that same transform. The renderer
accepts a view only after a nonzero map serial, populated world metadata, a
valid viewport and `RF_DRAW_WORLD` are present. It intentionally runs with one
frame in flight while a producer frame is outstanding so a VideoOut event can
never be coalesced past the serial being acknowledged.

This checkpoint proves live camera consumption and exact producer/GPU/VideoOut
pacing. Geometry, entities and 2D commands are still drawn from the baked Phase
4 proof resources. Translating live engine resources and replacing those three
fixture paths remains open; this document does not classify captured counts as
rendered live content.

## Texture-ingestion contract

The next additive module form has 40 descriptor entries. It replaces the null
renderer texture callbacks with an engine-owned, bounded store. Image input is
copied through Xash's image API, normalized to RGBA8 and retained under stable,
never-reused positive handles. Creates, updates and frees advance a monotonic
revision; a bounded visitor presents changes in revision order while holding
pixel lifetime stable. A failed visitor retains its last completed cursor, and
a future cursor is rejected rather than silently moving backward.

`R_NewMap` also walks every non-null world texture and resolves its
`gl_texturenum` back through the store. This closes the CPU-side RefAPI resource
boundary and removes the previous null callback behavior. It does not yet claim
that those pixels reside in direct memory or are sampled by AGC: the live GPU
texture arena, world geometry and entity/2D draw translation remain open.

## Accepted FW 12.02 camera-consumer gate

The final paired run began 52 ms apart:

- Engine: `20260908T213552886Z_PPSA99996_xash3d-engine_0x1181f06e47802`
- Renderer: `20260908T213552938Z_PPSA99996_ps5-xash3d_0x1181f0a138343`

The engine loaded `c1a0`, published 5,265 frames and captured a valid view in
5,256 of them. The consumer accepted all 5,265 serials and all 5,256 valid
views. The world snapshot reported 3,695 surfaces and map serial 1; the live
producer observed a peak of 22 visible entities and 3 2D commands, with zero
drops. The fixed unattended camera produced consumer hash
`951d313a61414eca` and zero changes, as expected without input.

Every acknowledged serial was first retired through its exact GPU fence and
VideoOut token. The renderer reported aggregate engine-frame hash
`298ec22eacb819b3`, GPU hashes `d0ed5a8903ca9609` and
`f96398a7af728fba`, 2,636,616 bright pixels, six reclaimed allocations,
intact guards and zero structured renderer errors. Server, menu, client,
renderer and filesystem unloaded with active counts 4, 3, 2, 1 and 0. Both
streams were gap-free and ended with `xash-engine-boot-complete` and
`ref-agc-live-complete` respectively.

Artifact hashes:

| Artifact | SHA-256 |
| --- | --- |
| Engine ELF | `4cdb0e32ef289aeb6fe81ddb4e613799bc764b005a08fdab9cdffe8795117eb1` |
| Engine fSELF | `66039dc87168004ff2f03cf66ff5e4c881da0d70083234ef8c1c5478359eb2ad` |
| `ref_agc` ELF | `c9a7474a4c2e6dee243c2d3c8c42621c52a3b9324d97f17924b83ddb207e9d79` |
| `ref_agc.prx` | `861aeef01497a9a22afeb831374b34d064572cbe604ee47a2dcd63cc6e4ab006` |
| `map.ps5bsp` (`c1a0`) | `e868f5b9d2ab98fbd671b426caf441a11666dd9816bfa603407f05531f85802f` |
| `model.ps5mdl` (`sphere`, sequence `fire`) | `d5b3a1f9b5c9035b02e678079b3586a5fe35987d55167dab27868050969b3e31` |
| Engine transcript | `b7ff4ec445511ee3296bcc5834a5c1e88e1f6a113fa6959e3a522482a548033c` |
| Renderer transcript | `483e57b32007cf12bc18af5451404e23ddf1d32d6d0eac3bf62b882b0cdfaa22` |

`validate_engine_boot_evidence.py` accepts the immutable 16-export Phase 6
module, the 26-export capture bridge and the current 31-export consumer. For
the current form, `validate_ref_agc_prx_evidence.py` additionally cross-checks
producer and consumer frame/view counts, final serial, camera hash, live map,
nonzero readback, exact `fence+videoout+ack` ownership, six-resource
reclamation and the dedicated completion BYE. The accepted paired result is:

```json
{"engine_frame_hash":"298ec22eacb819b3","frames":5265,"gpu_bright_pixels":2636616,"gpu_buffers":["d0ed5a8903ca9609","f96398a7af728fba"],"ownership":"exact","pass":true,"phase":7,"start_skew_ms":52}
```

## Accepted FW 12.02 texture-ingestion gate

The 40-export form passed in correlated runs started 55 ms apart:

- Engine: `20260908T221608881Z_PPSA99996_xash3d-engine_0x11a5189788703`
- Renderer: `20260908T221608936Z_PPSA99996_ps5-xash3d_0x11a518ccb39f0`

During the 5,217-frame run, the engine created 333 stable texture handles,
freed 109 and reached a peak of 329 active textures and 13,166,784 resident
bytes. At map capture, all 164 non-null `c1a0` world texture references resolved
to live store entries. The store recorded 442 revisions; the engine and native
consumer matched all 5,217 frame serials and 5,208 valid views with zero drops.
Both GPU buffers remained nonzero, the full five-module chain unloaded to zero,
the direct-memory engine arena reported exact teardown, and both `ps5log/1`
streams were clean and gap-free.

Artifact and evidence hashes:

| Artifact | SHA-256 |
| --- | --- |
| Engine ELF | `0efb8729b9d6547667fe1e0cd3d875ed8d37903ef4bd8e09822aee63bbecd553` |
| Engine fSELF | `c09cb469add09a7323795b2e5130d6b6a4afa545282c5869c0096e81c80fec2e` |
| `ref_agc` ELF | `e2e856e9f7ed6a9edc1e4e8f9d4786aed11a72044f66e5b5ebec016b5559b8f0` |
| `ref_agc.prx` | `5841de5a22ee4ab0b46ac104a77b5ba1a1d3ab16780a1469b20292c3107664bf` |
| Engine transcript | `8e2b48c27717057bf95a44ae46941c35792150e171a75ad4c276ac637e4c3a5f` |
| Renderer transcript | `15e73c2ec733481851bd72cdb3bc8b88bc651a8989f893dcc466f46d32a94ea5` |
| Engine manifest | `cacd2107e0cf2d46233d55693eb12c002da57ab7cd63222adad4a80cb378946e` |
| Renderer manifest | `7afa825bf51377be2b1aaeda4b15f7da237761b3f80a13dce30b8b8338e7a283` |

The fail-closed validators retain the immutable 16-, 26- and 31-export forms,
accept 40 only with positive resource accounting, and reject a world texture
count unless every reference resolves. The accepted paired result is:

```json
{"frames":5217,"ownership":"exact","pass":true,"phase":7,"ref_agc_texture_creates":333,"ref_agc_texture_handles":333,"ref_agc_texture_peak_active":329,"ref_agc_texture_peak_bytes":13166784,"ref_agc_texture_revision":442,"ref_agc_world_texture_refs":164,"ref_agc_world_textures_resolved":164,"start_skew_ms":55}
```

## Accepted FW 12.02 direct-memory texture gate

The next checkpoint adds a single 64 MiB parent allocation to the native
resource pool and suballocates the engine textures inside it at 256-byte
boundaries. Each active RGBA8 base level is row-padded, copied and flushed,
then receives a project-built GFX10.3 T# plus bilinear repeat/clamp S#.
Replacement and deletion are refused unless the preceding frame has completed
its exact fence and VideoOut token; the one-frame producer/ACK contract provides
that proof before the next revision is visited.

Correlated runs `20260908T223347685Z_PPSA99996_xash3d-engine_0x11b480e6bf9f8`
and `20260908T223347740Z_PPSA99996_ps5-xash3d_0x11b4811bb7bb1` began 55 ms
apart and passed 5,206 matched serials. The GPU cache applied revision 337,
created 330 images, deleted one, retained 329, copied 13,297,856 source bytes
into 14,171,136 pitched bytes and produced descriptor-sequence hash
`b1a857ec0c1edfd6`. Later CPU-only renderer shutdown advanced the source store
to revision 442 with 109 total frees; the validator therefore requires GPU
revision to be positive and no newer than the final CPU revision. It also
requires the GPU active count to cover all 164 world texture references.

The parent arena retired with the six prior resources, so the completion marker
records seven exact reclaims. Both streams were clean and gap-free, both GPU
readbacks remained nonzero, and native plus five-module teardown completed with
zero errors.

| Artifact | SHA-256 |
| --- | --- |
| Engine ELF | `0efb8729b9d6547667fe1e0cd3d875ed8d37903ef4bd8e09822aee63bbecd553` |
| Engine fSELF | `c09cb469add09a7323795b2e5130d6b6a4afa545282c5869c0096e81c80fec2e` |
| `ref_agc` ELF | `855dd54537fc3a4d5cd4faead70445f3e2c8a22f42a6437343b76747a65674ae` |
| `ref_agc.prx` | `5b2162afb617f62e1ebf99ada4b4e11ef37445c66db7e2af5f563a1ccad3ef86` |
| Engine transcript | `8b088dfe24edf981d80723ae11aec13e8468d720278bdc143b017b6ec364925e` |
| Renderer transcript | `193ef2045e74f739eab2d7a42f15b72e77c260675fc1f9f9bc6308071068e79d` |
| Engine manifest | `7aabd2ade226eaad1e76219e3b2d5f77e360ff7e977a8cc51914367934958586` |
| Renderer manifest | `a9a203d5952c26ee0ef556e9499468fb0e8ee6457ae9f4ee1ea29cff9394843e` |

This gate proves native direct-memory residency, descriptor construction,
cache flush and exact lifetime. It does not prove shader sampling from these
descriptors; that claim is closed by the following live-world gate.

## Accepted FW 12.02 live-world gate

The next checkpoint replaces the baked world draw list with geometry extracted
from the parsed `model_t` passed to `Mod_ProcessRenderData`. The producer
validates every surface, edge, surfedge, vertex and texture reference, supports
both BSP v30 and BSP2 edges, applies the same `(x, y, z) -> (x, z, -y)` mapping
as the camera, computes base and face-local lightmap coordinates, and publishes
one pointer-free owned snapshot. A failed publish cannot replace the preceding
revision.

The native consumer copies that snapshot into a dedicated 32 MiB parent arena.
Each surface receives an independent V# table, rebased `uint16_t` GPU indices
from `uint32_t` source indices, and a 24-DWORD texture table built from the live
texture cache. Per-surface rebasing avoids imposing a 65,535-vertex limit on the
whole map. Arena mutation and descriptor refresh are refused until the prior
fence and exact VideoOut token have retired. The Phase 7 PRX compiles the
Phase 4 pipeline catalog but no longer compiles or executes its baked
visibility, brush, studio, effect or procedural-2D gates.

Correlated runs started 55 ms apart and passed the bounded 20-second gate:

- Engine: `20260908T232706159Z_PPSA99996_xash3d-engine_0x11e30bf78529b`
- Renderer: `20260908T232706214Z_PPSA99996_ps5-xash3d_0x11e30c30c3800`

The live `c1a0` world produced 17,245 vertices, 29,565 indices and 3,695
surface draws. All 164 non-null world texture references resolved. The GPU
world arena retained 1,047,584 bytes, emitted one descriptor table per draw and
recorded source hash `ba427a54bcdc4cb9` plus upload hash
`934960d09d207e22`. The renderer completed 1,076 matched serials, including
1,067 valid views, with aggregate engine-frame hash `5724000629ec5fa0`,
framebuffer hashes `553ced9a3817b91b` and `0218ed9c11fbe6bb`, 4,066,868
bright pixels, zero errors, intact guards and all eight parent allocations
reclaimed. The engine then unloaded server, menu, client, renderer and
filesystem to active counts 4, 3, 2, 1 and 0.

Artifact and evidence hashes:

| Artifact | SHA-256 |
| --- | --- |
| Engine ELF | `8256012d69c65d8d3680c6cf8429e358ec009d557c6ec01a7cbb9dd02865de31` |
| Engine fSELF | `dbe3cd647c381bf679980c1888f96d88889ab43fd14cb5150e6936a442e4e329` |
| `ref_agc` ELF | `f7c45ec294d5537fca9c2666641079df8acccfdb40cbba1e665ef9ff1cc241f6` |
| `ref_agc.prx` | `01349df23adf9c42a3dd4e726f3f50d80d8e18e7250245bdb038833a48d61d26` |
| Engine transcript | `988b5695eb16ef0057ef14c713dfd2a212934bad559288bde3583ef912c089d6` |
| Renderer transcript | `d12af8a8dd91995a474417e0282693c5ecd4e85c13eddb83b6aeaef4755d4cf5` |
| Engine manifest | `ff46582662a2d36b19d98b0c66319e830d9bea7e3d0eca40119ef0ba1046b5a1` |
| Renderer manifest | `572bc2565fb6d7d5a92e550e50a6d4ec62569855e78f73f6ed1c4daf163ef45c` |

The fail-closed paired validator now requires `geometry=live-refapi`,
`textures=live-refapi`, positive geometry/index/draw counts, one texture table
per draw, nonzero source/upload hashes, direct-memory ownership and eight exact
reclaims for this form. It retains compatibility with the immutable 16-, 26-,
31- and earlier 40-export evidence forms. The accepted result is:

```json
{"engine_frame_hash":"5724000629ec5fa0","frames":1076,"gpu_bright_pixels":4066868,"gpu_buffers":["553ced9a3817b91b","0218ed9c11fbe6bb"],"ownership":"exact","pass":true,"phase":7,"ref_agc_world_texture_refs":164,"ref_agc_world_textures_resolved":164,"start_skew_ms":55}
```

This is deliberately a world draw-and-descriptor-binding gate. Its original
nonzero readbacks did not, by themselves, isolate texture sampling from the
clear pass. The compositor-visible A/B gate below supplies that stronger
evidence. Live lightmap atlas sampling, native sky/turbulent semantics,
translated entity draws and translated 2D/menu/HUD lists remain Phase 7 work
rather than being inferred from captured entity or 2D counts.

## Accepted FW 12.02 compositor-visible world gate

The original live-world artifact submitted and retired cleanly but Remote Play
captured a completely black 1920x1080 frame. A magenta-clear diagnostic proved
that the live world covered the clear, and a no-log `nanosleep` probe isolated
the presentation difference to a scheduler handoff rather than to logging.
Three serial launch/capture trials then fixed the placement boundary:

| Placement | Result | Capture SHA-256 |
| --- | --- | --- |
| No handoff (`ref_agc.prx` `01349df2...`) | black | `e95c0eda406aab59485803a024616ea5f0c68293f83a67969d2916f1218d57b8` |
| 10 ms after `load_bsp_bundle` | black | `e95c0eda406aab59485803a024616ea5f0c68293f83a67969d2916f1218d57b8` |
| 10 ms after bundle load and `REF_AGC_LIVE_CAMERA_READY` | textured `c1a0` tram interior | `1ee3578b517bee368f72805d3a9ecd339a5f7de65de462bbefd7a6d7a19c1850` |

The accepted implementation performs that bounded handoff before command and
pipeline planning, retries `nanosleep` only for `EINTR`, reports
`live_camera_settle_ns=10000000`, and fails before submit on any other error.
A host source contract fixes this placement and rejects the temporary visual
probe and the failed post-bundle placement. This is a hardware-proven boundary,
not a claim about an unobserved firmware-internal cause.

The final artifact was captured only after `launch-xash3d` had verified
`PPSA99996` active. Correlated runs began 55 ms apart:

- Engine: `20260909T005027224Z_PPSA99996_xash3d-engine_0x122bd226f4e00`
- Renderer: `20260909T005027279Z_PPSA99996_ps5-xash3d_0x122bd25b72b53`

The fail-closed paired validator accepted 1,076 matched frames, 1,067 live
views, 17,245 vertices, 29,565 indices, 3,695 draws and all 164 world texture
references. Both streams were clean and gap-free, all eight parent allocations
were reclaimed, the five modules unloaded to zero, and renderer errors were
zero. The visible capture therefore closes base-texture sampling and
compositor presentation for the live world; it does not close lightmaps,
special-surface semantics, entities, viewmodel or 2D/UI.

| Artifact | SHA-256 |
| --- | --- |
| Engine ELF | `8256012d69c65d8d3680c6cf8429e358ec009d557c6ec01a7cbb9dd02865de31` |
| Engine fSELF | `dbe3cd647c381bf679980c1888f96d88889ab43fd14cb5150e6936a442e4e329` |
| `ref_agc` ELF | `a28574b13c63c2a95771888769a855f886c23070680ee1aca2616ea7970627e3` |
| `ref_agc.prx` | `4132e6574b64de14982b1b4b80c7c00815dae932fe1395fda059d004caf1e8e1` |
| Engine transcript | `f3127207da6f7ced41c88771a30d3667e15f0d3da4854d5b9943718ee1928318` |
| Renderer transcript | `b54fd915b19be59848fbdd73224a8947762eff7f117f4afb0b7f3040bdfd1034` |
| Engine manifest | `ad0f869f962d2226ad19c7f499e34fda2472eadf45a410eec443470d4341850b` |
| Renderer manifest | `dfc40743871bb10a0dc0464854df074be6a8628d180f83dbebe5ba9d88d5e209` |

The accepted validator result is:

```json
{"engine_frame_hash":"5724000629ec5fa0","frames":1076,"gpu_bright_pixels":4066868,"gpu_buffers":["553ced9a3817b91b","0218ed9c11fbe6bb"],"ownership":"exact","pass":true,"phase":7,"ref_agc_world_texture_refs":164,"ref_agc_world_textures_resolved":164,"start_skew_ms":55}
```

## Accepted FW 12.02 live-lightmap gate

The producer now builds one deterministic owned lightmap atlas directly from
the parsed engine world. It validates surface extents and sample spans,
combines the active `MAXLIGHTMAPS` style planes through the engine lightstyle
values and gamma table, reserves one-texel gutters, duplicates every edge and
publishes normalized atlas coordinates with the pointer-free world snapshot.
The packing width is fixed at 1,024 texels, the height is the smallest fitting
power of two up to the guarded 4,096-texel bound, and a failed build cannot
replace the prior revision.

The native consumer places the RGBA8 atlas in the existing 32 MiB world
direct-memory arena with a 256-byte-aligned row pitch, flushes the owned span,
and creates one clamp-plus-bilinear descriptor. Lightmapped opaque draws bind
the already hardware-proven Phase 2–4 `bsp_resource` pipeline and lightmapped
masked draws bind `bsp_alpha_test`; unlit classes retain the GoldSrc pipeline
runtime. This reuses native AGC contracts and introduces no OpenGL emulation
layer.

The final five-module bundle was staged, verified and promoted as one
transaction. Launch verification then identified `PPSA99996` active, and the
CLI Remote Play stream was independently captured while known live. Correlated
runs began 53 ms apart:

- Engine: `20260909T022301539Z_PPSA99996_xash3d-engine_0x127ca54ee550a`
- Renderer: `20260909T022301592Z_PPSA99996_ps5-xash3d_0x127ca581d165f`

The strict paired validator accepted 1,075 matched frames, 17,245 vertices,
29,565 indices and 3,695 lightmapped draws. The 1024x256 atlas used a 4,096-byte
row pitch and 1,048,576 bytes, with RGB sum 66,594,990, 186,051 nonzero texels
and channel range 0..255. Both framebuffer slots produced
`49b1297de5cef0a0`; their ordered aggregate is the nonzero engine/renderer hash
`a3219a480a7a1c41`. Identical slot images are valid, so the aggregate hashes
the ordered pair rather than XORing it. All eight parent allocations were
reclaimed, renderer errors remained zero and the five modules unloaded in
order to zero.

The accepted result is:

```json
{"engine_frame_hash":"a3219a480a7a1c41","engine_run_id":"20260909T022301539Z_PPSA99996_xash3d-engine_0x127ca54ee550a","frames":1075,"gpu_bright_pixels":3989406,"gpu_buffers":["49b1297de5cef0a0","49b1297de5cef0a0"],"ownership":"exact","pass":true,"phase":7,"renderer_run_id":"20260909T022301592Z_PPSA99996_ps5-xash3d_0x127ca581d165f","start_skew_ms":53}
```

Artifact and private-evidence hashes:

| Artifact | SHA-256 |
| --- | --- |
| Engine ELF | `8256012d69c65d8d3680c6cf8429e358ec009d557c6ec01a7cbb9dd02865de31` |
| Engine fSELF | `dbe3cd647c381bf679980c1888f96d88889ab43fd14cb5150e6936a442e4e329` |
| `ref_agc` ELF | `d9f58ce2dd4b7a868e48ad6e6dbfa5234c0690c1fa0d7d22b13fbd07740fd0ad` |
| `ref_agc.prx` | `424faba887f0b86980d436022a123e5fd04405d22ea98701b2d1f7b7331e2ed4` |
| Engine transcript | `b36a05dacb88caf7aba56428844a0365cc8781310602cac0171440177025bf8c` |
| Renderer transcript | `b8e40713dc664181cd2214960d94fb8e0eff5a6b59721f4dc060904417e09e3b` |
| Engine manifest | `e459bc1ae18910ad9b2d71c867ea3c99e09283d56faba97c68edeccb4c30ea09` |
| Renderer manifest | `7a587ae6a151c022b788f191a693bd08f2349022cc0e471b1b77216f1fa9e9a4` |
| Transaction journal | `89428b3481202b699c3ff4e0b5a28368f9c62539c8a42bece530eb31b5e0cb96` |
| Launch-verified visible capture | `2b8bd9ea8dd5345463f7bd9363ee79df36ae77af76cc635c54b8859699cdbfd7` |

A separate valid-stream no-lightmap control produced a visibly unlit scene
(`f8c457fc5fa90042e695871994189776cb0c67c1b7628482d85e0073e404c758`).
Earlier byte-identical black captures were discarded after a PS home capture
proved that the CLI Chiaki process itself was stale; they are not renderer
evidence. The accepted image came only after a fresh visible home-frame check.

This closes live engine-lightmap atlas construction, direct-memory residency,
native pipeline binding and compositor-visible sampling. Native sky/turbulent
semantics, entities, viewmodel and 2D/menu/HUD translation remain open.
