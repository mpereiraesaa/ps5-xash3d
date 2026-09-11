# Phase 7 Studio lighting and viewmodel — historical gate record

> Phase 7 is now closed and the repository status is **PLAYABLE**. This file
> preserves the individual hardware gate records and their original scope;
> consult [`PLAYABLE_RELEASE.md`](PLAYABLE_RELEASE.md) for the current public
> boundary. Statements below that say a candidate was “open” or “not accepted”
> describe that historical run, not the released runtime.

Baseline: hardware-accepted HUD PR #27, merged as `4726bd3`, with the normal
non-probe build installed. Live game audio and the optional `valve_hd` mount
now have first hardware acceptance; this document retains their precise
evidence boundaries below.
Accepted NPC STEP interpolation, poses, mip filtering and HUD state ownership
must remain intact. Historical candidate entries below preserve their original
evidence boundary; the current status here supersedes their pending statements.

## Current integration checkpoint — 2026-09-09

The controller-interpolation path follows the merged recovery checkpoint;
host/native checks and the 10,989-frame paired hardware regression pass, with
operator acceptance. The released runtime includes this path. The coverage
ledger below retains the exact historical forced cases.

Latest effects checkpoint: operator accepted reload/crowbar, muzzleflash,
wall marks, blood and the enhanced-blood/sprite-lighting candidate. The final
18:17 paired run passes 18,175 frames, nine reclaims, intact guards, zero
renderer errors and exact teardown. Full acceptance and normal restoration
identities are recorded in the final section below; earlier pending entries
are historical. Phase 7 is closed; future work is compatibility maintenance.

- NPC lighting and NPOT texture-layout correction: operator accepted, with
  clean paired normal-build resource validation (15:42 runs below).
- Ordinary NPC chrome: operator accepted, with clean paired resource validation
  (15:55 runs below). Forced glowshell remains outside this acceptance.
- Callback symbol coverage: the observed `c1a0 -> c1a0d -> c1a0` round trip
  now passes the explicit multi-map paired validator (10,810 frames).
  Named Host_Error inactive-world recovery is hardware/operator accepted
  with a clean 10,825-frame paired diagnostic; see recovery outcome below.
- Viewmodel now has separate pose, lighting and draw ownership. Operator used
  pistol fire, crowbar attack and immediate weapon cycling in live QA. This
  includes the tested reload/events/muzzleflash path and clean resource QA.
  Custom FOV/handedness, full controller/sequence-transition fidelity and
  broader forced-effects coverage remain historical compatibility notes, not
  release blockers.
- DualSense profile v5 is the current operator-accepted aim baseline: radial
  deadzone 10%, exponent 1.6, yaw/pitch 140/105 degrees per second; R2 primary,
  R1 secondary and immediate D-pad cycling. See [controller guide](SCEPAD_PHASE5.md)
  for deployment hashes, run IDs, controls and remaining button QA.
- Diagnostic weapon grants and Studio A/B remain opt-in, default off. Graphics
  evidence uses audio disabled; live-game audio and the optional `valve_hd`
  mount have separate first-run acceptance. Startup underruns and four-blend
  console coverage are retained as historical coverage notes for contributors.

## Latest cross-feature hardware checkpoint — 2026-09-09

The combined Studio QA run accepted controller interpolation, crossfade,
two-blend routing and glowshell visually. Mode 4 found no visible four-blend
sequence, so that path is not hardware-accepted. The subsequent live-game
audio run was audible to the operator and closed with exact ownership; the HD
run mounted `valve_hd` and was visually accepted. Their complete run IDs,
hashes, underrun note and rollback boundaries are documented in
`SCEAUDIOOUT_PHASE5.md` and the lower combined-candidate record. The top UI
diagnostic text is still a presentation cleanup task; external `ps5log/1`
telemetry remains enabled.

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

The then-current paired validator rejected this multi-map run because its menu gate
requires a unique boot-map spawn (and matches `c1a0d` by substring). Therefore
do not report automated paired validation as passed; an explicit transition-aware
contract/test was pending, without weakening the single-map gate. The follow-up
below now supplies that explicit mode; original transcripts are unchanged.

### Transition-aware validator follow-up (2026-09-09, local candidate)

