# Publication roadmap

> Items up to the 60,000-frame Gears soak were completed in `ps5-agc-gears`;
> this repository continues the list from the Phase 2 resource foundation on.
> The phase-level plan of the Xash3D port lives in the README table.

- [x] Record the initial hardware-accelerated triangle proof.
- [x] Create an isolated publication staging directory and positive allowlist.
- [x] Add a fail-closed publication auditor.
- [x] Inherit the original `ps5-agc-gears` repository history,
  GPL-3.0-or-later license and initial `PPSA99997` hardware evidence.
- [x] Confirm intended GitHub owner: `mpereiraesaa`.
- [x] Publish the repository publicly and confirm the development identity.
- [x] Derive the standalone app from a pinned public boilerplate revision.
- [x] Extract the AGC backend behind a small documented interface.
- [x] Extract and host-test the first backend core: two-surface planning,
  SetFlip/release-fence composition and exact GPU/VideoOut completion state.
- [x] Extract sanitized AGC ABI declarations and deterministic MRT0,
  pipeline-register and no-HTILE depth-state builders with host regressions.
- [x] Extract injected event polling, GPU-visible span validation and a
  fail-closed presentation/submission transaction without native test links.
- [x] Extract the direct-memory lifecycle, minimal platform ABI and AGC/driver
  link stubs; compile/link the stubs with the pinned native toolchain.
- [x] Vendor and host-test the project-owned `ps5log/1` client, compile its
  native `sceNet` backend and document the no-filesystem telemetry contract.
- [x] Extract the self-relative shader-header builder and repository-local
  shader embedding assembly; compile both with the native toolchain.
- [x] Extract the checked AGC command-writer boundary with exact direct/draw
  packet sizes, GPU-span validation and transactional render-wait adaptation.
- [x] Add a checked DCB submit descriptor/cache-flush boundary and compile its
  complete firmware-symbol binding with the pinned Prospero target.
- [x] Add the independently authored non-indexed gear mesh generator and host validation.
- [x] Add the three-draw MVP/quaternion/material scene builder and PS5-toolchain
  compatibility check.
- [x] Add and host-test two-buffer ownership tracking with exact 48-bit flip
  tokens and independent GPU/VideoOut completion.
- [x] Replace the three-draw prototype with a tested compositor that enforces
  the measured 27+3 DWORD cursor contract and 90-DWORD total.
- [x] Join scene timing and two-buffer ownership in a platform-neutral animation
  controller and pass a 10,000-frame host soak.
- [x] Add low-overhead frame telemetry with averages/maxima, deadline/error
  counters and a fixed 60-frame persistence cadence.
- [x] Classify imports and reverse-engineering needs by capability in
  `docs/CAPABILITY_MATRIX.md`.
- [x] Move the independently authored lit-Gears LLPC pipeline source without
  committing generated PAL ELF or ISA blobs.
- [x] Add and test the public `gfx1013` shader compiler/extractor with a
  reproducible manifest and no committed generated binaries.
- [x] Translate PAL metadata into sanitized AGC register templates during the
  standalone build and match the hardware-used contract exactly.
- [x] Add host unit tests and a fixture generated entirely from public sources.
- [x] Build without references to the parent laboratory.
- [x] Add CI for shell validation, host tests, generated-tree cleanliness and
  publication audit.
- [x] Replace release-time finite chunks with one persistent production
  `init / run-frame` state machine; expose drain only for host assertions and
  move soak duration to the external supervisor.
- [x] Validate a 300-frame moving double-buffer demo on FW 12.02 with exact
  GPU-fence and VideoOut-token ownership, intact guards and clean teardown.
- [x] Pipeline two frames in flight without weakening per-slot ownership; the
  FW 12.02 10K soak proved depth two and 59.94 fps sustained throughput.
- [x] Replace color DMA with a tested fullscreen-triangle render-target clear;
  pass green visual proof, final black 300-frame run and black 10K soak.
- [x] Add a standalone host `Makefile` for all reusable renderer tests and the
  publication audit.
- [x] Extract the four-draw frame command core into `gears_renderer`, consume
  it from the FW adapter and revalidate the exact refactor on hardware.
- [x] Pass 1,000-frame and 10,000-frame hardware soaks with exact requested,
  completed and verified counts, zero errors and clean teardown.
- [x] Pass a strict 60,000-frame standalone soak with two frames in flight,
  exact fences/tokens, intact guards, zero renderer errors and gap-free BYE.
- [x] Produce a deterministic release-candidate archive and `SHA256SUMS`.
- [x] Perform the fail-closed proprietary-material publication audit.
- [x] Replace copied numbered stages with an isolated Git-worktree development
  workflow that reuses the tested renderer contracts.
- [ ] Reproduce the archive from a fresh clone after this reconciliation PR
  commit and compare its checksum.
