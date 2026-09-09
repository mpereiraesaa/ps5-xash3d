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
numeric contract to the actual pinned engine definitions. This is not yet
ready for deployment: alpha-test and modulate consumers still need work.

Tests cover ordered normal/alpha/additive/reset/masked/modulate sequences and
invalid-mode fallback. The existing full host suite passes, but this does not
prove missing GPU modes or visual correctness. No console launch or Remote
Play was performed for this partial change.

The render-state layer now accepts masked 2D and a screen-only modulate
extension. Existing 96 3D permutations and numeric keys remain unchanged;
the five screen keys are 128, 129, 130, 131 and 384. Invalid/negative blends
and modulate in 3D are rejected. Register tests cover depth/cull disabled
for masked and modulate screen draws. Modulate encodes ZERO/SRC_COLOR for
RGB and ZERO/SRC_ALPHA for alpha (`0x64000200`); SRC_COLOR=2 was checked
against AMD PAL's `gfx9_plus_merged_enum.h`.

This remains preparation: the pipeline cache still contains its baseline
99 entries. Neither new screen state is registered there yet, and shader
selection explicitly rejects masked screen state until its discard shader
exists. Do not deploy this checkpoint or claim GPU/visual acceptance from
the register tests. Next: generate and compile the dedicated screen masked
shader, register both new screen pipelines, then update live translation,
batch accounting and fill side effects before the paired hardware run.

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
