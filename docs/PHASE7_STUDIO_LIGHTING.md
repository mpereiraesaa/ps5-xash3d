# Phase 7 Studio lighting and viewmodel — in progress

Baseline: hardware-accepted HUD PR #27, merged as `4726bd3`, with the normal
non-probe build installed. This task precedes live game audio and `valve_hd`.
Accepted NPC STEP interpolation, poses, mip filtering and HUD state ownership
must remain intact. Historical candidate entries below preserve their original
evidence boundary; the current status here supersedes their pending statements.

## Current integration checkpoint — 2026-09-09

- NPC lighting and NPOT texture-layout correction: operator accepted, with
  clean paired normal-build resource validation (15:42 runs below).
- Ordinary NPC chrome: operator accepted, with clean paired resource validation
  (15:55 runs below). Forced glowshell remains outside this acceptance.
- Callback symbol coverage: observed `c1a0 -> c1a0d -> c1a0` round trip works;
  the single-map automated validator still needs an explicit multi-map contract.
  Host_Error inactive-world recovery is host-tested, not hardware accepted.
- Viewmodel now has separate pose, lighting and draw ownership. Operator used
  pistol fire, crowbar attack and immediate weapon cycling in live QA. This
  does not close reload/events/muzzleflash/attachments, custom FOV/handedness,
  full controller/sequence-transition fidelity or final integrated resource QA.
- DualSense profile v5 is the current operator-accepted aim baseline: radial
  deadzone 10%, exponent 1.6, yaw/pitch 140/105 degrees per second; R2 primary,
  R1 secondary and immediate D-pad cycling. See [controller guide](SCEPAD_PHASE5.md)
  for deployment hashes, run IDs, controls and remaining button QA.
- Diagnostic weapon grants and Studio A/B remain opt-in, default off. Graphics
  evidence uses audio disabled. Live game audio and valve_hd are still pending.

Do not mark Phase 7 complete based on this incremental integration.

Integration cleanup: host suite/publication audit and native build pass.
At 2026-09-09 16:48 UTC, with the title externally confirmed stopped, the
normal no-grant/no-A-B 180-second build was restored by exact raw-FTP bundle
verification, without relaunch. ELF
`86b83ac6e6439c971440d23e0f6fa7b644836d2a2fe8c2f4a212cfcaed2afdd5`,
SELF `8c4b1fcd951c6583a3fd4bd3e38487ef5370f70190cab01c4edc6285e462fe38`.
The accepted DualSense v5 cfg is retained. Restoration is not a new hardware
resource run; historical clean lighting/chrome runs retain their identities.

## Connected implementation (2026-09-09; hardware validation in progress)

The engine-thread bridge now compiles the pinned, unchanged
`ref/common/ref_light.c`. The engine's `CL_RunLightStyles` renderer callback is
wired to that implementation. Lighting is finalized in `RefAgcEndFrame`, before
immutable publication: entities are submitted before `GL_RenderFrame`, so
sampling during pose capture would see zeroed view flags and choose fullbright.
Owner-thread staging preserves the entity needed for sampling; only ambient,
shade, color, direction, scale and a copied 1024-entry gamma table cross to the
worker. The sampler uses the same interpolated origin as the pose and does not
modify the engine entity's floor-color bookkeeping. Required callbacks/cvars
are checked; missing data fails explicitly rather than silently using white.

The shared path supplies BSP/brush-model sampling, sky, fullbright/inverted
light, dynamic lights and lightstyles. Renderer-local extended-light sampling
and virtual-radius settings currently use upstream defaults 1 and 3. No cvar
registration pointer is retained across PRX unload. This does not add new
world-lightmap uploads or Studio entity-local-light/chrome/viewmodel support.

Per-vertex lighting follows pinned `R_StudioLighting`: Lambert constant
1.4953241, ambient/shade limits, flat/fullbright texture flags, scale handling,
engine light-to-texture gamma, then RGB tint. The result is packed RGBA8 in the
32-byte Studio vertex's formerly unused face-id word at offset 28. The eight
GoldSrc surface variants read this attribute only when the Studio constant at
byte 124 is -1. World/brush constants leave it inactive; 96 existing 3D state
keys, HUD shaders, alpha/additive rules and pose matrices are retained.
Static assertions bind both offsets to the shader ABI. All variants are rebuilt
and their pre-raster ISA is verified inside the deployed ELF.

