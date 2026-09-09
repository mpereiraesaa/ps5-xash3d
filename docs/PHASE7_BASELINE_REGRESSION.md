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
