# Phase 7 fullscreen menu and c1a0 recovery — historical gate record

> Phase 7 is closed and the public runtime is **PLAYABLE**. The sections below
> retain the original gate scopes and failure analyses; they are not pending
> release requirements. See [`PLAYABLE_RELEASE.md`](PLAYABLE_RELEASE.md).

The bounded FW 12.02 regression passed on 2026-09-09. Native MainUI fills the
1920x1080 output, the engine queues `map c1a0`, and the map becomes visible
through AGC before exact teardown. This closes the recovery regression only.
The recovery alone does not close brush entities, Studio drawing, viewmodel,
gameplay or release gates. The subsequent bounded brush validation is recorded
below. The tested branch includes preliminary brush and Studio resource work;
resource storage is not evidence of complete Studio rendering.

## Failure and correction

The renderer cache can already contain a loaded BSP while the consumed loading
frame still has no map range or valid view. The old consumer tried to count a
zero surface range and parked at `live-world-count`. World, sky and brush
composition now wait for a valid world range and view, while loading/2D frames
continue through normal submission, retirement and acknowledgment. Deferred
frames are explicitly logged; invalid nonempty cache ranges still fail the
draw contract. Logical canvas dimensions are captured from the engine and
the native client uses 1920x1080, fixing the upper-left menu presentation.

A separate operator error mixed a PRX built against the c1a0e proof resource
with the installed c1a0 resource. `bsp_bundle_load=-2` was the size check;
that run never submitted native frames and is rejected. The Phase 7 baseline
target now requires c1a0.bsp. The canonical transactional deploy preflight
requires all six PRXs, both assets and matching compiled BSP identity.

## Accepted evidence

- Engine run: `20260909T093418265Z_PPSA99996_xash3d-engine_0x13f53250523fc`
- Renderer run: `20260909T093418322Z_PPSA99996_ps5-xash3d_0x13f53284f02fe`
- Paired validator: `--require-live-lightmaps --require-live-2d --require-live-menu`, pass.
- 1,155 matched frames; 930 valid-view frames; zero structured errors and gaps.
- 220 pre-map 2D frames, 95,543 quads and 27,547 draws. Map metadata at serial
  221; first valid 1920x1080 3D view at serial 226. These are different events.
- Serial 218 explicitly deferred the world (`map_serial=0`, range zero) and
  continued through 2D. Engine capture subsequently reports root-world range
  0+2,825 within 3,695 total model surfaces.
- Live world cache: 17,245 vertices, 29,565 indices, 3,695 surface records.
- GPU buffers both `1e8207b335eb79f3`; 3,977,492 bright pixels.
- Nine resources reclaimed, guard checks intact, native teardown result zero,
  five engine PRXs unloaded, engine memory empty and both clean BYEs.
- The ninth resource is the preliminary 32 MiB Studio cache arena. The validator
  accepts nine only with its explicit positive cache accounting and ownership
  marker; missing markers, eight reclaims with that cache, and overflow fail.
- Independent post-run status: no BigApp; FTP, shsrv, elfldr and ps5debug healthy.

| Artifact | SHA-256 |
| --- | --- |
| Engine ELF | `11ef79361c9e8a6a2b9c24eaf5e5e7636c2925567bc945e26d682e8533ae6367` |
| Engine SELF | `249fd8eaf95d2c53dd60131ff0949bc6337f5a23700ac65784dddc34bbc542b6` |
| ref_agc ELF | `20d570533cce5766f4e93d74463b64db424c3bdd55152df1c1dbf310d04deea1` |
| ref_agc PRX | `74a7ef1d0d4316d9c9c7feac9c205d75689b6f913ede312f638424a8dafe5771` |
| c1a0 proof bundle | `e868f5b9d2ab98fbd671b426caf441a11666dd9816bfa603407f05531f85802f` |
| Studio support bundle | `d5b3a1f9b5c9035b02e678079b3586a5fe35987d55167dab27868050969b3e31` |
| Engine transcript | `99805c355e03855ca87261f74758fec3d21e929642f8c3249dbc41df51e03e33` |
| Renderer transcript | `d092828656d323bf4f7080ddf3807954c9b4ebe9091e8688ea7df7743364da77` |
| Private 40-second CLI video | `00fda961fb40f1183ab19d603f96c44ae05d007d411fc4f81d09bb0077534366` |

The private lab stores `phase7-baseline-coherent-deploy-20260909.jsonl` and
`phase7-baseline-coherent-launch-20260909.jsonl` under `research/xash3d/`.
The video is `phase7-baseline-coherent-20260909T0934Z.mp4` under the ignored
Remote Play captures tree. Its 10-second frame shows fullscreen MainUI; the
independent live screenshot shows the c1a0 airlock facing reception (Anomalous
Materials, not the c0a0 tram). No joystick input
was required for this bounded regression. It auto-quits after 25 seconds.

## Brush entities: accepted c1a0 validation, 2026-09-09