Host tests execute the actual shared BSP sampler on a planar synthetic world,
then add a red dynamic light and check fullbright, gamma, linear-gamma override
and borrowed-entity preservation. ASan/UBSan passes that bridge. Additional tests
cover per-normal math, separate bone arrays, vertex packing, missing lighting,
allocation rollback and schema-2 evidence rejection. `make -B all`, shader
validation and the full native build pass. GCC's false-positive tbn initialization
warning is suppressed only around the unchanged upstream include; tbn is filled
under the same condition that enables its later deluxemap use.

Candidate renderer ELF:
`da7b663f18cdde507cb4c760d50387158cd69cafe8bc27a9af37a85352ecda74`;
renderer PRX:
`b5874067db7ade3efaa903649088cdf8c0bc4856397588550d527448d81288ed`.
Engine ELF/SELF remain the accepted non-probe identities. Transactional raw FTP
hash verification preceded the authorized FW 12.02 launch. Schema-2 Studio
telemetry records the applied lighting mode, normal count, color hash/range;
these counters alone are not visual acceptance.

### First hardware run: visual lighting accepted, lifecycle NOT accepted

Engine run: `20260909T143423620Z_PPSA99996_xash3d-engine_0x14fb348e6dddd`.
Renderer run: `20260909T143423679Z_PPSA99996_ps5-xash3d_0x14fb34c56fc93`.
The operator confirmed lighting looked correct with nothing unusual, crossed
into the next section successfully, then returned toward `c1a0` and saw a
frozen loading screen. This is visual acceptance of the observed NPC lighting,
not acceptance of transitions, viewmodels or clean resource teardown.

The engine log reports restoration of `save/c1a0.HL1`, empty landmark lookup
errors and `Host_Error: Level transition ERROR / Can't find connection to c1a0d
from c1a0`. Earlier save processing repeatedly reports `Invalid function pointer
in entity!`; that message originates in `CSave::WriteFunction` when
`NAME_FOR_FUNCTION` cannot resolve a callback. Whether it causes the missing
transition connection is not yet demonstrated. Preserve the saves for diagnosis.

After server error cleanup, the renderer reaches
`PARKED retain_all_resources=true reason=live-world-stats-failure` at serial
5639. Its revision-change path requires an active, nonempty world, including
when the world has been removed. Investigate this error-recovery path separately
from save/restore: the park is not proof of a lighting fault. The engine later
executes the configured 180-second quit; its renderer state has `pass=0`.
This run must not satisfy the clean hardware gate despite individual module
completion markers. No automatic relaunch follows the failure.

### Recovery follow-up (host/native build only, not deployed)

The renderer now accepts an inactive revision only with zero draws, vertices,
indices and resident bytes, emits `REF_AGC_LIVE_WORLD_CLEAR`, and retains the
existing cache validation and prior-GPU-use retirement checks. Active worlds
still require nonempty geometry. Host coverage exercises retirement refusal,
clear, repeated clear and loading a replacement after clear. Native contract
tests retain the distinct active/inactive checks. `make all` and the native
client/AGC build pass; PS5 recovery acceptance is still pending.

Save/restore inspection found a concrete field-count hazard: `CSave::WriteFields`
counts every nonempty callback, but `WriteFunction` writes nothing if reverse
resolution fails. `CRestore::ReadFields` consumes the advertised count regardless.
The current generated server descriptor includes entry points and entity factory
symbols, not the compiled C++ member callbacks used for think/use/touch. The
dynamic-library reverse resolver returns only descriptor matches. This explains
why those callbacks cannot be saved by this path and provides a mechanism for
stream desynchronization; the exact failed landmark restoration still needs a
reproducer. Do not mask the error by dropping callbacks or using unvalidated
absolute addresses. Next: tested bidirectional callback symbol coverage and a
fresh round-trip save/restore test, preserving the original failed saves.

### Callback coverage candidate (2026-09-09; not deployed)

The build now adds all 3,020 compiled defined Itanium C++ code symbols to the
server descriptor (3,271 engine exports total, plus six support exports).
The input is restricted to `nm` defined T/W code, not data or firmware imports;
it includes arbitrary method names and thunks rather than guessing callback
names. Names must satisfy the descriptor's identifier/256-byte constraints and
the complete table must fit its 4,096-entry limit. Save/restore uses the existing
bidirectional PRX resolver; no absolute-address fallback or dropped fields.