- [x] Publish the repository and acknowledge upstream projects precisely.
- [x] Assign PS5 Xash3D the dedicated local development identity `PPSA99996`;
  retain `PPSA99997` exclusively for the frozen Gears demo.
- [x] Publish the LLPC GFX1013 fork with pinned LLPC/LLVM revisions and a
  target-selection regression test.
- [x] Add the Phase 2 direct-memory resource pool with generation handles and
  exact-token deferred reclamation.
- [x] Add the two-slot per-frame transient ring, named GFX10.3 V#/T#/S#
  builders and explicit CPU-to-GPU cache transition.
- [x] Move camera constants, texture tables and a visible overlay into
  transient resources selected through generated pipeline permutations.
- [x] Add host contracts and a fail-closed structured-evidence validator for
  the resource-foundation path.
- [x] Pass and archive the Phase 2 60,000-frame hardware gate on FW 12.02.
- [x] Pass the Phase 3 bounded dynamic-lightmap 10,000-frame gate with
  alternating GPU-visible readbacks, stable surrounding bytes, intact guards,
  exact slot retirement and six reclaimed allocations.
- [x] Add deterministic mip chains and mip-aware trilinear/anisotropic
  descriptors; pass their host contracts and paired-lightmap 10,000-frame FW
  12.02 hardware gate with distinct sampler readbacks.
- [x] Add the `{` alpha-test permutation as a separate opaque/alpha draw pass;
  pass its compiled kill-bit contract and paired-state 10,000-frame FW 12.02
  hardware gate with distinct control/alpha readbacks.
- [x] Add sky as a separate unlit pass and pass its paired-state 10,000-frame
  hardware gate with 158 draws and distinct skip/pass readbacks.
- [x] Add checked resident/upload accounting with a gap-free per-frame digest
  and pass its 10,000-frame hardware gate.
- [x] Pass and archive the complete Phase 3 60,000-frame hardware gate with
  exact accounting, GPU-visible readbacks, intact guards and zero errors.
- [x] Define and host-test the complete Phase 4 semantic render-state space,
  its 99-entry permutation cache, exact dynamic blend/depth/cull translation
  and checked mid-frame viewport/scissor updates.
- [x] Generate, compile and manifest-check all eight explicit surface/masked
  fog/lightmap shader variants plus the orthographic 2D shader for `gfx1013`.
- [x] Generate the native shader catalog, host-test the bounded slot builder
  and produce a signed `PPSA99996` package that creates and links all nine
  variants without changing the frozen Phase 3 permutation table.
- [x] Bind the real opaque and masked-lightmap Phase 4 variants in BSP draw
  composition and pass the 10,000-frame FW 12.02 viewport/scissor hardware
  gate with full-frame restoration, exact ownership and zero errors.
- [x] Exercise alpha blend, additive, alpha test, depth-write on/off, cull
  front/back/none, fog on/off and lightmap on/off through actual draws and 18
  post-retirement framebuffer readbacks in a 10,000-frame FW 12.02 gate.
- [x] Add and prove the orthographic blended 2D HUD/console/menu/font path
  with a procedural atlas and per-frame transient geometry in a clean
  10,000-frame FW 12.02 gate.
- [x] Add real BSP lightstyle planes and face-local dynamic lights through the
  bounded Phase 3 atlas uploader; pass their four-mode, eight-readback
  10,000-frame FW 12.02 hardware gate.
- [x] Add camera-facing sprites plus alpha/additive particle batches from the
  existing per-slot transient ring; pass four modes, eight post-retirement
  readbacks and a clean 10,000-frame FW 12.02 hardware gate.
- [x] Add animated studio models with CPU skinning, per-model textures, chrome
  and additive modes; pass five modes, ten post-retirement readbacks and a
  clean 10,000-frame FW 12.02 hardware gate.
- [x] Add independently transformed real BSP brush entities with opaque, alpha
  and additive render modes; pass five modes, ten post-retirement readbacks and
  a clean 10,000-frame FW 12.02 hardware gate.
- [x] Add real world-tree PVS plus draw-AABB frustum culling; pass four modes,
  eight post-retirement readbacks and a clean 10,000-frame FW 12.02 gate.
- [x] Pass the complete integrated Phase 4 visual and continuous ownership
  soak with zero errors.
- [x] Boot the dedicated Xash3D engine with the static filesystem/server
  modules, resolve the complete 4,823-entry asset tree and load `c1a0` cleanly.
- [x] Add the native ScePad backend and pass movement, look, jump, crouch, use
  and fire on FW 12.02 with chronological batch reads and exact teardown.
- [x] Add SceAudioOut with an owned ring buffer, continuous 44.1-to-48 kHz
  resampling, underrun accounting, audible proof and exact shutdown.
- [x] Require pull requests on `main`; require the host CI check after this PR
  establishes its final check context.