This follow-up validates the existing native brush submission for c1a0 normal
and alpha-test entities, including translation and rotation. It does not close
all Phase 7 rendering modes, Studio drawing, viewmodel or gameplay.

- Engine run: `20260909T094643623Z_PPSA99996_xash3d-engine_0x14000af1ef631`.
- Renderer run: `20260909T094643679Z_PPSA99996_ps5-xash3d_0x14000b282f6bb`.
- Paired validator passes with `--require-live-lightmaps --require-live-2d
  --require-live-menu --require-live-brush --map c1a0`; engine `9aa39ad`,
  HLSDK `e277ffa`, bundle identities and sizes unchanged from the recovery.
- 1,214 matched frames, 988 valid-view/brush frames, 17,784 accumulated
  instances, 201,552 brush draws and 1,636,128 submitted brush indices.
- Three sampled poses (serials 227, 601, 1201) each report 18 accepted entities,
  204 draws and 1,656 indices, with zero rejections. Fifteen are normal;
  the three classified as `alpha` are mode 4 (alpha-test), not translucent glass.
- Eleven entities change their actual origin/angles: chair `68/*17`, door leaves
  `120/*30` and `123/*32`, bars `121/*31`, `124/*33`, `125/*34`, `126/*35`,
  wheels `127/*36` through `130/*39`. Camera changes are zero, so this is not
  merely an MVP hash change caused by moving the camera.
- The private CLI video at 20 seconds shows the closed airlock; at 29 seconds
  the leaves are open and bars displaced, revealing reception. No joystick
  input was required. These are frames of the same run, not separate builds.
- Menu still precedes the map (221 pre-map frames). Both clean BYEs, zero
  structured errors, nine resource reclaims, intact guards and exact teardown.
- Independent post-run status at 09:50:56 UTC: no BigApp; FTP, shsrv, elfldr
  and ps5debug healthy (`phase7-brush-validation-postrun-20260909.jsonl`).
- Host `make all` passes, including publication audit (402 files). Added tests
  exercise GoldSrc-to-AGC translation, 90-degree yaw, alpha constants and
  rejection of invalid brush evidence/ranges/accounting/missing completion.

The instrumentation now emits `REF_AGC_LIVE_BRUSH_ENTITY` sampled poses and
`REF_AGC_LIVE_BRUSH_COMPLETE` totals/ownership. The validator checks bounds,
unique instance indices, pose shape, class counts and sampled vs final totals;
it reports moving identities rather than assuming every map must animate.

| Follow-up artifact | SHA-256 |
| --- | --- |
| ref_agc ELF | `0325729fb70502dea40dd9dbf2dd976e8018a8bfca0d2012c6eac9d4ad3ae22a` |
| ref_agc PRX | `4f3493813317bee5f30494e92dd33ce3a5f3968f731eafc6bc9eebdf2d3ef971` |
| Engine transcript | `f860e6bf91d0922186e1843dde9bf4c6efc8f15570aa16438def9662f65783b4` |
| Renderer transcript | `c3da67783364f45d8be6d48dd97aa2c062468cfbdc56a33d064cc4bc6fa66422` |
| Private CLI video | `7b6a1e97f523e95118f23d0a77bc4ce904ae883d9656efe77858fb8339eba08f` |

Private lab journals: `research/xash3d/phase7-brush-validation-{deploy,launch}-20260909.jsonl`.
The video is `research/gpu/captures/remoteplay/phase7-brush-validation-20260909T0947Z.mp4`.
The canonical deployment staged and read-back verified the engine SELF, six
PRXs and both assets as one coherent set. This turn does not change the engine
SELF or asset identities in the recovery table.

Still unvalidated here: additive/transcolor brush blending, turbulent/water
brushes and their ordering. No such coverage may be inferred from the generic
`alpha` counter. Studio cache residency is also not Studio draw evidence;
that is the next separate rendering checkpoint.

## Studio first-draw candidate: runtime passes, visual quality remains open

The first live Studio drawing candidate has run on hardware. Submission and
animation have positive evidence, but **visual quality is not accepted**:
the operator reports jagged/flickering borders and textures. The correlated
results below must not be promoted to complete Studio rendering acceptance.

The producer uses the engine's `R_StudioGetAnim` callback (including external
sequence groups) and statically compiled upstream `R_StudioCalcBones`, quaternion
blending and matrix functions. It evaluates the current network frame plus
elapsed animation time, current controllers/mouth and 1/2/4 sequence blends.
It publishes up to 32 owned poses with 128 world-space bone matrices each;
no engine animation pointers cross the acknowledged frame boundary. Scene
clearing resets the pose count; BeginFrame preserves the already staged poses.

The native consumer reads the cached engine-decoded Studio v10 geometry,
selects bodygroups and skin families, expands triangle strips/fans, skins
vertices with the published matrices and resolves the engine's texture handles.
Geometry, indices and descriptors belong to the existing transient slot and
retire under its GPU fence, VideoOut token and engine acknowledgment. Invalid
poses, ranges, bones or missing textures fail before submission; this is not
a baked-model substitution.

