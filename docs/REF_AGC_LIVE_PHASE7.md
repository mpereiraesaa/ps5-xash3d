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