The paired validator now accepts `--map-sequence c1a0 c1a0d c1a0` together
with `--require-live-menu`. Without that opt-in it requires exactly one map
spawn and compares complete map tokens, not prefixes. The sequence mode
requires alternating engine spawn/capture events for every expected map,
one corresponding retired-before-reuse GPU world publication per capture,
strictly increasing frame serials and revisions, matching geometry counts,
valid hashes and final-world agreement. Unexpected clears are rejected in
this successful-transition contract; error recovery is a separate gate.
Brush samples use the world revision's own surface bounds and do not infer
movement by comparing entity indices reused across different maps.

Revalidation of the unchanged `20260909T145113528Z` engine and
`20260909T145113618Z` renderer runs above passes the full paired
lightmap/2D/menu/brush/Studio checks: 10,810 frames, publications at serials
213, 2570 and 2982, nine exact reclaims, intact guards and zero errors.
This is retrospective validation of that build, not a new console run of
the latest effects build or a claim of arbitrary save compatibility.

Next hardware gate: deliberately invoke `Host_Error` through an owner-thread
diagnostic command once after a stable active map, confirm inactive-world clear and
continued 2D presentation, then load `c1a0` again and obtain exact teardown.
Use a dedicated bounded diagnostic and an exact expected-error contract:
never suppress arbitrary Host_Error/Sys_Error lines in the normal validator.
No corrupt saves, process kill, console restart or Remote Play is required.
Operator acceptance must confirm that loading/console presentation does not
freeze and the recovered map is visible and controllable. This recovery
gate remained pending at the transition-validator checkpoint.

The recovery diagnostic is opt-in with `XASH_RECOVERY_GATE=1`, requires the
MainUI/ref_agc stack and a map-relative timeout of at least 90 seconds, and
cannot be combined with the other visual probes. It waits 15 active seconds,
queues a one-shot restricted `ps5_recovery_error` command, observes inactive
client state for 10 seconds, queues the boot map, and records active recovery.
The callback unregisters itself before calling `Host_Error`; the clock hook
never calls the longjmp path directly. Normal builds default to gate off.

The first diagnostic attempt (`20260909T185048895Z` engine run) queued the
upstream `host_error` command, but it was unavailable at developer level 1;
the log reports `Unknown command: host_error`. That run does not prove error
recovery. The corrected diagnostic registers its own callback instead of
raising developer verbosity or enabling the engine's other fault commands.
The bounded state machine and callback scheduling have host tests.

`--require-host-error-recovery` is a separate paired-validator mode. It
requires exactly the named `Host_Error: PS5_RECOVERY_EXPECTED` between the
injection and inactive markers, all five ordered engine stages, two same-map
publications surrounding one retired zero-resident world clear, continued 2D
draws while cleared, and normal paired accounting/teardown. Every other
fatal error is still rejected. A passing diagnostic reports one expected
Host_Error; it must never be described as a run with no engine errors.

### Recovery hardware outcome — accepted 2026-09-09

Engine run `20260909T185435450Z_PPSA99996_xash3d-engine_0x15de625771803`;
renderer `20260909T185435571Z_PPSA99996_ps5-xash3d_0x15de62b04835c`.
Operator confirmed the error/console/reloaded-map sequence and working
movement/look: “sii todo ok”. The paired recovery validator passes 10,825
frames (9,998 world-view frames), with one explicitly expected Host_Error,
zero renderer errors, nine exact resource reclaims, intact guards, clean
BYEs, all module unloads and exact VideoOut/direct-memory/AGC teardown.
World publications are serials 211/revision 1 and 1700/revision 3; the clear
is serial 1099/revision 2 with zero resident bytes, followed by 2D draws.
Texture peak is 68,034,048 bytes. The start skew is 121 ms.

Diagnostic ELF SHA-256:
`563f679181b6f7a20cd3d0dcf7ac62a9189fda7b7f314b8be7e924400872f8a3`.
Diagnostic SELF:
`2690f0ceca419a7025b6a6326144b652d277d00e0bc20f3f885a9d279f9def3c`.
Renderer PRX remains the accepted effects binary:
`d7003f1c56e84cc91bcc78d7f7e7e300b8e612b42881d2fbed7ddc23c3c89386`.
Exact raw-FTP verification preceded launch. No manual process close, corrupted
save, firmware restart or Remote Play was used. This proves this controlled
inactive-world recovery path, not arbitrary failures or a transition soak.