Scope of this first visual test: complete textured bodies and changing poses
in c1a0. Lighting is explicitly `unlit`. Directional/ambient Studio lighting,
chrome UV generation, previous-sequence transitions, latched controller/blend
interpolation, follow-entity bone merging, render FX and viewmodel semantics
are not accepted by this candidate. Current entity transforms and ordinary
sequence interpolation must not be described as full upstream Studio parity.

Preflight results: `make all` passes (405 publication entries); the new geometry
test also passes ASan/UBSan. Tests cover coordinate conversion, strips/fans,
pose changes, missing poses, invalid bone/command bounds and transient rollback.
The frame test proves poses survive BeginFrame and are copied, not aliased.
The opt-in paired validator `--require-live-studio` requires positive draw
totals, sampled model/bone evidence, changing poses and exact final ownership.

- Candidate ref_agc ELF: `342bfddc4035bce178008d53158cd4fd107279e3f6fc12229bea6eceb06af132`.
- Candidate ref_agc PRX: `5a3438175d3d9089a8dd3bd63867f9cf86df3f6596e038a355412e4ceaec0b02`.
- Completion/frame markers verified inside the ELF. No undefined Studio math,
  matrix or `strcasestr` imports; upstream math is linked into the module.
- Canonical nine-file transaction read-back verified and promoted at
  10:07:49 UTC. Private journal: `research/xash3d/phase7-studio-deploy-20260909.jsonl`.
- Engine SELF and both support assets retain the accepted recovery hashes.

Operator workflow: no Remote Play and no recordings by default. Coordinate
presence before launching the bounded menu-to-c1a0 test. Ask whether
the guard at reception and scientists appear with complete bodies, correct
textures and visible animation after the airlock opens. Record that answer as
operator-reported visual evidence separately from the automatic log checks.

### Three-minute operator run and input integration correction

Use `make engine-phase7-menu-native-release PHASE7_GATE_SECONDS=180
PHASE7_GATE_FROM_MAP=1` with the documented c1a0 inputs. Defaults retain the
25-second regression. `PS5_XASH_ACTIVE_MAP_TIMER` marks a single clock rebase
at `cls.state == ca_active`; menu/loading does not reduce the full 180-second
map interval. Failure to reach active signon still times out from startup.

First three-minute run:

- Engine: `20260909T101339725Z_PPSA99996_xash3d-engine_0x14178f4fc2023`.
- Renderer: `20260909T101339782Z_PPSA99996_ps5-xash3d_0x14178f875d094`.
- Engine ELF `dab46a4eaef606ef64078dd9ea0c99589b06c8c27f436dd61966cc827382b1c5`;
  SELF `701418bac37b2649f4558c5273ec2bbb52c3dedd7df57ad8b238b703a431583d`.
- Renderer hashes unchanged from the Studio candidate above.
- Paired validator with lightmaps, 2D, menu, brush and Studio requirements passes:
  10,997 matched frames, 10,771 Studio frames, 723,905 Studio draws,
  76,014,951 indices and 10,770 pose changes; Barney and scientist sampled.
- External `barney02.mdl`, `scientist02.mdl` and `scientist01.mdl` resolved by
  the engine. Both clean BYEs, nine reclaims and exact teardown, zero structured
  errors. Post-run: no BigApp, all four supervisory services healthy.
- Engine transcript SHA-256 `4c8908ee1b05efb4de44a39d83f5c7f7935ace0d21f4273df78c52c8359d37ed`;
  renderer transcript `43871aac3c23d088c891fdaa3e592f12e1a8892b9f1a315e3865b7608995c068`.
- Operator confirms guard/scientists present, moving, animated and correctly
  positioned, but describes poor visual quality, jagged edges and flickering
  textures. This is operator evidence, not a recorded capture. Sampling/aliasing
  is a hypothesis, not a demonstrated cause; assess distance and stationary
  camera behavior before modifying rendering.

The same operator could not move. Inspection confirmed ScePad init/poll/shutdown
was wired only into `PS5_XASH_PAD_GATE`; the live renderer correctly expected
the engine to own input, leaving no active pad owner in the ordinary client.
Normal client input now lazily starts ScePad at active signon, polls on the
engine owner thread and releases its handle/user-service ownership after
Host_Main. Its event sink is detached before teardown because input/cvars may
already have been destroyed. It never invokes the six-action gate's early
autoquit. Dedicated gate behavior remains unchanged. Host tests cover one-time
open, continued polling without gate success and idempotent shutdown.

