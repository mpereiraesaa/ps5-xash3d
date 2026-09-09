# Phase 7 fullscreen menu and c1a0 recovery

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