Normal `XASH_RECOVERY_GATE=0` rebuild reproduces ELF `0c0b79677bd6dabba954305fd4f08143a6bb5d80615e9052d5f45d3a4d3c0e2e`
and SELF `6445127bd600a19b4af405ef9eeda12de6c95a7ce1a5ad479ac184baa774f16b`;
diagnostic marker absence was checked in the ELF. The timed/audio-off harness
and no-grant controls remain unchanged. Exact FTP restoration completed at
18:58 UTC without relaunch. Phase 7 is not closed by this gate.

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

### Coverage ledger after recovery — controller interpolation candidate

The live adapter previously used only `curstate.controller` and
`curstate.blending`, unlike pinned `ref/gl/gl_studio.c`. The candidate now
uses the reference animation-time interpolant (default 1, extrapolation cap
2), latched previous controllers, shortest circular controller interpolation
across the byte wrap, and latched blending values for both axes of 2/4-way
blend sequences. The existing upstream bone slerp clamps its blend weight.
Mouth mapping and the accepted STEP movement interpolation are unchanged.
Nonfinite scalar input/output is rejected before immutable pose publication.

Host tests cover time thresholds, disabled interpolation, extrapolation,
linear bounds, both circular-wrap directions, the exact 128-byte boundary,
blending and nonfinite rejection. ASan/UBSan, full host suite and native build
pass. Source-contract checks verify the actual adapter consumes latched values.
Bounded `REF_AGC_STUDIO_CONTROLLERS` telemetry reports sequence, controller and
blend counts, current/previous bytes and interpolation factors on the engine
thread. Sampling is not exhaustive coverage of all controllers or sequences.

Candidate renderer PRX SHA-256:
`a293b132fffec15182e65627062c65f7efdf79f868405fe9287025442ab59efe`.
Normal engine SELF remains
`6445127bd600a19b4af405ef9eeda12de6c95a7ce1a5ad479ac184baa774f16b`.
Candidate has no weapon grant, recovery injection, lighting change or input
profile change. It is not yet deployed; no new hardware result is claimed.

| Remaining case | Current boundary / required proof |
| --- | --- |
| Controller and 2/4-way blend interpolation | Natural NPC regression accepted below, including sampled controller changes. Forced wrap and 2/4-way cases still need explicit coverage. |
| Previous-sequence crossfade | Local candidate below implements the reference 0.2-second blend. Hardware acceptance pending; separate from accepted STEP movement. |
| Forced glowshell / other render effects | Local two-pass shell candidate below; ordinary chrome remains accepted, shell hardware acceptance is pending. Other forced render effects remain separate. |
| Custom viewmodel FOV/handedness | Normal pistol/crowbar path accepted; overrides remain unproven. |
| Other effect parity | Entity muzzleflash dynamic light, beams, glow/sorting/follow details and Studio wound decals remain outside accepted impact effects. |

Next operator observation: remain in `c1a0`, approach Barney and scientists,
observe head/body turns and standing/walking changes; check that accepted
lighting, chrome and absence of flicker remain intact. This natural scene
cannot by itself close forced wrap, 4-way blend or all sequence coverage.

#### Controller candidate hardware outcome — accepted natural-scene regression

Engine `20260909T191350217Z_PPSA99996_xash3d-engine_0x15ef302087045` and
renderer `20260909T191350314Z_PPSA99996_ps5-xash3d_0x15ef307a587b5` pass the
paired live lightmap/2D/menu/brush/Studio validator: 10,989 frames, 10,770
world-view frames, 97 ms start skew, nine exact resource reclaims, intact
guards, zero errors, clean BYEs and exact VideoOut/direct-memory/AGC teardown.
The operator reported “no, todo perfecto” when asked about jumps, trembling
and flicker during NPC turns and movement.

There are 65 bounded controller samples: all report two controllers and
`blends=1`; four samples contain different current/previous controller bytes.
This is evidence of the natural changing-controller path, not exhaustive
sampling or forced circular-wrap/2-way/4-way blend acceptance. Those cases,
previous-sequence crossfade and glowshell remain open.

The exact raw-FTP verified candidate above remains installed after its automatic
close. No second launch, manual close, control-profile edit or asset change was
performed for this validation. Code is local on `feat/phase7-studio-coverage`;
PR/merge and the lab plan update have not yet been performed for this increment.