The follow-up client SELF is `48395ac510aa1fb1acf2216962005c81a89a7aa50e774e75551429e843809854`
(ELF `878232f31299066486c1e3b4d8678c3f20d54a286bad2f7acc1e2a65f9724de2`).
It retains the exact same renderer to isolate input from visual quality.
Its completed run: `20260909T101931364Z_PPSA99996_xash3d-engine_0x141cad41bd1ec`
and `20260909T101931418Z_PPSA99996_ps5-xash3d_0x141cad7467f97`. Runtime ScePad
initialization succeeds, both sticks generate events and consumed-frame telemetry
shows changing camera origin/angles. The paired validator passes all five live
requirements: 10,997 frames, 10,771 Studio frames, 858,528 Studio draws,
90,815,742 Studio indices, 10,770 pose changes and 1,856 camera changes.
ScePad reports 121,616 connected samples, 8,703 movement samples, 4,301 look
samples, zero read errors and exact handle/user-service teardown. Buttons were
not exercised; this is not a repeat acceptance of the six-action input gate.
Active-map start and timeout are exactly 180 seconds apart in the engine log.
Both clean BYEs, nine resources reclaimed and zero structured errors. Independent
post-run status finds no BigApp and all four supervisory services healthy.

Transcript SHA-256:

- Engine: `a7373bc36286d7b6105929798d2c323bd61382f58df121d545988f48d6041906`.
- Renderer: `64ee936ed7bf78ac3adb829769c782a410ec50d221e811015e51063da473735f`.

Operator feedback after approaching the characters: edge jaggies and texture
flicker improve substantially at close range; the defect is most apparent at
distance. Code inspection confirms `ref_agc_gpu_texture_cache_apply` uploads
only the base image and builds single-level bilinear descriptors, despite the
CPU store retaining a mip-count field. Missing minification filtering is thus
a concrete candidate for texture shimmer, not proof that all silhouette aliasing
has the same cause. Next isolated visual-quality test: proper GPU mip residency
and minification sampling, compare the same model near/far and stationary/moving.
Do not substitute higher-resolution replacement art, and do not claim this
input-only run fixes image quality. No Remote Play session or capture was used.

### Distance-black wall: sampling probe (2026-09-09, unresolved)

The operator reports that wall geometry remains visible but black at distance,
with its texture appearing progressively when approaching. An opt-in build
(`XASH_SAMPLING_PROBE=1`) cycles root-world shading every 45 map seconds:
normal, base texture only, lightmap only, then explicit level-zero sampling of
both textures. Brush entities and Studio draws are not modified by this probe.

- Engine run: `20260909T102943333Z_PPSA99996_xash3d-engine_0x142594fbb6b5b`.
- Renderer run: `20260909T102943391Z_PPSA99996_ps5-xash3d_0x14259532d5e07`.
- Engine SELF remains `48395ac510aa1fb1acf2216962005c81a89a7aa50e774e75551429e843809854`.
- Diagnostic renderer ELF: `381ec0f5cba8c6fd68af4106c777abd57c856d3d89087c35f7ea53d24ed6a101`.
- Diagnostic renderer PRX: `707f378e651b43a43fac7a09b7e447ff59ceb718d820bfe0b06b67eb215e7ed1`.
- Deployment journal in the lab: `research/xash3d/phase7-sampling-probe-deploy-20260909.jsonl`.
- Mode markers: serial 227 / 1133 ms; 2843 / 45010 ms;
  5540 / 90005 ms; 8237 / 135000 ms.
- Operator clarification: only two visual checks were requested and observed.
  First, with lighting present, the distant wall was black and its texture
  appeared progressively when approaching. Second, nearby walls lost their
  textures while the target wall in the distance remained black. The latter
  appearance is consistent with the lightmap-only diagnostic, but operator
  observations were not individually synchronized to all four mode markers.
  Neither base-only nor explicit-LOD-zero has a separately confirmed visual
  result. The earlier attribution of "still black" to explicit LOD zero was
  an assistant error, not operator evidence; that conclusion is withdrawn.
- The paired validator passes run mechanics: 10,991 matched frames, 10,765
  Studio frames, 898,094 Studio draws, 95,162,280 Studio indices and 10,764 pose
  changes. Both runs finish cleanly. Independent post-run status at 10:38 UTC
  finds no BigApp and all four supervisory services healthy.

Automatic mode markers establish execution, not operator validation of each
mode. Future visual checks must identify and hold one mode until the operator
reports its result, rather than infer correspondence from message timing.
This is an unresolved diagnostic, not a visual fix. The single-level live
sampler already clamps minimum and maximum LOD to zero; missing mip chains
remain relevant to shimmer but do not establish the cause of the black wall.
Also, until the target is identified as root world rather than a brush entity,
unchanged black cannot conclusively isolate its lightmap or sampling path.
Next discriminate draw ownership and base-only appearance before changing
filtering or lighting. No Remote Play session or capture was used.

#### Follow-up QA protocol: operator-held modes

The replacement opt-in probe removes timed switching. A touchpad **press edge**
while the client is active cycles `r_agc_qa_mode`: 0 normal, 1 base-only,
2 lightmap-only, 3 solid cyan, then 0. Holding the button does not advance again.
The cvar is non-archived and reset at renderer initialization. Ordinary builds
retain the usual touchpad binding and publish probe mode zero. The mode travels
in the owned live-view snapshot; the renderer logs every consumed transition
as `REF_AGC_SAMPLING_PROBE schema=2`, including returns to a previous mode.

