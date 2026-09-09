# Phase 7 Studio lighting and viewmodel — in progress

Baseline: hardware-accepted HUD PR #27, merged as `4726bd3`, with the normal
non-probe build installed. This task precedes live game audio and `valve_hd`.
Accepted NPC STEP interpolation, poses, mip filtering and HUD state ownership
must remain intact. This document is not hardware acceptance.

## Source findings

The live Studio compositor currently emits the base texture multiplied by a
uniform white draw color. Its telemetry correctly says `lighting=unlit`.
Although triangle commands carry normal indices, the original live path only
checked their numeric range: it did not read the normal vector or its bone
index. Vertex bones and normal bones are separate arrays in Studio v10.

Upstream `ref/gl/gl_studio.c:R_StudioLighting` handles fullbright, flat shading,
ambient plus directional shade and the light-to-texture gamma conversion.
`ref/common/ref_light.c:R_EntityDynamicLight` supplies world-dependent lighting,
including floor/ceiling sampling, sky and dynamic contributions. Reusing that
logic requires its engine/global dependencies to be wired deliberately; it is
not a self-contained callback already provided by this port. A constant gray
multiplier or an arbitrary camera light would not close this gate.

## Implemented prerequisite: checked normal input

The live decoder now validates both normal-array spans, independently checks
normal bone ownership and transforms directions using the matching bone's
3x3 component, never its translation. Values must be finite. The output remains
in engine world coordinates, with its original magnitude; these are rigid-bone
directions, not a general nonuniform-scale inverse-transpose implementation.
The current vertex buffer/shader and pose interpolation are unchanged.

Each built frame counts normal samples and hashes their transformed values.
`REF_AGC_STUDIO_NORMALS` logs bounded samples alongside existing Studio frame
records, explicitly declaring `lighting=not-applied`. On rejection the transient
allocation checkpoint is restored and draw/normal output is invalidated.

Host regression tests cover unchanged normals under translation, rotation,
different valid vertex/normal bones, out-of-range normal bones, NaN input,
truncated normal arrays, negative bone-array offsets and exact allocation
rollback. The test also retains existing strip/fan and position checks.
The full `make all` suite and standalone ASan/UBSan Studio test pass.
The native client/AGC build also passes; the normal-input marker is verified
inside renderer ELF `ce58de8382f097cc910b63d452fe57ec0b9cd9db05e74acfde1ca6fc5ee0e742`.
Renderer PRX: `1d9a64ac6bdaea37dd8cef2778fbe1a095248ae0e8d6390666a4a91dc9d3c900`.
These are build evidence only, not deployed or hardware-accepted artifacts.

## Remaining implementation and acceptance

1. Capture map/entity lighting on the engine owner thread, respecting lightstyles,
   fullbright/inverted light and the upstream ambient/directional/gamma rules.
   Copy owned numeric data across the PRX handoff; no borrowed engine pointers.
2. Feed per-normal/per-vertex lighting into an explicit native Studio shader
   contract. Preserve alpha-tested/additive materials, exact vertex layout and
   rebuild/verify actual shader bytes. Preserve world and HUD pipeline behavior.
3. Add chrome and complete controllers/transitions; verify the viewmodel has
   its own actual pose/draw path, projection/depth behavior and weapon animation.
   Its presence in the live frame alone is not proof that it is rendered.
4. Host numeric/ABI/ownership tests first, then incremental operator QA on
   lit/shadowed NPCs and the viewmodel, with structured coverage counters,
   artifact hashes and exact teardown. Do not ask for another movement-only QA.
5. Update the HTML/checkpoint and integrate via green PRs. Leave audio disabled
   for this task and preserve title IDs; do not claim whole Phase 7 is closed.

No console deployment or launch has been performed for this prerequisite.