#### Previous-sequence crossfade candidate — not deployed

The following sequence candidate is now included in the combined QA build
described below; it has not had a separate hardware launch.

The adapter now evaluates the latched previous sequence at its frozen
`prevframe`, uses its own `prevseqblending` for 2/4-way poses and blends it
with the current pose over the reference 0.2-second interval. Current and
previous evaluation share one function; external animation groups remain
engine-owned and only finished matrices cross the immutable frame boundary.
The previous-frame latch is updated only outside the active crossfade and
after successful finite-matrix validation. Sequence bounds, blend counts,
bone parent/controller references, nonfinite frames and weights are checked.
The reference previous-frame clamp/reset behavior is retained.

Host tests execute the actual extracted adapter evaluator with deterministic
bone-math doubles to verify 1/2/4-way routing, axis order, motion suppression
and malformed-input rejection. This does not independently validate upstream
quaternion interpolation or compressed animation decoding. Timing/latch-weight
tests include start/midpoint/expiry, an unset latch, invalid indices, frame
clamps and nonfinite values; scalar ASan/UBSan checks pass. Full host suite and
native build pass. Source checks enforce previous-sequence blending inputs and
the successful-pose-before-latch-update order.

`REF_AGC_STUDIO_CROSSFADE` samples active transitions every six scene calls,
reporting current/previous sequence, frozen previous frame, weight and times.
The marker was verified in the renderer ELF. Candidate renderer PRX SHA-256:
`f5f8bf3d52244f2eb1a0979aa04e09027e6e82008cf8b3d241820964471892e5`.
Engine SELF remains the normal `6445127bd600a19b4af405ef9eeda12de6c95a7ce1a5ad479ac184baa774f16b`.
No deployment or launch yet; the console retains the accepted controller-only
candidate. Natural sequence-change QA and paired resource closure are next.
Forced blend/wrap cases, glowshell and broader Studio parity remain open.

#### Combined Studio coverage — accepted hardware subset (2026-09-09)

The next single QA build combines sequence crossfade, forced controller/blend
modes and glowshell. `XASH_STUDIO_COVERAGE_QA=1` is off by default, requires
the complete MainUI/ref_agc stack and at least 300 map-relative seconds, and
rejects conflicting diagnostics. Touchpad mode 0/1/2/3/4/5 cycles through
normal, linear controller, circular wrap, actual 2-blend sequence, actual
4-blend sequence and glowshell. See `SCEPAD_PHASE5.md` for operator guidance.
Modes 3/4 select only valid existing sequences and explicitly log unsupported
models; no sequence count or animation allocation is fabricated. Host tests
already force 2/4-way routing, but absence of matching console assets remains
an explicit hardware coverage gap. Forced entity state is a local copy, never
a server-state or on-disk model edit. Cvars are not archived; restart resets 0.

Glowshell adds a second draw pass after each normal Studio entity. Shared
world-space face normals, accumulated across submodel meshes and normalized,
expand vertices by `max(1, renderamt)/128`; first-referenced normal indices
stabilize chrome mapping across shared vertices. The engine owner captures the
default chrome sprite handle before publication. The second pass has its own
transient vertices/constants/texture tables, rotating chrome origin using
`r_glowshellfreq`, additive draw flags, full alpha and rendercolor (white when
all channels are zero). Normal texture/lighting remain in the first pass.
No shader change or retained engine pointer crosses the frame boundary.

Host tests verify a normal-plus-shell pair, additive routing, half-unit
expansion at amount 64, packed tint, invalid texture/nonfinite rollback and
the existing normal/chrome/viewmodel paths. ASan/UBSan pass for the live Studio
geometry tests. `REF_AGC_STUDIO_SHELL` reports bounded shell draw/vertex counts
and texture identity; `REF_AGC_STUDIO_BLEND_QA` reports selected/unsupported
sequences. Controller samples identify diagnostic mode; their current/previous
bytes describe raw entity input, not the forced scalar override in modes 1/2.

Operator reported all modes visually correct after two complete touchpad
cycles. Paired evidence validation passed: 18,178 frames, 17,960 world views,
zero errors, nine resource reclaims, intact guards, exact ownership, VideoOut
closed, direct memory released and clean engine/renderer BYEs. Runs:

- Engine: `20260909T194723366Z_PPSA99996_xash3d-engine_0x160c7b9664654`.
- Renderer: `20260909T194723463Z_PPSA99996_ps5-xash3d_0x160c7bf39d9a5`.

Crossfade telemetry records changing previous weights. Mode 3 selected real
sequence 6 on entity 63 (`supported=1`). Mode 4 reported `supported=0` for all
visible models: **four-blend hardware coverage remains open**, despite the
successful mode cycle and host routing tests. Shell samples show 64 draws /
3,372 vertices with texture 29, then zero shell draws after return to normal.
This accepts the observed controller/crossfade/two-blend/glowshell subset,
not full glow/sorting parity or a release configuration.

Final combined candidate: full host suite, native build, publication audit and
live Studio ASan/UBSan pass. All mode/crossfade/shell markers were verified in
the actual ELF outputs. Diagnostic engine ELF SHA-256:
`f769cb704533711d9612a997b6dffc055ab6f46b4ec178ee4394b7e72afa002a`;
SELF `5b107c76ed3edfd7d8781f0b4e6a7b861b24440ff65e0d14e65b297073bf0f0e`;
renderer PRX `1ba4540065cd3ecaa16bca5e74b3f90791be1004e9184f14c41c028b3e480ce1`.
This accepted five-minute build is diagnostic-only; normal configuration
disables the touchpad test cycle.

At 19:59 UTC the normal research configuration was rebuilt and restored with
all nine files verified via raw FTP SHA-256, without launching it. Coverage,
weapon-grant and recovery probes are off; the existing 180-second map-relative
gate and audio-off setting remain (this is not a release build). SELF SHA-256:
`6445127bd600a19b4af405ef9eeda12de6c95a7ce1a5ad479ac184baa774f16b`;
renderer PRX `470fbeb293256843c626ad7feec3390c3a816053faf01d6a0da2646d7c925252`.

### Earlier viewmodel-event implementation record (historical)

The following entries preserve the earlier candidate and its subsequent QA.

Branch `feat/phase7-viewmodel-events` adds engine-thread attachment transforms
using the already evaluated viewmodel bones and delivers client Studio events
through the actual RefAPI `pfnStudioEvent` callback. Validate array spans,
attachment bone ownership, finite transforms, event frame ranges and terminated
options before dispatch. A repeated view at the same client time cannot replay
side effects; pause suppresses events and map load resets the cursor. The event
window uses open-start/closed-end intervals, including the first frame and loop
wrap; a long stalled frame dispatches each event at most once, not a burst of
all missed loop repetitions. Bounded event telemetry identifies model, sequence,
event/frame, total count and engine-thread ownership. No shader/input changes.

Host tests cover window boundaries, wrap/stall, malformed spans, attachment
rotation/translation and nonfinite output rollback; ASan/UBSan pass. This is
not full dispatch integration or hardware acceptance. The client callback can
create muzzleflash temp sprites, but the live AGC sprite drawing path remains
separate unfinished work; do not claim visible muzzleflash from event delivery.
EF_MUZZLEFLASH entity lights and other effects also remain explicitly pending.
Next operator QA: fire several pistol rounds with R2, reload with Square,
then switch to crowbar and attack; correlate real sequences/client events.

Host suite and final native build passed; the event marker was verified in
renderer ELF `c2a093c7e109bfb4ed4f39eca355ea23641e5660afb2d07597d1dae821c6079b`.
Renderer PRX: `addde4294a5543c8f642c75117c45786838c9fa74cedf12cd598cead4792ffe2`.
This candidate uses the opt-in 300-second weapon-grant build for QA. It has
not been deployed or launched; the console retains the prior normal build.

Follow-up: with operator present, exact raw-FTP verification preceded launch
at 2026-09-09 17:11 UTC. Engine run
`20260909T171128240Z_PPSA99996_xash3d-engine_0x15845982b81a6`
confirms DualSense profile v5 loaded and `Spawn Server: c1a0`. Candidate
hardware QA is now in progress; no event/visual/resource acceptance yet.

Operator outcome: "se ve bien todo ok se repone etc y la palanca bien al atacar".
This accepts the observed reload animation, magazine replenishment and crowbar
attack. The same engine run logs pistol sequence 3 event 5001 (frame 0), and
reload sequence 6 events 5004 at frames 4 and 23, delivered on the engine thread.
This confirms actual client-event dispatch, not audible playback or rendered
muzzleflash sprites. Exact attachment placement and final resource teardown
are not established by this feedback; those checks remain open.

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