Coverage is root-world ordinary/alpha-tested surfaces and brush ordinary/
alpha-tested/translucent/additive surfaces. Sky, turbulent water and Studio
models are not the target of this wall probe. Unlightmapped surfaces show white
in lightmap-only mode. Solid cyan returns before texture fetches and alpha
discard, so masked cutouts intentionally fill in. Existing geometry, depth and
blend state are retained; translucent/additive brush surfaces need not appear
as uniformly opaque cyan. This test cannot by itself exclude depth/occlusion.

Use a 1,800-second active-map safety timeout for the interactive QA, with no
mode changes on timers. Before each change, record the current operator report;
ask for exactly one touchpad click, confirm the renderer transition, then ask
for the next observation. Do not conflate an input request with a consumed mode
or either of those with a visual result.

1. Find the same black wall in normal mode. First hold still and describe whether
   the black region is stable or flickering. Record camera telemetry.
2. From the same position and orientation, compare base-only, lightmap-only and
   solid cyan. Report whether the target changes, whether nearby walls visibly
   confirm the mode, and whether boundaries resemble triangles, blocks or a
   smooth gradient. Do not move between these initial comparisons.
3. Return to the mode that best discriminates the defect. Approach/back away,
   then rotate from a fixed position; describe progressive versus abrupt change.
   Treat distance and viewing angle as separate variables.
4. Only after the wall diagnosis, revisit Studio shimmer independently.

Host tests cover unchanged normal brush constants, mode encoding for every
diagnostic mode, invalid-mode fallback, preserved alpha, shader specialization,
and solid-branch placement before texture sampling. Hardware mode switching and
visual conclusions remain pending until the operator performs the new QA.

Manual-QA candidate built and deployed on 2026-09-09:

- Engine ELF `46886a9e15ed0a5b19bb5b020519d4bc2a5f6ba6ee512fd3105405a74c1c192a`;
  SELF `e6c8bcc7a192e7baefdcc18341f4cdc70f57380de3450581b29646bc677631ff`.
- Renderer ELF `485912d9a9d4baf49146055dff01bc93ccc0b1c0a3a6b76e16afb2c0dbf4d944`;
  PRX `b3b2e6ce32ca91b380a6ba759dee9fa905cee9f8a7052668312ce3d70c644a19`.
- Both control markers and the QA cvar description verified in the ELFs.
  Full shader/build validation and `make all` pass; publication audit: 405 files.
- Canonical nine-file raw-readback deployment:
  lab `research/xash3d/phase7-manual-qa-deploy-20260909.jsonl`.
- Engine run `20260909T104641154Z_PPSA99996_xash3d-engine_0x14346499d0eaf`;
  renderer run `20260909T104641211Z_PPSA99996_ps5-xash3d_0x143464d0ea183`.
  Launch verified as PPSA99996; run and operator QA are pending, not accepted.

Manual-QA operator observations (run still in progress):

- Normal mode: operator identifies the target as a stable black square/plane
  apparently in front of the camera, not necessarily a wall. It appears to move
  away when advancing and follow when retreating; it does not flicker. Apparent
  wall texture recovery may instead be this occluder moving behind the wall.
  Camera-relative geometry/occlusion is a hypothesis, not yet a proven cause.
- Base-only mode confirmed by renderer serial 10108 (`mode=1 name=base-only`)
  following `XASH_QA_MODE_REQUEST mode=1`. Operator confirms surrounding lighting
  changes, but the black square does not change; it still recedes/follows with
  forward/backward movement. Do not describe this as a fixed wall material bug.
- Lightmap-only confirmed by renderer serial 16670 (`mode=2 name=lightmap-only`).
  Operator confirms nearby surfaces lose their textures while the black shape
  remains unchanged, stable, and apparently follows/recedes with camera movement.
  Operator asks whether it is an effect or something intended to be textured or
  transparent; none of those explanations is confirmed yet.
- Solid-cyan confirmed by renderer serial 28476 (`mode=3 name=solid-cyan`).
  Operator explicitly confirms the environment becomes cyan but the shape stays
  black. This distinguishes it from the overridden wall shading, but does not
  yet identify which other draw/depth interaction produces the occlusion.

Follow-up source inspection finds a concrete suspect: `bsp_flat_build_clear`
places the background triangle at clip depth 0.999, while the native live path
binds the enabled depth target before `bsp_resource_compose_clear`. With the
camera projection near=1/far=8192, this is an interior depth, not the far plane.
Check/fix background depth ownership and validate on hardware before attributing
the operator's shape to this path. Skybox and other non-overridden draws remain
alternative contributors; no hardware fix has been tested yet.

#### Background depth isolation candidate

