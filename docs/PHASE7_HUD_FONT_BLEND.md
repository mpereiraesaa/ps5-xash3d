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
No console deployment or launch has been made for this HUD build yet.

## Operator QA handoff (pending)

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
   integrate through green PRs. Only then continue to live game audio.