## Live impact effects — incremental sprite candidate (2026-09-09)

Operator reports missing wall bullet marks and visible blood despite correct
damage/death animations. These are separate renderer paths: the adapter still
inherits null `R_DecalShoot`, `CL_DrawParticles` and `CL_DrawTracers`. Do not
interpret successful damage, reload or Studio event delivery as effects parity.

First increment connects live sprite entities, including temporary blood and
muzzleflash sprites. On the engine thread, `R_GetSpriteFrame` resolves the
decoded frame; only its texture handle, orientation and extents enter the
immutable frame snapshot. Worker geometry and descriptors use the existing
retired transient slot, rolling back allocation on failure. Billboard/upright/
oriented/rolled quads reuse existing surface shaders and texture ownership.
Sprites are drawn after Studio and before HUD. Synchronous muzzleflash entities
emitted during viewmodel events inherit the weapon's compressed depth range.
This tagging does not establish attachment-follow parity across later frames.

Host tests cover axes/UVs, rotation, degenerate upright sprites, nonfinite
inputs, tint/alpha, viewmodel depth and allocation/missing-texture rollback.
The full host suite and ASan/UBSan sprite test pass. Native compilation passes;
the `REF_AGC_LIVE_SPRITES` marker is present in the renderer ELF. No hardware
acceptance or deployment of this sprite increment has occurred yet.

Final candidate identity: renderer ELF SHA-256
`829f41c93226b50db4a552f8f645d4098500a8949c7a6293593bedc9d4a21868`,
renderer PRX `6c2e6210cd5c3175a9872bbffeb239f4fd608f89ad993efd927d9ed05483fd4a`.
Engine/SELF remain the preceding viewmodel-events QA build; only the renderer
changed. Publication audit passes (428 allowlisted files). Await operator
presence before deployment/launch; do not overwrite this boundary with an
assumed hardware result.

QA: retain accepted DualSense v5, fire the pistol (R2), then check NPC blood
impacts and any rectangular opaque backgrounds; correlate sprite telemetry.
Wall decals, particle simulation/drawing and tracers are subsequent increments,
not included in this first QA. Sprite interpolation, follow attachments,
glow-specific occlusion/depth policy, distance-dependent minimum scaling and
full translucency sorting remain outside the current parity claim. No changes
to lighting, shaders, aim or private game assets are part of this increment.

## Combined sprite / particle / decal candidate — awaiting hardware

The operator requested one combined launch, superseding the sprite-only QA
sequence above. No sprite-only candidate was deployed or launched.

`effect_bridge.h` now overrides the null particle/tracer/decal callbacks.
`CL_DrawEFX(dt, true)` executes once per engine frame with a world view;
paused frames pass zero dt. Engine-owned `CL_ThinkParticle` retains particle
behavior/custom callbacks, while tracer integration follows the pinned GL
implementation. Engine builtin particle texture, default dot sprite and
palette supply geometry/color. Only copied vertices cross to the worker;
consecutive equal texture/mode/alpha polygons share a GPU draw. The existing
packed Studio RGB shader path supplies per-vertex colors without shader edits.

Decals project onto eligible BSP surfaces and clip to the UV square/surface
intersection. A 0.03-unit normal offset avoids coplanar depth conflict. Parent
entity coordinates are inverted when shooting and transformed back each frame,
so translating/rotating brush models retain attached marks. Water, sky,
conveyor and stencil-dependent transparent surfaces are excluded. Persistent
here means retained across frames in the current map, not permanent unlimited
storage: a bounded 256-fragment pool replaces old non-permanent entries,
preserving `FDECAL_PERMANENT`. Clear-map, entity-storage release and renderer
shutdown reset it; texture removal respects permanent decals. Serialization
exports one entry per shot, local-space flags, plane, scale and basename in
age order. Save/load and cross-map decal restoration are **not hardware
accepted** by these host tests.

Current bounded geometry contracts: up to 32 vertices per decal polygon,
32,768 effect vertices / 4,608 polygons per frame. Capacity drops are counted;
GPU allocation failure rolls back the transient slot. These are effect bounds,
not a change to the accepted texture-memory policy. Larger BSP polygons,
Studio-model wound decals, beams, full translucent sorting and special glow
occlusion remain outside this implementation. Blood impact sprites, blood
particles and brush/world blood decals do not depend on Studio wound decals.