The live-world clear now binds the existing disabled-depth state before its
color draw and restores the complete ordinary depth state immediately afterward,
before sky/world geometry. Both commands are error-checked; a failed clear or
restore prevents successful composition. This leaves the clear color, clip
vertices, texture filters, lightmaps and map geometry unchanged. The independent
depth-buffer fill remains 1.0. A periodic `REF_AGC_BACKGROUND_DEPTH` marker
identifies the candidate and the commanded state transition, not visual success.
A host source-contract regression checks disable → clear → restore ordering.

The manual-QA run was externally closed at 10:58 UTC to replace the bundle;
supervisor verified no BigApp and all four services healthy. Do not count this
operator-driven early replacement as an engine timeout/teardown acceptance.
New hardware validation is pending. Start with normal shading, locate the same
view, then compare near/far and camera rotation before asserting the cause fixed.

Candidate renderer ELF `f2a13ae66240bb51454fd1a00461cab47f7b509308944fd417e33cc8ffb27896`;
PRX `a23054f739a3ebcae970760cc9ad9849f06995b0cee7462739ff9165d7e82f3e`.
Engine SELF is unchanged from manual QA (`e6c8bcc7…`). Build and `make all` pass
(405-file publication audit); the new marker is present in the renderer ELF.
Deployment journal: lab `research/xash3d/phase7-clear-depth-deploy-20260909.jsonl`.
Nine-file promotion completed with exact raw readback. Candidate launched as
PPSA99996: engine `20260909T105928815Z_PPSA99996_xash3d-engine_0x143f905380475`,
renderer `20260909T105928873Z_PPSA99996_ps5-xash3d_0x143f9089909ac`.
Operator confirms on this candidate: "si ya desaparecio y veo todo a distancia
ok". The camera-following black shape is gone and distant surfaces are visible
in normal shading. Renderer logs confirm `mode=0 name=normal` and repeated
`REF_AGC_BACKGROUND_DEPTH clear_test=0 clear_write=0 world_depth=restored`.
This accepts the visual correction of this defect in the tested view: the
background color triangle was incorrectly participating in scene depth. Texture
filtering, lighting and geometry were unchanged in this isolated comparison.
End-of-run teardown and broader regression coverage remain pending; this is not
completion of Phase 7 or acceptance of the separate Studio aliasing/shimmer issue.

### Studio minification candidate (2026-09-09, pending hardware QA)

Operator requests correction of strong shimmer/flicker when characters walk and
of jagged edges. Whether whole body parts disappear versus texture/edge shimmer
has been asked explicitly and is not yet established. Do not infer that texture
filtering repairs animation, visibility or silhouette aliasing.

The first isolated candidate generates GPU mip chains for opaque Studio images
identified by the producer's `#… .mdl` naming contract (without the space).
TF_NOMIPMAP, TF_NEAREST, alpha-bearing and normal-map textures are excluded.
World, menu and sky keep their current path. Source mip metadata is retained;
the separate owned `generate_mips` property requests GPU generation from RGBA
level zero. GPU entries report their actual resident level count.

The cache uses the same reverse-level, 256-byte-row-aligned linear layout as the
existing BSP mip path. It builds each reduced level with box averaging, covering
odd-sized edges, uploads/flushes the whole allocation, and enables trilinear
sampling only when more than one level exists. Replacement still requires prior
GPU use retirement; allocation size includes every level. Opaque alpha remains
opaque. There is no new per-frame upload or pose change in this candidate.
`REF_AGC_STUDIO_MIP_RESOURCE` records handle, dimensions, levels and resident bytes.

Host checks cover metadata propagation, checkerboard averaging, layout and
descriptor mip selection, odd dimensions, 1x1 fallback and retirement on update.
The cache test passes AddressSanitizer/UndefinedBehaviorSanitizer. The background
color-only depth correction remains enabled. Hardware resource markers and
operator near/far, stationary/moving comparisons are required before acceptance.

Candidate renderer ELF `8f3bd1ecf882e38d05282ca622ccc82fd90d145da9b78748c93859e016e21c8e`;
PRX `f3f5f55bae8d520461320b717e9d76a5c14b53fd66b6d286162ad01b8e5dd5d6`.
Engine SELF unchanged (`e6c8bcc7…`). `make all` passes (405-file publication
audit), and the mip-resource and background-depth markers are present in the ELF.
Previous background-fix run was externally closed at 11:07 UTC for this
replacement; no BigApp and four healthy services independently verified.
Deployment journal: lab `research/xash3d/phase7-studio-mips-deploy-20260909.jsonl`.
Nine-file deployment passed exact raw readback. New engine run
`20260909T110826606Z_PPSA99996_xash3d-engine_0x144763b8fb258`, renderer run
`20260909T110826667Z_PPSA99996_ps5-xash3d_0x144763efd9fc3`.
Hardware mip-resource markers are present for Studio textures; visual quality
and final teardown remain pending. No claim of reduced shimmer is made yet.