Generator tests compile the actual generated descriptor and production PRX
resolver, verify address-to-name-to-address, reject unknown names and unloaded
handles, and resolve the persisted name against a replacement module with a
different callback address. This is a symbol-resolution round trip, NOT a full
HLSDK save-file round trip. `make all` and the native build pass. Hardware still
must verify fresh `c1a0 -> c1a0d -> c1a0`, no callback/landmark errors, lighting,
and clean teardown; previously malformed saves must not be silently deleted.

Server ELF: `37a049170fba94f39179705b6db5671454aa76301033e469fd28f18507618da0`.
Server PRX: `15afbf9cb4078b9ef999b5e0cba51902b83c17efac98f4315f4d64406b32b1a3`.
Renderer ELF: `c12620537ce1d13653a2145fe8fbff80cf7febdfc8bf419428a5d30e00564e58`.
Renderer PRX: `0325699d132c72684df438c00c9ea27a362a58ff5758adbb37687573d40f27ca`.

### Callback candidate hardware outcome and new visual blocker

Engine run `20260909T145113528Z_PPSA99996_xash3d-engine_0x1509e6b42d06c`;
renderer run `20260909T145113618Z_PPSA99996_ps5-xash3d_0x1509e70b768c3`.
Raw-FTP verified deployment preceded launch. The inactive application's
336,789,504-byte download container was copied to private lab research storage
before launch; no saves were manually deleted. Operator confirmed return to
`c1a0`. Logs show `c1a0 -> c1a0d -> c1a0`, restoration and no repeated invalid
callback, landmark or connection errors. Renderer completed 10,810 frames,
reclaimed nine resources, guards intact, errors zero, exact teardown and clean
BYE; engine also completed cleanly. This validates the observed round trip,
not arbitrary saves across different builds or the Host_Error recovery path.

The existing paired validator rejects this multi-map run because its menu gate
requires a unique boot-map spawn (and matches `c1a0d` by substring). Therefore
do not report automated paired validation as passed; an explicit transition-aware
contract/test is pending, without weakening the single-map gate.

Operator subsequently reported black areas on Barney's front jacket near the
computer and on some scientist faces, dependent on viewing angle/proximity and
disappearing after rotation or moving away. Studio visual acceptance remains
OPEN. Local classic companion model inspection shows the front-jacket and face
materials have flags 0; the separate chrome materials have flags 0x3. Missing
chrome alone is therefore not an established explanation. Next QA must isolate
surface lighting versus texture sampling/depth while preserving the same pose
and viewpoint; no claim yet that the texture resource itself disappears.

### Rejected live Studio diagnostic protocol (removed from code)

Opt-in build: `XASH_STUDIO_PROBE=1`, mutually exclusive with the old wall
sampling probe. Default production build is zero. Touchpad down advances one
mode; release is consumed without another advance. No timed mode changes.
The current mode is shown briefly through the existing center-text path and
logged as `XASH_STUDIO_QA_REQUEST` and applied `REF_AGC_STUDIO_QA`, with numeric
lighting snapshots. The diagnostic run lasts 900 seconds from map load.

0. Normal: reproduce the black jacket/face at a specific view.
1. Texture without lighting: same texture, pose, alpha, geometry and depth;
   vertex light is white. Does the black region disappear immediately?
2. Lighting only: preserve sampled alpha/masking, replace texture RGB with white;
   retain calculated vertex light. Does the dark region match the original?
3. World normals RGB: normalized direction mapped to RGB, white texture RGB;
   look for abrupt/incorrect patches. Colors are diagnostic, not lighting.
4. Constant light: texture multiplied by 128/255, bypassing both normals and
   the sampled ambient/shade/gamma. Compare with mode 1 from the same view.
5. Next press returns to 0: confirm reproducibility. Then move/rotate slightly
   and repeat on the same character, followed by an affected scientist.

World/brush/HUD shaders retain their normal paths; no chrome/viewmodel change,
depth disable, resource replacement or camera teleport is part of this probe.
Host geometry tests verify all five modes preserve the pose hash and expected
packed colors/constant ABI. Modes 2/3 retain alpha-tested discard before replacing
RGB. Diagnostic runs cannot close production lighting acceptance. Check the
baseline first: if the black patch is absent in mode 0, do not infer a cause.

