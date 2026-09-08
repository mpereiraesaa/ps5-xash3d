# Phase 6 final gate: `ref_agc.prx`

## Accepted boundary

This gate completes Phase 6 by replacing the static diagnostic renderer with
an application-owned `ref_agc.prx`. The module publishes Xash RefAPI version
18, receives the engine callback table, owns the accepted native AGC/VideoOut
runtime and releases it before the filesystem module is unloaded.

The engine workload and renderer proof are deliberately distinct and both are
required:

- Xash boots `c1a0`, initializes the dynamic filesystem, HLSDK server, MainUI,
  GoldSrc client and renderer modules, and invokes live renderer callbacks.
- The renderer runs the complete Phase 4 feature composition against the baked
  `c1a0e` proof scene for 600 frames. It proves real GPU submission,
  presentation, readback and ownership rather than treating a RefAPI call as
  equivalent to pixels on screen.

This is not a claim that every live `model_t` or `entity_t` emitted by the
engine is already translated into AGC draw lists. That gameplay-facing
translation, dynamic level changes and long playable soaks are Phase 7.

## Implementation

`xash/platform_ps5/ref_agc_module.c` wraps the pinned upstream null renderer as
an ABI-complete callback skeleton without modifying the submodule. The real
lifecycle and frame hooks start and join the native backend. The module records
`R_BeginFrame`, `GL_RenderFrame`, `R_RenderScene`, `R_EndFrame` and `R_NewMap`
activity and exports a narrow status surface to the host loader.

The host fails closed unless all of these conditions hold at unload:

- RefAPI version 18 and complete engine mask `63`;
- runtime state `5`, runtime result zero and teardown result zero;
- exactly 600 native frames, a nonzero aggregate frame hash and visible-pixel
  count;
- positive begin, scene, end and new-map callback counts, with begin/end
  balanced;
- native ownership reported as `fence+videoout`;
- server, menu, client, renderer and filesystem release in that order, leaving
  zero active modules.

The generated descriptor has 16 exports: `GetRefAPI` plus 15 lifecycle/status
entries. The ELF has 91 dynamic imports and zero imports denied by the project
audit. The reusable Phase 4 source, shaders and owned platform stubs are linked
into the PRX; no proprietary game or SDK material is committed.

## Reproducible build

The accepted proof uses the enriched `c1a0e` scene because its baked plan
contains the water, glass, brush, visibility, effects, Studio and 2D fixtures
required by the Phase 4 completion contract. The engine itself still loads
`c1a0` from the private game tree.

```sh
XASH_GAME_DATA=/private/path/half-life \
PS5LOG_DEV_CONF=/private/path/dev.conf \
XASH_GATE_SECONDS=20 \
BSP_INPUT=/private/path/valve/maps/c1a0e.bsp \
STUDIO_INPUT=/private/path/valve/models/sphere.mdl \
make engine-ref-agc-prx-native-release
```

The generated title remains `PPSA99996`. Deployment uses the transactional
bundle helper. Regular assets are staged, promoted and verified by full remote
SHA-256; SELF/fSELF verification uses the connection-local transformed-ELF
contract. No `PPSA99998` title is created or installed.

## Accepted FW 12.02 evidence

The two ps5log clients started 51 ms apart and are treated as one paired run:

- Engine: `20260908T191327933Z_PPSA99996_xash3d-engine_0x11059870e2628`
- Renderer: `20260908T191327984Z_PPSA99996_ps5-xash3d_0x110598a25cd2f`

Artifact SHA-256:

| Artifact | SHA-256 |
| --- | --- |
| Host ELF | `c00933748ce9dad152ae163b83205d6b042fe4516494928d3afbaff63792b310` |
| Host fSELF | `b27647e03263b72d3a9e32efcd6b4bce02cb9fdbade3626c0456dea8fb06b5b9` |
| `ref_agc` ELF | `4a1b4cb19c31ee765caade673c625dac19deba714634c1ecbe33404ded571a98` |
| `ref_agc.prx` fSELF | `23437e8a26141bfedb2f64abbb3a3bb0621019b9adbcab1ea25ad5c3b80c2156` |
| `map.ps5bsp` (`c1a0e`) | `d66be922584d7537e2dca7233293195d6ae383b22fc7959853537a75815c5cfa` |
| `model.ps5mdl` (`sphere`, sequence `fire`) | `d5b3a1f9b5c9035b02e678079b3586a5fe35987d55167dab27868050969b3e31` |

The engine observed 203,420 begin/end calls, 203,411 live scene calls and one
new-map call. The module exported frame hash `d8c9aadab3c82cdb` and 820,521
bright pixels. The renderer produced 600 combined frames, final GPU buffer
hashes `a9e62c5188ca6bf5` and `0044418de19349d8`, 807,578 bright pixels, zero
errors, exact fence/VideoOut tokens, six reclaimed resources and exact native
teardown. Both streams ended with their expected gap-free BYE.

Validate the immutable manifests with:

```sh
python3 tools/validate_ref_agc_prx_evidence.py ENGINE.json RENDERER.json \
  --engine-commit 9aa39ad --hlsdk-commit e277ffa \
  --bundle-sha256 d66be922584d7537e2dca7233293195d6ae383b22fc7959853537a75815c5cfa \
  --bundle-bytes 9971952 \
  --studio-sha256 d5b3a1f9b5c9035b02e678079b3586a5fe35987d55167dab27868050969b3e31 \
  --studio-bytes 93952
```

## Rejected diagnostics

Two earlier runs remain diagnostics and are not reclassified:

1. `20260908T190615...` used the smaller `c1a0` baked bundle and failed before
   GPU submit because it did not satisfy the complete Phase 4 scene plan.
2. `20260908T191000...` used `c1a0e`, but mapped the 600 integration frames to
   Phase 4 frame indices 0–599. Those indices selected control modes, so the
   final combined-composition check rejected the run cleanly.

The accepted build maps its bounded integration indices into the established
59,400–59,999 feature window while retaining the standalone 60,000-frame soak
as the durability authority. Neither rejected run crashed or leaked submitted
resources.
