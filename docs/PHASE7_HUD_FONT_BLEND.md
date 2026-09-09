# Phase 7 HUD/font blend gate — in progress

Baseline: merged texture-memory policy PR #26 (`70ebea8`). This gate does
not change audio, Studio, asset packs or the accepted memory policy.

## Source findings

- `engine/client/cl_font.c` maps font mode 0 to `kRenderTransAdd` and calls
  `GL_SetRenderMode` before drawing. The port inherited a no-op callback and
  hardcoded `kRenderTransTexture` in every stretch-pic command. This loses
  the requested additive composition that should leave black font pixels
  transparent against the scene.
- Upstream `ref/gl/gl_backend.c` distinguishes opaque, alpha blend,
  alpha test, additive and the engine-only `kRenderScreenFadeModulate=0x1000`.
  Modulate uses source ZERO / destination SRC_COLOR, not ordinary alpha.
- The live compositor currently maps `kRenderTransAlpha` to alpha blend;
  screen-space render-state validation rejects alpha test, and the existing
  screen shader has no discard. Merely replacing the callback cannot close
  the full gate.
- Pinned `ref/gl/gl_local.h` defines `DEFAULT_ALPHATEST` as **0.0f**;
  `gl_opengl.c` initializes `GL_GREATER`. Screen fonts must discard alpha
  zero after texture/color multiplication, not inherit the surface shader's
  0.5 cutoff. Use a dedicated screen shader with the existing vertex ABI.
- `ref/gl/gl_context.c:CL_FillRGBA` blends additive only for TransAdd,
  otherwise alpha, then disables blending. Its color and state side effects
  must be considered alongside R_Set2DMode's alpha-test/color setup.

## Implemented preparation

`GL_SetRenderMode` now has a real adapter callback. An owned, host-tested mode
state preserves all six standard modes and the modulate extension; unknown
modes fall back to opaque as upstream does. Each stretch-pic command snapshots
that mode instead of forcing alpha blend. Static ABI assertions bind the
numeric contract to the actual pinned engine definitions.

Tests cover ordered normal/alpha/additive/reset/masked/modulate sequences and
invalid-mode fallback. The existing full host suite passes, but this does not
prove missing GPU modes or visual correctness. No console launch or Remote
Play was performed for this partial change.

The render-state layer now accepts masked 2D and a screen-only modulate
extension. Existing 96 3D permutations and numeric keys remain unchanged;
the screen keys are 128, 129, 130, 131 and 384..387. Invalid/negative blends
and modulate in 3D are rejected. Register tests cover depth/cull disabled
for masked and modulate screen draws. Modulate encodes ZERO/SRC_COLOR for
RGB and ZERO/SRC_ALPHA for alpha (`0x64000200`); SRC_COLOR=2 was checked
against AMD PAL's `gfx9_plus_merged_enum.h`.

The pipeline cache now contains 104 entries: the unchanged 96 3D entries
plus eight screen entries. Alpha test can accompany opaque, alpha, additive
or modulate composition. The dedicated `screen_2d_masked` shader is generated
from the ordinary screen source with only a post-multiply alpha-zero discard;
vertex layout and color transport stay unchanged. Asset/catalog generators,
build rules and manifest validation include all ten shader variants. Host
tests check shader selection, unique keys, source generation and invalid
template rejection.

Local LLPC compilation for gfx1013 succeeded, without relocations. Compiled
metadata reports `kill_enable=true` for the new shader. Its pixel ISA is
200 bytes, SHA-256
`2cff6aeca534a4d7a1574e8815224f46ff3a60b0eabc5146e1ad9cabd9b73149`;
pre-raster ISA is 372 bytes, SHA-256
`98c1f5e2d57d705f1699a11d5feb5a9a8d210cc81bb14c1fb8aec675bfbe5ce9`.
The compiled ten-variant manifest validator and `make all` pass.

The live compositor now translates captured stretch-pic modes, including
masked and modulate, instead of defaulting to alpha. Preflight rejects an
invalid stretch mode before allocating transient storage. Fill commands use
additive only for TransAdd and alpha otherwise, matching `CL_FillRGBA`'s
argument semantics. Host tests exercise all modes, contiguous merging of
equivalent alpha modes, order/vertex preservation, opaque restore after
modulate and unchanged transient usage on invalid-mode rejection.

Live 2D telemetry is schema 2 with distinct masked/modulate counters; the
paired evidence validator requires both counters and includes them in exact
batch accounting. Historical schema 1 remains supported. Tests accept a
mixed schema-2 sample and reject missing, negative, overcounted counters and
unknown schemas. `make all` passes.

The adapter now owns independent alpha-test and 2D-entry state alongside its
color. FillRGBA leaves its color active and disables blending, preserving
alpha test; R_Set2DMode enables alpha test and resets white only on an actual
transition into 2D. Repeated enable calls do not reset color. Host tests cover
these transitions, including retained modulate blending on 2D entry.