Diagnostic launched after `make -B all`, shader rebuild and native build passed:
engine run `20260909T151053209Z_PPSA99996_xash3d-engine_0x151b114d3b560`;
renderer run `20260909T151053304Z_PPSA99996_ps5-xash3d_0x151b11a796084`.
ELF diagnostic markers were checked before raw-FTP verified deployment.
Engine ELF `4fde1adeda7d377013f9ae0971800bb2cd946ee633761148c51031689d7c9d25`;
SELF `978022b2e1253f54dfde58e80f0671ce8b28491064a211ea44cf493f986268b1`;
renderer ELF `9c39e455f312d57f34d1e950376c1cb9d7098904604391d72353f8aa6f267683`;
renderer PRX `4586a4be3f1cbbf72208c6e698a554bedd08a0a453c88a04dabeabe4916a9e3e`.
Operator reported the black regions persisted in all modes, but also new
texture flicker on the door and NPC bodies immediately on entering the map.
The diagnostic changed the shared surface shader interface; the run is therefore
confounded and cannot exonerate the lighting path. Exact cause of this new
flicker is not established. Do not reuse this diagnostic or treat it as a fix.
The QA selector, frame field, vertex overrides and added shader varying were
removed, preserving the earlier lighting/callback/recovery changes. Targeted
application close was externally verified; this is not a clean renderer gate.
Rebuild/restoration must match the pre-QA candidate hashes above. Any later
isolation should avoid changes to the shared shader interface.

Restoration completed at 2026-09-09 15:18 UTC. Rebuilt engine ELF/SELF and
renderer ELF/PRX match the pre-QA callback candidate hashes exactly; the
transactional deployment verified all nine files by raw FTP SHA-256.
The faulty diagnostic is no longer installed. No new launch was performed.
Host tests/native build pass; a concurrent build briefly recreated Python
bytecode during the publication audit, corrected by disabling bytecode in the
build's generator import and rerunning the suite without that race. The
original angle-dependent black surfaces remain unresolved.

### Replacement CPU-only A/B diagnostic

`XASH_STUDIO_AB=1` (default zero, mutually exclusive with wall QA) toggles
normal lighting A versus packed white vertex RGB B. Uses reserved value 4 in
the existing sampling-probe word; world/brush diagnostics accept only 1..3.
No frame/vertex ABI, shader source, shader varying or draw-constant changes.
Tests compare the vertex bytes and constants against baseline, allowing only
the packed color to differ, and verify unchanged pose hash. Actual consumed
mode is logged by `REF_AGC_STUDIO_AB`; input by `XASH_STUDIO_AB_REQUEST`.
Touchpad alternates A/B with brief center labels, no automatic toggling, 900s
run. First reject the run if A introduces new flicker; otherwise compare the
same black region at the same viewpoint in A/B/A.

Host suite and native build pass. Baseline surface GS SHA-256 remains
`cd0c0ef2660213899b2ea701988c1f7f8115ff03e8ac7791c54dd7a57aa5b6ef`;
all 20 generated GoldSrc GS/PS blobs were found in the new renderer ELF.
Candidate engine ELF `38e24f6edd1adb2fc5083c45c182ab9b67e161dc55dcd45bf65d7577fe28c118`,
SELF `2926782517860e5a65d9df8ac1c9e06c69a20a7f3ffc43468bbc70f7bab7041e`,
renderer ELF `913449d9cacb6a3760f37b2d0ea9812df3edecf82acdf1dda8d940cfd5270b99`,
renderer PRX `de6ef134a4bf83d2f93e8eded3564f80d8740c73f7780140a32ee86662417453`.
Pre-QA base SELF/renderer PRX were downloaded raw into private lab artifact
storage and matched their documented hashes before candidate deployment.
Raw-FTP verified deployment completed 2026-09-09 15:22 UTC. Results pending.

Operator confirmed the added QA flicker is gone in the CPU-only A/B run.
Lighting visibly changes between A/B, but the localized black torso remains
and depends on Barney's own rotation even with stationary camera. Screenshot
shows black front torso while face/sleeves remain visible. This narrows the
failure beyond vertex-light modulation; final hardware cause remains pending.

### Material/NPOT layout inspection

Classic Barney bodypart 0 meshes 0/1/6 contain 30/67/48 triangles and map to
`BX_Front_Mid1.bmp` (68x97), `BX_Front_Bot1.bmp` (52x75), and
`BX_Front_Top1.bmp` (80x94), all flags zero. Native live Studio submits with
`GOLDSRC_CULL_NONE`. The source mid-front palette image has no fully black RGB
pixels; its summed RGB range is 2..451. No material replacement is justified.

Concrete mismatch found against local Mesa AddrLib GFX10 source:
`gfx10addrlib.h:GetMipSize` uses `ShiftCeil`, while
`ref_agc_gpu_texture_cache.c` uses truncating width/height right shifts for
linear mip storage. `HwlComputeSurfaceInfoLinear` packs levels smallest-first
with 256-byte pitch alignment using those ceil dimensions. With the same
seven mip levels, CPU versus AddrLib mip0 offset/total bytes are:

| Dimensions | Current CPU | AddrLib contract |
|---|---|---|
| 68x97 | 24064 / 73728 | 25600 / 75264 |
| 80x94 | 22784 / 70912 | 24064 / 72192 |
| 92x109 | 26624 / 82432 | 28160 / 83968 |
| 60x92 | 22528 / 46080 | 23552 / 47104 |
| 64x64 (control) | 16128 / 32512 | 16128 / 32512 |

This establishes an NPOT storage-layout defect, not yet hardware proof of the
reported black surfaces. Fix must separate logical mip dimensions from padded
storage extents/pitches, initialize padding appropriately, test NPOT and POT
controls, preserve sampler/shader behavior and validate the same NPC views.

NPOT candidate implements separate ceil storage extents/pitches and floor
logical mip dimensions. Logical downsampling is unchanged; storage-only fringe
is edge-extended after generation, never fed back into logical downsampling.
Host fixtures cover the four NPOT examples above plus 64x64 control, all-level
pixels including fringe, exact allocation/flush bytes and end guard; existing
retirement/exhaustion tests remain. Host suite, ASan/UBSan and native build pass.
No shader, sampler or lighting change relative to the CPU-only A/B candidate.
Engine identities remain unchanged. Renderer ELF
`43ed5d592dd433ac91277d9c566d49459c0414321ba6ba0b1f5edc82765b1e6c`;
renderer PRX `4266d59c1438d8bd78918d3b5b5d5b702355fc20a5da82176746776a9b9c07f6`.
The prior A/B run was externally closed before deployment. Hardware visual
acceptance of this fix is pending; keep A/B enabled only for this diagnostic.

Operator accepted the NPOT candidate: "si ahora estan perfectos". This confirms
the observed black NPC surfaces are resolved. Diagnostic run identifiers:
engine `20260909T153540666Z_PPSA99996_xash3d-engine_0x1530b66c5f35d`, renderer
`20260909T153540762Z_PPSA99996_ps5-xash3d_0x1530b6c91ed8f`.
That run stopped without internal completion/BYE markers; external status
confirmed no application, but it does not establish clean resource teardown.

Normal candidate built with `XASH_STUDIO_AB=0`, 180-second automatic exit.
Engine is again ELF `878232f31299066486c1e3b4d8678c3f20d54a286bad2f7acc1e2a65f9724de2`,
SELF `48395ac510aa1fb1acf2216962005c81a89a7aa50e774e75551429e843809854`.
Renderer ELF `891d5914406f3aacaf521e3e5177753292092c624250f6da3e0655b35da16942`,
PRX `45bdb0488feb5396189a02a424ea70363988b3298f129d4b50045888c466e057`.
A/B marker absence verified in both ELF binaries. Raw-FTP verified normal
deployment completed before the authorized automatic resource-validation run.

Normal run accepted by the paired validator with required live lightmaps, 2D,
menu, brush and Studio gates: engine
`20260909T154242843Z_PPSA99996_xash3d-engine_0x1536db225ae78`, renderer
`20260909T154242941Z_PPSA99996_ps5-xash3d_0x1536db7e13bf0`.
10,990 frames, ownership exact, pass true; Studio uses `engine-bsp-dynamic`,
10,771 Studio frames and 10,770 pose changes. Texture/world errors zero, 2D
unresolved zero. This closes normal-build resource validation for the lighting
and NPOT fix, together with the operator's visual acceptance. Normal candidate
remains installed, A/B disabled. Chrome and viewmodel remain pending; this is
not whole-task/Phase-7 completion or PR integration.

### Chrome candidate (host verified; hardware pending)

Live materials with `STUDIO_NF_CHROME` now generate UVs on CPU from the normal's
own bone and current camera, matching pinned GL `R_StudioSetupChrome`:
normalized viewer-to-bone vector, cross with camera right, normalized up/right,
dot with transformed normal, then `(dot+1)*32/texture_dimension`. Engine-world
coordinates and the existing camera angles include pitch/yaw/roll. Dotting
world-normal with world axes is algebraically equivalent to the upstream
inverse-rotation of those axes before dotting the model-space normal. Normal
magnitude is retained. Coincident/parallel vectors follow upstream's zero-vector
normalization behavior, yielding finite center coordinates. Nonfinite inputs
reject the frame with transient allocation rollback.