Tests execute the production bridge with synthetic engine callbacks, checking
entity translation, permanent-entry retention, bounded replacement, clear and
serialization, once-per-frame integration, pause and hidden-particle updates.
Separate tests cover clipping, normal offset, UV bounds, batching, packed color,
invalid input, missing texture and allocation rollback. Both effect test
binaries pass ASan/UBSan. Host tests do not constitute visual acceptance.

Combined console QA (single launch, diagnostic weapons enabled):

1. Check menu, chapter caption, world and NPC textures for regressions.
2. R2 pistol: inspect muzzleflash, fire several spaced shots at an opaque wall,
   then turn away/back and verify marks remain anchored without rectangles or
   flicker. Crowbar impacts should also produce the game's configured effects.
3. Shoot an NPC: inspect blood impact/splatter, transient motion and expiration;
   animation/damage alone is not acceptance of these effects.
4. Where accessible, mark a moving brush door and check the mark follows it.
5. Leave the session running for its normal exit; inspect
   `REF_AGC_DECAL_SHOOT`, `REF_AGC_LIVE_EFFECTS`, `REF_AGC_LIVE_SPRITES`, drops,
   renderer errors and paired resource teardown before closing the gate.

Keep current DualSense v5 unchanged. Graphics QA still has audio disabled.

Final combined candidate: full `make -B all` passed, including the production
effect-bridge test and publication audit (433 files). Native build passed;
all three effect markers were verified in the renderer ELF. Renderer ELF
SHA-256 `a2d882dc1e39f46ab2ee061fe63996bcf89d67dcdfed3685545507c25a193c04`,
PRX `a550734579b1a5fcc4edad5de5050f89d9e0aca937fc8adb085552a6a8df9999`.
Engine ELF remains `7cb95f38a88638e01dce8f981e210e4d044fad0bd851d4a69d1c27c49429b644`;
SELF remains `d6ff88953965ac2308d5c22303659d5f764eefc26c89e0c4335498e493cecc20`.
Five-second menu / 300-second map gate, diagnostic weapon grant enabled, audio
disabled. **Prepared locally only: not deployed or launched, no hardware
run ID or visual/resource acceptance yet.**

Deployment/launch follow-up: operator confirmed presence. At 2026-09-09
17:47 UTC the nine-file bundle was promoted with exact raw-FTP SHA-256
verification after confirming no running big app. One launch was verified as
PPSA99996, app ID 24600. Engine run
`20260909T174713363Z_PPSA99996_xash3d-engine_0x15a3909b6b1d7`, renderer run
`20260909T174713463Z_PPSA99996_ps5-xash3d_0x15a390fb2b7ba`.
The engine entered c1a0 and logged the diagnostic weapon grant. Combined
operator QA is now in progress; this is not yet visual/resource acceptance.

The operator missed that session and requested a relaunch of the unchanged
candidate at 17:54 UTC (app 32792). Runs:
`20260909T175447407Z_PPSA99996_xash3d-engine_0x15aa2c05bc2df` and
`20260909T175447500Z_PPSA99996_ps5-xash3d_0x15aa2c5f521cc`.
Operator accepted visible wall marks, muzzleflash and blood, but requested
more noticeable blood. Sampled renderer telemetry contains particles, tracers,
sprites and decals with zero reported capacity drops. This feedback does not
close full effects parity or replace paired resource validation.

## Sprite lighting and optional enhanced blood — local candidate

At operator request, eligible alpha-tested sprites now sample the unchanged
shared `R_LightPoint` implementation on the engine thread, after view capture.
Eligibility follows pinned `R_SpriteHasLightmap`: sprite format 3, renderamt
above 127, normal/alpha/texture modes, no EF_FULLBRIGHT, and
`r_sprite_lighting` enabled. Only numeric RGB crosses the frame boundary.
The factor multiplies tint in the existing single-pass surface shader; we do
not reproduce the legacy second framebuffer-modulation pass. This avoids
darkening the scene behind transparent sprite pixels. No dynamic-light
addition beyond the pinned sprite sampler, no shader or input changes.

