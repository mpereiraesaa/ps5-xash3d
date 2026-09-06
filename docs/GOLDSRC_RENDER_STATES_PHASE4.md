# Phase 4: GoldSrc render states

Phase 4 turns the Phase 3 textured BSP viewer into a renderer that can express
the visible GoldSrc scene contract. It is complete only after every gate below
has passed on the host and on the FW 12.02 console. A host-only implementation
or one successful screenshot does not close the phase.

## State contract

`goldsrc_render_state` is the platform-neutral source of truth. It preserves
the six numeric GoldSrc entity modes and represents the complete state space:

- opaque, source-alpha, additive and alpha-test blending;
- depth writes enabled or disabled while retaining depth testing for 3D;
- front, back or no face culling;
- fog enabled or disabled;
- lightmap multitexture enabled or disabled;
- a separate depth-free, uncullled, unfogged 2D path.

`goldsrc_pipeline_cache` expands this into 96 unique 3D permutations and three
2D permutations. Every entry has a stable key, render pass, shader variant and
dynamic CX register plan. Masked draws use the masked shader family; fog and
lightmap each select an explicit shader feature variant. Translucent and
additive 3D passes are classified for back-to-front ordering.

`ps5_goldsrc_render_state` owns only these GFX10.3 fields:

- `CB_BLEND0_CONTROL` (`0x1e0`), including separate alpha equations;
- depth enable/write bits in `DB_DEPTH_CONTROL` (`0x200`);
- front/back cull bits in `PA_SU_SC_MODE_CNTL` (`0x205`).

Depth compare, stencil, winding and all remaining raster bits come from the
known-good base pipeline and are preserved. Alpha blending uses
`SRC_ALPHA / ONE_MINUS_SRC_ALPHA`; additive blending uses `SRC_ALPHA / ONE`.
The 2D path disables depth and culling without mutating unrelated base state.

`ps5_viewport_scissor` builds checked viewport and generic-scissor CX updates
for use between draws. Rectangles must be non-empty, fit the framebuffer and
fit the hardware coordinate fields. It supports a viewport and a narrower
scissor so the hardware gate can prove both state changes in one frame.

## Required implementation gates

The phase advances in independently attributable gates:

1. Compile and bind the explicit surface/masked shader variants and exercise
   opaque, alpha, additive, alpha-test, depth-write, culling, fog and lightmap
   switches through the checked cache.
2. Change viewport and scissor between draws in one frame, then restore the
   full-frame state before presentation.
3. Add an orthographic blended 2D batch for HUD, console, menu and fonts.
4. Apply lightstyles and dynamic lights to the lightmap atlas through the
   bounded, fence-retired Phase 3 upload path.
5. Stream sprites and particles from the existing transient ring.
6. Render animated studio models with CPU skinning, per-model textures,
   chrome and additive modes.
7. Render brush entities with independent transforms and render modes.
8. Cull world and entity work with BSP PVS plus camera frustum tests.

The final visible scene must contain water, glass, sprites, particles, an
animated studio model and HUD produced by the port's own loaders. No borrowed
host renderer is acceptable evidence.

## Evidence and ownership

Each hardware gate must keep the existing two-slot resource contract: a draw's
pipeline tables, descriptors, transient allocations, lightmap updates and
geometry remain live until both its GPU fence and exact VideoOut token retire.
Structured telemetry must identify the selected state key, pass and shader
variant, draw counts, culling counts, per-frame transient/upload bytes and the
paired control/feature readback hashes. A gate accepts only gap-free frames,
intact guards, expected visible deltas and zero renderer errors.

The final Phase 4 soak re-runs the complete scene without weakening any Phase
0–3 invariant. Xash3D remains `PPSA99996`; the frozen Gears demo remains
`PPSA99997`; the retired historical `PPSA99998` title is not installed.

## Current checkpoint

The semantic state model, complete 99-entry cache, exact GFX10.3 dynamic-state
translation and checked viewport/scissor builder pass the full host suite. All
eight surface/masked feature variants and the orthographic 2D shader compile
for `gfx1013`; their manifests prove nine non-empty pipelines, the expected
masked kill bits and eight distinct surface/masked pixel programs. Native
storage embeds the 18 stage blobs in a generated typed catalog, and a checked
slot constructor creates, links and builds both framebuffer pipelines for all
nine variants without changing the frozen four-entry Phase 3 table.

The real BSP draw path now selects Phase 4 opaque-lightmap key 68 and
masked-lightmap alpha-test key 71. The viewport gate changes to a 1280×720
viewport and a nested 1120×640 scissor between the full-frame clear and the BSP
draws, then restores the 1920×1080 state before the final pass. Run
`20260906T220730780Z_PPSA99996_ps5-xash3d_0x7cb052db2ae7` completed
10,000/10,000 frames at 59.94 fps with two frames in flight, exact GPU fences
and VideoOut tokens, intact guards, six reclaimed allocations, zero renderer
errors and no presentation intervals over budget. It observed all 99 semantic
entries, all nine native shader variants, both real BSP state keys and every
required viewport marker. The transcript SHA-256 is
`daee720296ea418ec0e9d887de0937400291b7351b24228d15763b2c03d68708`.
A compositor-visible capture from the exact inset artifact confirmed the
mid-frame state on screen before exact-title closure.

This closes the native binding and viewport/scissor checkpoint, not Phase 4.
Actual draw/readback gates for alpha blend, additive, depth-write, all cull
modes, fog and lightmap off remain open, followed by the 2D, lighting,
sprite/particle, studio, brush-entity and culling gates.
