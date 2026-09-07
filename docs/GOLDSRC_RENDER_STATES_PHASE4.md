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

The complete state-matrix gate is now closed too. Nine deterministic cases hold
the same camera for 300 frames each while selecting opaque, alpha, additive,
alpha-test, depth-write off, cull front, cull back, fog and lightmap-off states.
Every case renders through the real BSP draw path on both backbuffers. Its
constant buffer supplies an explicit render color and fog color/density, and
the framebuffer hash is captured only after both the GPU fence and exact
VideoOut token retire. Run
`20260906T223113472Z_PPSA99996_ps5-xash3d_0x7dfb90d3b053` completed
10,000/10,000 frames with all nine cases, 18 post-retirement readbacks,
distinct feature/control images in both slots, intact guards, six reclaimed
allocations and zero renderer errors. The ELF/fSELF hashes are
`d914bcf26b5aa3e0ca17eb3c99a10cdb3abb929bf89f9817e96f7640f9baf2e6`
and `31de1cf508c26f36217bb04aaa87140e191a71880a95e124a504d29c62441b9a`;
the transcript SHA-256 is
`d875d6793d92407e297daef313c7ad24ab84d5ada3abc0b15fd04f2381805ef3`.
Compositor-visible captures confirmed the ordinary lightmapped scene and the
fog-selected scene from that exact artifact.

The orthographic 2D gate is also closed. `goldsrc_2d` builds a 128×32
procedural RGBA8 glyph atlas, an orthographic constant buffer, 32-byte colored
vertices, 16-bit indices and all descriptor tables inside the current
framebuffer slot of the Phase 2 transient ring. It emits one source-alpha batch
for translucent HUD, console, menu and font quads, followed by one additive
batch for the crosshair. Both bind the compiled `screen_2d` variant through
semantic keys 129 and 130; depth, fog, lightmaps and culling remain disabled by
the state contract.

Run `20260906T225115588Z_PPSA99996_ps5-xash3d_0x7f137394ac44` completed
10,000/10,000 frames with 2 draws and 522 indices per frame, deterministic
atlas/layout hashes in both slots, 47,312 total transient bytes per frame,
exact fence/VideoOut retirement, intact guards, six reclaimed allocations and
zero renderer errors. The ELF/fSELF hashes are
`b7b2ef1e9cf4679bbe5edea37a8511aecdac3352252c7d48ffea0d6e70ac3dde`
and `f391dbbae2f90a34f64a3593418be137a4efd5099651254724144bf1665404b2`;
the transcript SHA-256 is
`12c94237d1aa4fb5e762372549e7e803f2df7781468b862af5a70a513237ac39`.
A compositor-visible capture from that exact parked artifact showed the BSP
world beneath the translucent console, menu and HUD, with readable bitmap text
and the additive crosshair. The direct Chiaki stream was stopped by its exact
PID and the exact `PPSA99996` close helper then left no BigApp while all four
console services remained healthy.

The lightstyle/dynamic-light gate is now closed as well. The version-3 BSP
bundle remains backwards compatible while optionally carrying `LMFM` face
metadata and `LMSP` source sample planes. The baker preserves every real
GoldSrc style plane rather than flattening the atlas. A checked composer
rebuilds one selected wall patch from those planes, applies the original
GoldSrc scale domain and a moving face-local radial dynamic light, then sends
the result through the existing Phase 3 bounded, fence-retired uploader.

Run `20260906T233103794Z_PPSA99996_ps5-xash3d_0x813f7d9b54cf` used the
privately owned `c1a0e.bsp` source and completed 10,000/10,000 frames. Its
enriched bundle contains 3,052 lightmapped faces, 734,229 source-sample bytes,
528 multi-style faces and 1,084 style layers. The proof camera selected wall
face 203 (draw 379), whose three actual styles are 0, 33 and 35. The four
600-frame modes were base, lightstyle, dynamic light and combined; after each
slot's initial full upload, every frame uploaded only the 704-byte 11×16 patch
through a 61,484-byte aligned acquire span.

The title captured exactly eight post-retirement framebuffer readbacks: every
mode on both slots, after fence zero and the exact VideoOut token. All four
same-slot images were distinct. It ended with guards intact, six reclaimed
allocations, 266 gap-free records and zero renderer errors. Exact artifacts:

- native ELF: `7cf6d6b7c0e4ace01781de5f8c63f18b8a7be09b2b5113cdd0c1bf215f0f62dd`;
- signed fSELF: `dd66e6c4659b8bc4453720c003c549683c884d40d9906c3b7e9859f6fff14506`;
- private enriched bundle: `0e6396cf2dbec287c4e2bc28f90a90e8f5cb26b98f43ebcd539dba7d9c171105`
  (9,573,888 bytes);
- transcript/manifest: `f1c69e8d1825275da6716aeff6f0620c516f8fb4e708a45b18f8e19cd00e620b` /
  `197c0086ac8e72e91ff01465c513a029d95c39e690e2d611231a35c12cd10060`.

Four compositor-visible Chiaki CLI captures were taken during the mode cycle;
their SHA-256 values are `6fbb3283006e563631d29f22c1e2e39fadb795b8cad6a90733f1baca70964890`,
`d08ac11fdfacb55e6aeeae6d2c375b9e219a627c673918fd1a2cdef5ade1e18d`,
`8dd5ba9ad6d416c17365b5255b5e4099c16f264f0119d67095cf0c12cea476b1`
and `145b5d9b1ee5b9bfa822f228ae21dc71cc89f77c7d76f31ef98995bde448f4ba`.
The stream reused the registered console entry without pairing and was closed
by exact isolated PID. Exact-title closure then left no BigApp, all four
services healthy and only `PPSA99996`, `PPSA99997` and `PPSA99999` in the
local homebrew title range; `PPSA99998` remains absent.

The sprite/particle gate is now closed too. `goldsrc_sprite_particles` creates
a deterministic 64×32 RGBA8 atlas and one camera-facing sprite, 24 translucent
smoke particles and 48 additive spark particles. Its constants, descriptors,
292 vertices and 438 indices are rebuilt inside the current framebuffer slot
of the existing transient ring. The semantic cache selects 3D alpha key 1 and
additive key 2 with depth writes, culling, fog and lightmaps disabled. Four
600-frame modes isolate control, sprite, particles and their combined result.

Run `20260906T235831459Z_PPSA99996_ps5-xash3d_0x82bf1cd8fb89` completed
10,000/10,000 frames on FW 12.02. It issued 0/1/2/3 effect draws and
0/6/432/438 indices for those modes, kept effect allocations at 18,772 bytes
per frame and captured exactly eight post-retirement framebuffer readbacks.
Every feature hash differed from its same-slot control, and combined differed
from both isolated features. Exact artifacts:

- native ELF: `b88df7b004495d828db7a594d1579a56fe4925578d384bef01b95b8ae5d63778`;
- signed fSELF: `33e804f669a7acdddaf8a38a6a3f51ee6b0ae2d946bd0fc97a347596d33dcf2a`;
- private bundle: `0e6396cf2dbec287c4e2bc28f90a90e8f5cb26b98f43ebcd539dba7d9c171105`
  (9,573,888 bytes);
- transcript/manifest: `e6d77a34f5276f59c12ac987f67a7720394b88c2788ee06e72f9f6ec8b9d4a05` /
  `6df527c58ea91bd060f3c570eda910d383b40a2018b5cb17751d128256a35899`.

Three compositor-visible CLI-stream captures isolate sprite, particles and
combined modes. Their SHA-256 values are
`da592df0f150849fe1008ab57115e0ff14c6d742e1a9fe06b09f06b73a2cb980`,
`d11be0a14328f34714aa380a112926a6e4b362a18992d38712aa5edda5155b75`
and `7034111a275c25f02e78e089ca4f4aa6c0853fa121b9281e6b3e19cc979ff730`.
The 284 records were gap-free, renderer errors stayed at zero, guards remained
intact and six allocations were reclaimed. Chiaki's registered entry was
reused through the isolated CLI process and closed by exact PID; exact-title
closure left no BigApp and all four services healthy.

This closes implementation gates 1–5, not Phase 4. Studio models, brush
entities and PVS/frustum culling remain open. `PPSA99998` remains absent.
