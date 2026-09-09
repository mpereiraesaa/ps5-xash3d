# Phase 7 fullscreen menu and c1a0 recovery

The bounded FW 12.02 regression passed on 2026-09-09. Native MainUI fills the
1920x1080 output, the engine queues `map c1a0`, and the map becomes visible
through AGC before exact teardown. This closes the recovery regression only.
Brush entities, Studio drawing, viewmodel, gameplay and release gates remain
open. The tested branch includes preliminary brush and Studio resource work;
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
independent live screenshot shows the c1a0 tram interior. No joystick input
was required for this bounded regression. It auto-quits after 25 seconds.