The existing command `enabled` word retains its meaning for MODE commands;
for draw commands it now carries the independently captured alpha-test flag
(0/1). This preserves struct size but is an internal protocol extension, so
producer and consumer must be built/deployed together. The compositor rejects
non-boolean flags and selects a masked shader with the requested blend
registers when both are enabled. Telemetry classifies every alpha-tested batch
as masked (mutually exclusive counters), including masked blended batches.

The complete client/AGC engine build passed with all five project PRXs,
5-second MainUI entry and 180-second map-relative timeout. Audio remains
disabled for this isolated HUD gate. Renderer ELF SHA-256:
`88ca05fbf445e532fa91e92c5d420b43dc9b90fbb73a0cd07fe0821f669e66b4`;
renderer PRX SHA-256:
`23672468d2920dc78096a7224744d254be1a82c590038b974252084ba9864ad9`.
An exact byte comparison located the new 200-byte pixel shader in the ELF
at file offset 295680, matching the shader hash above. The engine ELF and
SELF retain the accepted texture-policy identities; `--dyn-syms` still has
no strcasestr import.

This is not hardware or visual acceptance. Next: paired hardware QA with
the operator, followed by documentation/HTML and green PR integration.
The candidate was transactionally deployed and launched on 2026-09-09.
All nine remote files passed exact SHA-256 verification. Paired run IDs:
`20260909T131224948Z_PPSA99996_xash3d-engine_0x14b3a1552be1d` and
`20260909T131225006Z_PPSA99996_ps5-xash3d_0x14b3a18bdc20e`.

Operator feedback: MainUI looked correct and the chapter title appeared
without its black background. At disappearance, the operator noticed a very
brief white patch/flash around the title, then the scene returned to normal.
Follow-up confirms a rectangular shape around the text, not just bright
letters, and a very short duration. No second launch was requested or made.
This is partial visual acceptance only: title fade-out remains open. The
flash cause is unconfirmed; do not dismiss it as a normal effect or declare
the entire HUD gate complete.

The run completed 10,996 renderer frames, nine exact resource reclaims,
five PRX unloads, renderer teardown result zero and clean paired BYEs.
The paired evidence validator passed with live lightmap, 2D, menu, brush and
Studio requirements. MainUI accounted for 220 frames, 95,534 quads and
27,538 draws before map entry. Independent post-run status found no BigApp
and all four required services healthy. These facts accept the resource
regression, not the fade-out pixels or additional controller QA.

Current logs contain batch modes/counts and command/layout hashes but not
per-draw color, UV and texture identity. They cannot identify the white
rectangle's offending draw. Next diagnostic should record those values in
a bounded window around title disappearance and compare command state with
the bound GPU state; do not infer an alpha/color root cause from shape alone.

`XASH_HUD_TRACE=1` now enables that diagnostic in the renderer (default 0;
other values are rejected). `REF_AGC_HUD_BATCH` records serial/time, batch,
texture handle/fill, index span, selected pipeline key/shader/blend register,
vertex RGBA ranges, screen bounds, first-quad UVs and a hash of all batch
vertices. It runs only for valid map views at engine time 0..30 seconds and
stops after 4,096 records, emitting `REF_AGC_HUD_TRACE_LIMIT` on truncation.
This is batch-level evidence, not a screenshot or a per-glyph dump; mixed
vertex colors may require a narrower second probe. Synchronous logging can
affect timing, so this build is not performance acceptance.

The diagnostic native build and full host suite pass. Both trace markers were
verified in the ELF with `strings`. Diagnostic renderer ELF SHA-256:
`26bb8b6246d82dfa0445cc669672e28b444ae368028997136cb8d8f7b3d223f2`;
diagnostic PRX SHA-256:
`96e2c9c1eda3f5d65db958af81e8f0de17a70274afea57b11dd00a10156bfc50`.
This artifact was transactionally deployed with all nine hashes verified and
launched after a healthy no-BigApp preflight. Diagnostic run IDs:
`20260909T132208575Z_PPSA99996_xash3d-engine_0x14bc1f789c889` and
`20260909T132208628Z_PPSA99996_ps5-xash3d_0x14bc1fadbe47c`.
The preceding operator observation belongs only to the non-trace PRX until
the operator confirms whether the flash recurred in this diagnostic run.

The trace contains 1,082 batch records, without a limit marker. The chapter
font batch is texture 883, bounds (878,702)..(1037,721), 108 indices. All 297
samples use key 130, shader 8 and additive register `61010104`, from engine
time 1,383 to 6,321 ms. The last colors descend through 8,7,6,5,4,3,1,0 in
each RGB channel, with alpha 255. At 6,305 and 6,321 ms the trace contains
only this title batch; no white fill or mode switch is recorded there.
This narrows the investigation: no CPU-side title whitening is observed in
these samples. It does not prove the GPU produced correct pixels, nor that
the original flash was reproduced. Do not patch the fade arithmetic merely
because a white rectangle was reported. Diagnostic paired validation passed
with live lightmaps/2D/menu/brush/Studio requirements, exact resource teardown
and clean BYEs. Independent post-run status again found no BigApp and healthy
services. Visual reproduction of the flash in this run remains unconfirmed.

