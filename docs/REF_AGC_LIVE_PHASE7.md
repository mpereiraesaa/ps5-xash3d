# Phase 7 live RefAPI frame bridge

Phase 6 proved RefAPI 18 module loading and the independent Phase 4 AGC
backend. It deliberately did not claim that the backend consumed the engine's
live world, entities, camera or UI. Phase 7 begins by making that boundary
explicit and measurable.

## Producer contract

`ref_agc.prx` owns a `RefAgcLiveStore`. The engine thread stages visible
entities through `R_ClearScene` and `R_AddEntity`, then captures the view pass
through `GL_RenderFrame` and 2D commands through `R_Set2DMode`,
`R_DrawStretchPic` and `FillRGBA`. `R_EndFrame` publishes one coherent frame
under a short mutex.

The ordering is intentional. Xash calls `CL_EmitEntities` before
`V_PreRender` calls `R_BeginFrame`, so `R_BeginFrame` resets only the current
view and 2D list. It must not erase the already staged entity list. A host test
reproduces this exact order.

Published frames copy all transient values. They do not retain engine-owned
entity or view pointers. Each entity includes its transform, animation and
GoldSrc render state plus a bounded model name. The world snapshot includes a
map serial, model identity, geometry counts, bounds and the presence of light
and visibility data. The bridge is bounded to 2,048 entities and 4,096 2D
commands and records every rejected item.

`PS5_RefAgcTakeLiveFrame` is the internal consumer boundary for the native AGC
backend. At this checkpoint the Phase 4 backend still renders its baked proof
scene; consuming and translating the captured frame is the next gate, not a
completed claim here.

## Accepted FW 12.02 capture gate

The paired run began 54 ms apart:

- Engine: `20260908T201628524Z_PPSA99996_xash3d-engine_0x113c9c0b788f9`
- Renderer: `20260908T201628578Z_PPSA99996_ps5-xash3d_0x113c9c3e19917`

The engine loaded `c1a0`, published 844,006 frames and captured a valid view in
843,997 of them. The world snapshot reported 3,695 surfaces, map serial 1,
light/visibility-backed model data, a peak of 22 visible entities and a peak of
3 2D commands. Both bounded lists reported zero drops. The fixed unattended
camera produced a nonzero hash and zero changes, as expected without input.

The paired renderer retained the accepted 600-frame Phase 4 proof: GPU hashes
`a9e62c5188ca6bf5` and `0044418de19349d8`, 807,578 bright pixels, exact
fence/VideoOut ownership and zero errors. Server, menu, client, renderer and
filesystem unloaded with active counts 4, 3, 2, 1 and 0. Both streams were
gap-free and ended with their expected BYE records.

Artifact hashes:

| Artifact | SHA-256 |
| --- | --- |
| Engine ELF | `ee111da03f424f8cb3a4cc9f13a353e9b81265f19f9f6e8e127c43f8bc9a5768` |
| Engine fSELF | `46f73c143004a51c5a845031eba9cc22342a1b49ca30f811f35dc927b4267f34` |
| `ref_agc` ELF | `ae4615f2c3ad141b054066659873f2f5399f430c4a683c415398e3c9d991b3e9` |
| `ref_agc.prx` | `88d10643d28c2b99d7be9f60183f900ca5d7a8f5639a56678d1c0631b23feb7a` |
| Engine transcript | `3c256e988f30acc9dc466244ff3a818d12ca225acf61b26cf725cf44ef4381d1` |
| Renderer transcript | `00dfc5a68dbb0862a1a103bd1f541cad1da937f270e808929f787c25c089471a` |

`validate_engine_boot_evidence.py` accepts both the immutable 16-export Phase
6 module and the 26-export Phase 7 bridge. For the latter it additionally
requires positive live frame/view/map/world/entity/2D evidence, a nonzero view
hash, frame/end equality and zero drops. The paired validator then retains all
existing GPU, lifecycle and private-asset checks.