This first candidate failed before active-map rendering: serial 225 emitted
`REF_AGC_LIVE_TEXTURE_FAILURE cache_result=-2` (arena exhaustion), then
`PARKED retain_all_resources=true`. It is a failed run, not a visual validation.
At revision 963 the baseline occupied 52,698,624 bytes and the mip candidate
59,770,880: a measured 7,072,256-byte increase. Baseline final residency was
60,644,864 bytes; adding that increase exceeds the old 67,108,864-byte arena.
The revised candidate budgets 80 MiB in the same owned direct-memory pool, with
unchanged allocation/retirement/teardown mechanisms. Verify final residency and
remaining headroom in hardware telemetry. The budget should have been checked
before the first launch; allocation failure was correctly detected, not ignored.

80-MiB candidate renderer ELF
`0c80f45716a4d05b63fa88142e8126a473d7a55a94f2c1d491ac8c6f0774663f`, PRX
`7df4aa9a99985d622d1ce7f823c99e03888525071de8fb976166fc8c21425400`.
The native source-contract test's explicit arena expectation was updated from
64 to 80 MiB along with the allocation budget; the failed 64-MiB run remains
recorded above rather than being overwritten by the replacement candidate.

80-MiB run: engine `20260909T111118598Z_PPSA99996_xash3d-engine_0x1449e4702632b`,
renderer `20260909T111118658Z_PPSA99996_ps5-xash3d_0x1449e4a8db8ef`.
Build and `make all` pass, nine-file raw-readback deployment verified (lab
`research/xash3d/phase7-studio-mips80-deploy-20260909.jsonl`). Active map reached:
serial 225, revision 1010, 1,002 active textures, 67,717,120 resident bytes in
83,886,080 bytes (16,168,960 bytes headroom). Normal shading and runtime input
confirmed; Studio frames show four entities, 81 draws, 8,559 indices and changing
pose hashes. No structured renderer error observed through serial 1201.
Visual shimmer/edge quality and final teardown still await validation.

Operator visual feedback on the 80-MiB candidate: distant models/contours look
smoother and walking vibration is somewhat reduced, including when stepping
farther away. No body parts ever disappear. Residual vibration is most apparent
when the character walks facing the viewer; it is not noticeable to the operator
when walking away with its back visible. Other animations look very good.
This is partial visual acceptance of Studio minification, not elimination of
all aliasing or an MSAA/silhouette-AA claim. Do not classify the report as mesh
dropout. Texture shimmer versus discontinuous pose motion remains to be
discriminated for the front-facing walk; final teardown is still pending.

### Studio walking motion interpolation candidate (2026-09-09)

Operator clarifies that face/clothing details are not vibrating; walking itself
looks unnatural and not fluid, as though animation runs at a different speed
from the game. They cannot distinguish discrete position jumps. This is an
observation/hypothesis, not proof of an incorrect animation rate.

Source comparison against pinned upstream `ref/gl/gl_studio.c` establishes:
our frame estimate already uses the same normalized network frame plus elapsed
client time × entity framerate × sequence fps. However the AGC adapter inherited
the null renderer's empty `R_StudioLerpMovement` callback and omitted the
renderer-owned MOVETYPE_STEP transform interpolation. Upstream `cl_frame.c`
delegates this to the renderer unless ENGINE_COMPUTE_STUDIO_LERP moves ownership
into the engine (which then calls that callback).

The candidate implements the callback and the renderer-owned fallback before
world-space bone assembly, guarded by the engine feature to avoid double
interpolation. It interpolates previous/current origin and quaternion angles
using upstream timing, including the one-second stale-update cutoff. It does
not alter animation FPS, client clock, game speed, filters or the background fix.
Host tests check fractional progress, equal/stale timestamps, extrapolation and
invalid-time fallback. `REF_AGC_STUDIO_MOVEMENT` samples owner, timestamps,
fraction and raw/rendered origins to establish actual hardware use. Controller
interpolation and sequence crossfades remain separate unimplemented work.
This candidate still requires hardware and operator validation.

Candidate renderer ELF `e34b41518db3b532f600644fb9ccec7c8c00145c42620c622c6ba7e68bdadf4c`;
PRX `5768afe3aba682465ea401398cc07b4dc5ae98de0f3e71326a9ff1cb2205c055`.
Engine SELF unchanged (`e6c8bcc7…`). Build and `make all` pass; movement marker
verified inside the ELF. Prior minification run externally closed at 11:23 UTC,
with no BigApp and four healthy services confirmed. Canonical deployment journal:
lab `research/xash3d/phase7-studio-lerp-deploy-20260909.jsonl`.

Run engine `20260909T112410646Z_PPSA99996_xash3d-engine_0x14552080e37de`, renderer
`20260909T112410705Z_PPSA99996_ps5-xash3d_0x145520b966fd9`. Active-map Studio
draws and changing poses observed; movement marker reports `owner=renderer` with
varying timestamp fractions. Early sampled origins are equal while the sampled
NPC is stationary, so those samples alone do not prove smoother translation.
Operator now confirms: "ahora avanza fluidamente OK". Walking translation is
visually accepted on this candidate. The missing STEP movement interpolation,
not a changed game/sequence playback rate, was the defect isolated by this
comparison. Mip filtering and the background-depth fix are retained unchanged.
This does not accept all Studio features: controller interpolation, sequence
crossfades, lighting/chrome and broader regression still require their own work.
End-of-run ownership/teardown validation and PR integration remain pending.