## Operator QA handoff (pending)

### White-scene screenshot and blend-state correction

The operator's subsequent screenshot shows most of the world becoming white
while the door remains textured and the chapter lettering remains visible.
This is broader than a text-background rectangle; the earlier HUD-only
interpretation was too narrow.

Source inspection found that the native base pipeline does not include
CB_BLEND0_CONTROL in its render-target/viewport/link/shader register plan.
Unlike the GoldSrc entity pipeline, `bind_native_pipeline` did not apply a
dynamic blend reset. The next frame's background clear likewise inherited
blend state. An additive title draw can therefore leave additive blending
active for subsequent base clear/world draws, while brush entities such as
the door explicitly bind their own opaque state. This is a demonstrated state
ownership omission and a plausible explanation of the screenshot, pending
hardware confirmation.

The correction binds the existing immutable GPU-visible opaque blend register
before the background clear and after every native base-pipeline bind. It
does not alter depth/cull or the HUD's requested blend modes. Host regression
tests verify the register source, preservation of prior HUD state tables and
both native reset call sites. The upcoming native validation build disables
the diagnostic trace.

Operator QA after the correction: "si todo ok ya esta resuelto". This accepts
the chapter title without a black background and its disappearance without
the white-scene flash. Corrected renderer ELF SHA-256:
`72b77e924283b3f9c5452ec63d63593a570fc90895e715e33d8e6073f3e6ab5a`;
PRX SHA-256:
`1037c7fc64a0a95d7cec55540b14835c56d28769856fe40ee3dcc5e6b126e6bf`.
The normal build passes `make all`; its ELF contains
`opaque_blend=explicit` and no HUD batch-trace marker.

The first corrected paired session (`20260909T133533679Z` engine /
`20260909T133533738Z` renderer) ended with connection EOF, not BYE, at
13:36:27 UTC. It cannot prove exact teardown. A subsequent session
(`20260909T133633232Z` engine / `20260909T133633292Z` renderer) was followed
by the operator reporting a manual close. Do not infer a crash from EOF alone,
and do not combine partial sessions into one accepted resource run.

At the operator's request, a fresh uninterrupted normal run completed:
`20260909T134011789Z_PPSA99996_xash3d-engine_0x14cbe2b57d1b9` and
`20260909T134011848Z_PPSA99996_ps5-xash3d_0x14cbe2ed915f6`.
It passed 10,992 frames, nine exact resource reclaims, intact guards,
renderer teardown result zero and paired clean BYEs. The paired validator
passes live lightmaps, 2D, menu, brush and Studio requirements. Independent
post-run status confirms no BigApp and all four required services healthy.
This accepts the corrected build's resource regression together with the
earlier operator visual confirmation. Additional font-mode/multiplicative-fade
coverage and PR/lab integration still remain for the full HUD task.

Do not interpret a timer, draw count or clean exit as visual acceptance.
Confirm operator availability before the launch; use the existing 5-second
menu / 180-second map-relative build. Do not require another movement test
to prove already accepted controller functionality.

- During MainUI: fullscreen layout, intact background/buttons and readable
  lettering; no newly opaque rectangles around previously transparent art.
- At c1a0 chapter title: lettering visible over the actual scene, without the
  reported solid black rectangle; note appearance, fade-in and fade-out.
- After title disappearance: no leftover panel, color tint or darkened scene;
  existing world/brush/NPC presentation remains intact.
- Record exactly what was observed with the paired run IDs and artifact
  hashes. A missed title is unobserved, not a pass; do not automatically
  relaunch while the operator is using the console.
- A normal chapter-title run does not prove every font setting or the
  engine-only multiplicative fade. Those need a separate controlled exercise
  if not emitted in the observed run. Host mode/shader tests cover translation,
  not the resulting hardware pixels.

The live 2D host suite also exercises all fill modes with alpha test enabled
and disabled, and rejects non-boolean flags without consuming transient
storage. The complete live compositor test passes AddressSanitizer and UBSan.

## Required closure

1. Correct GPU translation for alpha test and multiplicative fades, preserving
   existing 3D pipeline keys/behavior where practical. Reject unsupported state
   explicitly rather than silently treating it as alpha.
2. Regression tests for source order, captured state, mode transitions, fill
   side effects, normal/alpha/additive/masked fonts and fades. Verify any shader
   changes are rebuilt and present in the actual deployed PRX.
3. Incremental hardware evidence: MainUI intact; chapter title without a black
   rectangle; readable fonts and correct fades. Direct operator QA is preferred;
   do not infer acceptance from draw counters alone.
4. Preserve paired menu/map/brush/Studio/lightmap/resource validation, exact
   teardown and deployment hashes. Update the lab checkpoint and HTML plan and
   integrate through green PRs. Only then continue to Studio lighting/viewmodel
   improvements (lighting, chrome, controllers and animation transitions),
   preserving accepted smooth NPC walking. The owner-reordered five-task plan
   places real game audio fourth, after Studio, and `valve_hd` validation fifth.