Generated `ps5_cl_tent.c` (pinned source untouched, generator fails on drift)
registers archived `ps5_blood_amount` during temp-entity initialization.
Requested default is 1.5: approximately 50% more droplets (integer rounding),
and sqrt(1.5), about 22%, larger blood sprites. Value 1 restores the original
count/size; supported interval 1..3, invalid values use 1. Droplet generation
is bounded to 96 per impact. Lifetime, damage, hit detection, multiplayer's
existing blood rule, violence controls and other effects remain unchanged.
This is a presentation enhancement, not a claim that upstream was missing
those droplets. Graphics candidate still uses diagnostic weapons/audio off.

Validation pending: host sampler/tint tests, deterministic generator/drift
tests and native build; then operator comparison of blood visibility and
lighting. Not deployed or visually accepted yet.

Local verification completed: full host suite and publication audit (435 files)
pass; sampler and sprite tests pass ASan/UBSan. Native build passes, with
`ps5_blood_amount` and `XASH_BLOOD_PRESENTATION` verified in the engine ELF.
Candidate hashes: engine ELF
`f2b3a55d0e601c887782c3ea103687073f02212c2c4f5d4464c193f09be15f8f`,
SELF `1d6e2c3a83e9ed5e24d1cd101d29252c80c420b388198027d8592fbc50116c3b`,
renderer ELF `72ec971185aca62c5d795649e596b71ce8b43398278a957375b0963d381deeb0`,
renderer PRX `d7003f1c56e84cc91bcc78d7f7e7e300b8e612b42881d2fbed7ddc23c3c89386`.
Still local only, awaiting operator presence for deployment/QA.

Operator-present follow-up at 2026-09-09 18:17 UTC: exact raw-FTP verification
and promotion completed for all nine bundle files, then one launch was
verified as PPSA99996 / app 40984. Engine run
`20260909T181705427Z_PPSA99996_xash3d-engine_0x15bda477abfcd`, renderer run
`20260909T181705519Z_PPSA99996_ps5-xash3d_0x15bda4cecca71`.
Sprite-lighting / enhanced-blood visual QA is in progress, not yet accepted.

## Accepted effects checkpoint and normal restoration — 2026-09-09

Operator: more splatter/presence, blood marks appearing on wall/floor,
everything visually OK. This accepts the tested blood enhancement and sprite
lighting alongside previously accepted muzzleflash/wall marks. The 18:17
engine/renderer pair above passes the validator with live lightmaps, 2D, menu,
brush and Studio requirements: 18,175 total frames / 17,957 world-view frames,
nine resource reclaims, intact guards, nonzero framebuffer hashes, zero errors,
all PRX unloads and both clean BYEs. GPU texture peak: 68,079,104 bytes.
Observed effect samples report no capacity drops. These tests do not close
beam effects, sprite glow/sorting parity, Studio wound decals, multi-map
validation, sound, performance or the release soak.

Validator accounting now correlates the separate viewmodel record rather than
requiring it to appear in the NPC list. Anonymous temporary Studio entities
(shell casings, server index zero) can coexist; positive entity indices must
remain unique. Regression tests reject missing/duplicate/incoherent viewmodel
records and duplicate persistent entities. Historical manifests are unchanged.

At 18:26 UTC, after external confirmation that Xash3D was stopped, the normal
no-grant candidate was restored via exact raw-FTP verification of all nine
files, without relaunch. `XASH_VIEWMODEL_QA=0`, Studio A/B off, existing 180 s
menu/map diagnostic harness and audio-off graphics configuration retained.
This is **not** a new normal-build hardware run or a finished release package.
Engine ELF `0c0b79677bd6dabba954305fd4f08143a6bb5d80615e9052d5f45d3a4d3c0e2e`,
SELF `6445127bd600a19b4af405ef9eeda12de6c95a7ce1a5ad479ac184baa774f16b`.
Renderer is unchanged from the accepted candidate (`d7003f1c…` PRX).
DualSense v5, sprite lighting and reversible `ps5_blood_amount` default 1.5
are retained; `ps5_blood_amount 1` restores original blood presentation.

Next: close the presentation-overlay cleanup, investigate startup audio
underruns and (if desired) supply a real four-blend model for console coverage;
then proceed to fixed-camera comparison, gameplay/performance soaks and the
release package. Audio and HD are accepted first passes, not full release
coverage (map-transition audio and long-session HD performance remain open).