### Integration closure and newly reported limits

The resource closure rerun uses `XASH_GATE_SECONDS=180`, `XASH_GATE_FROM_MAP=1`,
`XASH_SAMPLING_PROBE=0`: normal rendering and natural engine quit after three
active-map minutes. The touchpad QA override is not present in this engine build.
SELF `48395ac510aa1fb1acf2216962005c81a89a7aa50e774e75551429e843809854`;
renderer ELF `06f4900beebe7627d507d06cc5a2fcd94367604d7e26a03653f2a94ffd35ebd8`,
PRX `6f4d4326403d62e71296b94aa8a0a3a23f7ca8878ea33801b015081df91eafc3`.
Engine run `20260909T113010025Z_PPSA99996_xash3d-engine_0x145a5b445db42`;
renderer run `20260909T113010080Z_PPSA99996_ps5-xash3d_0x145a5b7bceb85`.

New operator reports must not be hidden by closing the accepted graphics work:

- No audio: these client/graphics builds explicitly use `XASH_AUDIO=0`, linking
  the sound stub. Phase 5 SceAudioOut evidence is not acceptance of this live
  client mix. Enable `XASH_AUDIO=1` in a separate integration run and validate
  real map sounds, mix/ring telemetry and exact audio teardown. Silence is
  expected for this build configuration, not the intended final product.
- The chapter title appears over a black rectangle. `CL_DrawCharacter` requests
  the font's render mode through `GL_SetRenderMode`; that callback remains a
  null-renderer stub, while `RefAgcDrawStretchPic` hardcodes kRenderTransTexture.
  The missing blend-state propagation is a concrete suspect. Preserve requested
  2D modes, test additive versus alpha/opaque ordering, and validate the chapter
  title and MainUI on hardware before closing this defect. No transparency fix
  is claimed by the current integration.

The initial integration closure run above naturally completed 10,995 frames,
reclaimed nine resources and emitted exact renderer and engine teardown with
zero errors. Independent post-run status found no BigApp and four healthy
services. Its validator rejected stale texture-summary metadata: the new arena
was 80 MiB but the summary still labeled descriptors only bilinear and the
validator required 64 MiB. Neither logs nor that failed result were rewritten.
The code now emits the mixed descriptor family, and the validator accepts only
the explicit legacy/new budget-family pairs and also bounds peak residency by
arena size. Host tests preserve the legacy case, accept the new case and reject
an invalid budget. A fresh natural-exit run is required for the corrected marker.

### Final resource acceptance (2026-09-09)

That fresh run passed the paired validator: engine
`20260909T113637960Z_PPSA99996_xash3d-engine_0x1460005826042`, renderer
`20260909T113638013Z_PPSA99996_ps5-xash3d_0x14600097ae176` (53 ms apart).
Normal rendering, probe disabled, five-second menu and 180 active-map seconds
completed 10,997 frames, with 18 brush entities, 10,770 Studio pose changes,
164/164 world textures resolved, zero errors and nine exact resource reclaims.
Peak texture residency was 67,717,120 of 83,886,080 bytes. Both logs have clean
BYEs; all five PRXs unload, ScePad closes and terminates its owned user service,
and the engine root reports mapped=0, allocated=0, live_bytes=0, ownership=exact.
Post-run status independently found no BigApp and four healthy services.

Artifact SHA-256:

- Engine ELF: `878232f31299066486c1e3b4d8678c3f20d54a286bad2f7acc1e2a65f9724de2`
- SELF: `48395ac510aa1fb1acf2216962005c81a89a7aa50e774e75551429e843809854`
- Renderer ELF: `dbda4c6f5b9ae7030269d316e50ba3379a318fdbbe1d8f60db4e803de968044d`
- Renderer PRX: `192ec1ffd401720ecc6f108c58f5844b00c146a92c9ddb3ca87f856a4bd4b9d2`
- Engine JSON: `15599edfa0c9e1efec391e38e63c06633cc635f07cb00de10f8e34586d411632`
- Renderer JSON: `9da1a6959ba1e9755a0e483c5b2d156448ca4f0a5c435f6af5caf67218d6d556`

The accepted validator requires live lightmaps, 2D, menu, brush and Studio,
plus the existing exact engine/HLSdk and BSP/Studio bundle identities.
An initial invocation incorrectly added the special-surfaces requirement;
it correctly rejected this scene's zero sky/turbulent draws. Removing that
inapplicable requirement does not accept new sky/water evidence or replace
the earlier dedicated gate. Raw logs and validator contracts remain unchanged.
Canonical nine-file deployment used exact raw FTP SHA-256 verification.
No additional operator QA, Remote Play or recording was needed for this
resource-only closure. Audio and title transparency remain pending as above.