Non-chrome UVs, textures, sampler, shaders, lighting, positions and indices are
unchanged. No new published-frame or shader interface. `REF_AGC_STUDIO_CHROME`
records bounded draw/vertex counts and UV hash for camera/animation QA. Synthetic
host tests exercise known UVs, changing camera with identical pose hash,
coincident viewer/bone, nonfinite rejection and non-chrome counts. Geometry
ASan/UBSan and native build pass. Ordinary NPC chrome only: forced glowshell and
viewmodel remain separate pending work.

Candidate renderer ELF `28c39f2ec5eae3fa4b4624ee124293369319d395d9638020c85c7509aae03714`,
PRX `fa5bb05f7aae9d7671cc9a3a6b5eb06815435d9589c9b9da4072328e7cc959fe`.
Marker verified inside ELF. No deployment or hardware acceptance yet.

Chrome candidate subsequently deployed with raw-FTP verification and launched
with operator present at 2026-09-09 15:55 UTC. Engine run
`20260909T155503025Z_PPSA99996_xash3d-engine_0x1541a07ca2e9c`, renderer run
`20260909T155503120Z_PPSA99996_ps5-xash3d_0x1541a0d970e66`.
Operator confirmed "todo perfecto". Automatic run completed 10,990 frames,
565 camera changes, nine resources reclaimed, guards intact and zero renderer
errors. VideoOut closed, direct memory released and AGC unloaded with exact
ownership; clean renderer BYE. Paired validator passes with live lightmaps,
2D, menu, brush and Studio required. Chrome records show active draws and
changing UV hashes with unchanged shaders. Ordinary NPC chrome is accepted
for this observed hardware path. Viewmodel/glowshell and PR integration remain
pending; no further run was launched.

### Viewmodel pose/draw candidate (in progress)

Previously the viewmodel was copied without a pose and never drawn. It now
captures its own owner-thread pose and lighting, with visibility guards for
local first-person living player, world view, non-cubemap and drawviewmodel.
Pitch compensation follows the host feature flag for the viewmodel only.
The worker appends it after ordinary Studio draws and before HUD, retaining
the existing camera projection and using clip-Z scaling 0.3 to implement the
upstream reduced depth range without leaking depth state. `UINT32_MAX` is the
explicit draw-owner sentinel for the separate viewmodel; it is never used to
index the ordinary entity array. No shader interface changes.

Host tests cover world/viewmodel order, separate owner, per-column depth
transform, invalid pose rejection/rollback and existing chrome/NPOT coverage.
Host suite, geometry ASan/UBSan and native build pass. This is not complete
weapon-event/glowshell/handedness/FOV-override coverage: first validate actual
idle/draw/attack poses and camera/depth behavior before further effects.

Opt-in `XASH_VIEWMODEL_QA=1` queues sv_cheats and give commands for crowbar,
handgun and ammo in the local diagnostic map; default is zero. No save command
or asset edit is issued. Diagnostic timeout 300 seconds. `REF_AGC_VIEWMODEL`
records actual draws, vertices, sequence and evaluated frame.
QA engine ELF `50c5f76b62452b4559eb51b230e08279731178105608c753ac1677f852f9374d`,
SELF `712a68e1d695bf64fd4a7a91ccf2783aee57f28bd96a0e60ca2af421891cda83`;
renderer ELF `f4677de560103b149313fdd447723eba8393e9ea86386fbe8944a3af92817233`,
PRX `420db881bee08018a270b0e350657b8e60d13148102e766976b7bac47a47aa6c`.
Raw-FTP verified deployment and authorized launch 2026-09-09 16:04 UTC;
hardware results pending. Restore a no-grant build after QA.

## Original source findings

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

1. Complete viewmodel events/effects and explicitly scoped projection/depth,
   reload and remaining button QA; retain accepted NPC lighting/chrome/NPOT.
2. Add a transition-aware evidence contract without weakening single-map gates;
   separately validate the inactive-world Host_Error recovery path on hardware.
3. Cover remaining Studio controllers/sequence transitions and forced effects.
4. Obtain integrated final resource evidence for the completed viewmodel scope,
   then live game audio, optional valve_hd and remaining Phase 7 release gates.
5. Keep the HTML/checkpoint and controller guide aligned with green PRs. Preserve
   title IDs and never treat diagnostic or externally killed runs as clean gates.

The normal-only prerequisite was not separately deployed. The connected
candidate above is the first console exercise of this work.
