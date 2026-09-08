# Hardware validation

Hardware claims in this repository refer to one PS5 running firmware 12.02.
They are not compatibility claims for other firmware or consoles. Run logs and
captures live in the private parent laboratory; this public boundary records
only sanitized identifiers, hashes, outcomes and known limitations.

## Dedicated-title identity gate

Before Phase 4, the port moved from the inherited Gears development identity
to its own local title, `PPSA99996` (`PS5 Xash3D`). The frozen Gears demo keeps
`PPSA99997`; both applications are installed side by side and are controlled
by separate exact-title launch/close helpers.

The dedicated-title smoke gate passed on FW 12.02:

- Run: `20260906T205728904Z_PPSA99996_ps5-xash3d_0x78de031d5d51`
- Native ELF SHA-256:
  `7aeb69f792de3dfd5080da43c93bd9c1b51f6fa3a5c956fd31cc1c479e9278d1`
- Signed fSELF SHA-256:
  `e62dec33f47c8bab7851dea9e75b90a2b9cca34f10f143e5a0d899d285c5face`
- Private bundle SHA-256/bytes:
  `7536b8a28be3b815f93b35f035f9f957952e379722657194e1ab15172f9604e1` /
  8,741,888
- Transcript/manifest SHA-256:
  `cf7369da2450be324baa08fe69558aefcfcb157025dabd86fb38a9140b5dc40c` /
  `d83526382b016b5a142d0add69b1145f0b1daab1ee1cb2e94ae3adba06446051`
- Observed title/application: `PPSA99996` / `ps5-xash3d`
- Completed sample: 1,440 frames at 59.94 fps, zero renderer errors and no
  presentation intervals over budget
- Runtime shape: 122 mip chains and 2,915/137/158 opaque/alpha/sky draws,
  matching the accepted Phase 3 final artifact
- Closure: the dedicated `PPSA99996` helper removed the exact BigApp, an
  independent status query observed no BigApp, and all four console services
  remained healthy

This was intentionally an identity/launch/close smoke gate, not a repetition
of the already accepted 60,000-frame renderer soak. The external exact-title
close ends the TCP stream without an application BYE, so this manifest is
expected to record a gap-free EOF with `bye=false`; it is not used as Phase 3
completion evidence.

## Phase 4 native binding and viewport/scissor gate

The first Phase 4 hardware checkpoint bound the generated GoldSrc pipeline
catalog in the real BSP draw path, selected opaque-lightmap key 68 and
masked-lightmap alpha-test key 71, and changed viewport plus scissor between
draws before restoring the full-frame state:

- Run: `20260906T220730780Z_PPSA99996_ps5-xash3d_0x7cb052db2ae7`
- Native ELF SHA-256:
  `a2e2a303aa428733c11128521b19c20b14cc1cf59efab54fad7307ab3eda9679`
- Signed fSELF SHA-256:
  `e36eb2fe00ff33e23f491c4c53d3744b9d6df801361b054dee3d851e1e65e1e4`
- Private bundle SHA-256/bytes:
  `7536b8a28be3b815f93b35f035f9f957952e379722657194e1ab15172f9604e1` /
  8,741,888
- Transcript SHA-256:
  `daee720296ea418ec0e9d887de0937400291b7351b24228d15763b2c03d68708`
- Requested/completed: 10,000/10,000; maximum frames in flight: two
- Pipeline catalog: 99 semantic entries, nine native shader variants and both
  required BSP keys observed
- Viewport/scissor: 1920×1080 full state, 1280×720 inset viewport, 1120×640
  nested scissor and full restoration observed in every sampled frame
- Presentation intervals over budget: 0; renderer errors: 0
- Fence and VideoOut tokens: exact; guards intact; six allocations reclaimed
- Closure: exact-title helper left no active BigApp and all console services
  healthy

A compositor-visible capture from the exact artifact showed the BSP world
inside the inset rectangle and the final overlay after the full-frame restore.
The fail-closed Phase 4 validator also required all Phase 3 resource,
lightmap, upload-accounting and alternating-readback invariants. Controller
connection and movement were observations, not acceptance conditions. This
gate closes actual opaque/masked binding and mid-frame viewport/scissor only;
it does not claim the remaining render-state matrix or later Phase 4 scene
features.

## Phase 4 complete render-state matrix gate

The next artifact selected nine deterministic GoldSrc cases through the real
BSP draw path: opaque, alpha, additive, alpha test, depth-write off, cull
front, cull back, fog and lightmap off. Each case remained selected for 300
frames and rendered into both backbuffers. The title captured each framebuffer
hash only after its GPU fence and exact VideoOut token retired:

- Run: `20260906T223113472Z_PPSA99996_ps5-xash3d_0x7dfb90d3b053`
- Native ELF SHA-256:
  `d914bcf26b5aa3e0ca17eb3c99a10cdb3abb929bf89f9817e96f7640f9baf2e6`
- Signed fSELF SHA-256:
  `31de1cf508c26f36217bb04aaa87140e191a71880a95e124a504d29c62441b9a`
- Private bundle SHA-256/bytes:
  `7536b8a28be3b815f93b35f035f9f957952e379722657194e1ab15172f9604e1` /
  8,741,888
- Transcript SHA-256:
  `d875d6793d92407e297daef313c7ad24ab84d5ada3abc0b15fd04f2381805ef3`
- Requested/completed: 10,000/10,000; structured records: 274
- State evidence: nine actual state keys and shader selections; 18 unique
  slot/case readbacks; every feature image distinct from the same-slot opaque
  control
- Renderer errors: 0; fence and VideoOut tokens: exact; guards intact; six
  allocations reclaimed; gap-free BYE
- Closure: exact `PPSA99996` helper eventually removed the parked title after
  its first external-verification window elapsed; independent status then
  observed no BigApp and all four console services healthy

Private captures from the exact artifact have SHA-256
`9268d8b18a2b1dc1333996308f9a9c6ed1be8251e77f703a5d3103bb7c15dcf8`
for the ordinary lightmapped scene and
`ac85ad1d8082bfafa9cf4b99e75f2a8ba0254c531572f2d2ddaf19b43bc05539`
for the fog-selected scene. This closes Phase 4's pipeline-state matrix, not
the later 2D, scene-object or visibility work.

## Phase 4 orthographic blended 2D gate

The next artifact exercised the compiled `screen_2d` shader through semantic
alpha key 129 and additive key 130. A procedural 128×32 RGBA8 atlas, projection
constants, vertex/index data and descriptors were rebuilt in the active
transient slot each frame. The visible layout combined a translucent console,
menu and HUD, 78 readable bitmap glyph quads and an additive crosshair over the
live BSP world:

- Run: `20260906T225115588Z_PPSA99996_ps5-xash3d_0x7f137394ac44`
- Native ELF SHA-256:
  `b7b2ef1e9cf4679bbe5edea37a8511aecdac3352252c7d48ffea0d6e70ac3dde`
- Signed fSELF SHA-256:
  `f391dbbae2f90a34f64a3593418be137a4efd5099651254724144bf1665404b2`
- Private bundle SHA-256/bytes:
  `7536b8a28be3b815f93b35f035f9f957952e379722657194e1ab15172f9604e1` /
  8,741,888
- Transcript/manifest SHA-256:
  `12c94237d1aa4fb5e762372549e7e803f2df7781468b862af5a70a513237ac39` /
  `254087254ae7358b02f5465fe1aeb00e281eccbbfdf6136cb547da35955f6db4`
- Requested/completed: 10,000/10,000; structured records: 224
- Draw shape: two 2D batches and 522 indices per frame; 4 HUD, 2 console, 3
  menu and 78 font quads
- Per-frame transient use: 47,312 bytes total, including 34,612 bytes for the
  deterministic 2D atlas/layout
- Renderer errors: 0; fence and VideoOut tokens: exact; guards intact; six
  allocations reclaimed; gap-free BYE
- Closure: the direct Chiaki stream was stopped by its exact process and the
  exact-title helper removed `PPSA99996`; independent status observed no
  BigApp and all four console services healthy

The first Remote Play frame arrived black during stream negotiation and was
rejected. The accepted RGB capture from the same parked artifact has SHA-256
`7102eadff45ee1b3c8598d02e5480fba1bc736494a0f80dfd7dbe46b936ee383`
and visibly shows all four 2D component classes over the map. This closes the
orthographic 2D gate only; lighting and the remaining Phase 4 scene/visibility
features are still open.

## Phase 4 BSP lightstyles and dynamic-light gate

The next artifact extended the compatible version-3 bundle with all original
BSP lightstyle sample planes. It selected wall face 203 (draw 379; styles
0/33/35), rebuilt its 11×16 atlas patch from real samples and alternated base,
animated lightstyle, face-local radial dynamic light and the combined result:

- Run: `20260906T233103794Z_PPSA99996_ps5-xash3d_0x813f7d9b54cf`
- Native ELF SHA-256:
  `7cf6d6b7c0e4ace01781de5f8c63f18b8a7be09b2b5113cdd0c1bf215f0f62dd`
- Signed fSELF SHA-256:
  `dd66e6c4659b8bc4453720c003c549683c884d40d9906c3b7e9859f6fff14506`
- Private enriched bundle SHA-256/bytes:
  `0e6396cf2dbec287c4e2bc28f90a90e8f5cb26b98f43ebcd539dba7d9c171105` /
  9,573,888
- Transcript/manifest SHA-256:
  `f1c69e8d1825275da6716aeff6f0620c516f8fb4e708a45b18f8e19cd00e620b` /
  `197c0086ac8e72e91ff01465c513a029d95c39e690e2d611231a35c12cd10060`
- Requested/completed: 10,000/10,000; structured records: 266
- BSP lighting source: 3,052 lightmapped faces, 734,229 sample bytes, 528
  styled faces and 1,084 style layers
- Upload path: two initial full-slot uploads, then a 704-byte bounded patch in
  a 61,484-byte aligned acquire span
- Readbacks: four modes × two slots, all taken after fence zero and exact
  VideoOut ownership; every same-slot mode hash was distinct
- Renderer errors: 0; guards intact; six allocations reclaimed; gap-free BYE
- Closure: exact `PPSA99996` helper left no BigApp and all four services
  healthy; the obsolete `PPSA99998` title remained absent

Four compositor-visible CLI-stream captures accompanied the exact run, with
SHA-256 values `6fbb3283006e563631d29f22c1e2e39fadb795b8cad6a90733f1baca70964890`,
`d08ac11fdfacb55e6aeeae6d2c375b9e219a627c673918fd1a2cdef5ade1e18d`,
`8dd5ba9ad6d416c17365b5255b5e4099c16f264f0119d67095cf0c12cea476b1`
and `145b5d9b1ee5b9bfa822f228ae21dc71cc89f77c7d76f31ef98995bde448f4ba`.
Chiaki reused the existing registered entry and its exact isolated PID was
closed after capture; no pairing, client-window control or focus assumption
was involved. This closes Phase 4 lighting only. Sprites/particles, studio
models, brush entities and visibility remain open.

## Phase 4 transient sprite/particle gate

The next artifact added a 64×32 procedural RGBA8 atlas plus camera-facing
transient geometry: one sprite quad, 24 source-alpha smoke quads and 48
additive spark quads. Four deterministic modes isolated control, sprite,
particles and their combined image:

- Run: `20260906T235831459Z_PPSA99996_ps5-xash3d_0x82bf1cd8fb89`
- Native ELF SHA-256:
  `b88df7b004495d828db7a594d1579a56fe4925578d384bef01b95b8ae5d63778`
- Signed fSELF SHA-256:
  `33e804f669a7acdddaf8a38a6a3f51ee6b0ae2d946bd0fc97a347596d33dcf2a`
- Private bundle SHA-256/bytes:
  `0e6396cf2dbec287c4e2bc28f90a90e8f5cb26b98f43ebcd539dba7d9c171105` /
  9,573,888
- Transcript/manifest SHA-256:
  `e6d77a34f5276f59c12ac987f67a7720394b88c2788ee06e72f9f6ec8b9d4a05` /
  `6df527c58ea91bd060f3c570eda910d383b40a2018b5cb17751d128256a35899`
- Requested/completed: 10,000/10,000; structured records: 284
- Draw/index modes: 0/0, 1/6, 2/432 and 3/438
- Transient effect allocation: 18,772 bytes per framebuffer slot and frame
- Readbacks: four modes × two slots after fence zero and exact VideoOut token;
  all feature/control and combined/isolated comparisons were distinct
- Renderer errors: 0; guards intact; six allocations reclaimed; gap-free BYE
- Closure: the isolated Chiaki PID and exact `PPSA99996` title were closed;
  no BigApp remained and all four console services were healthy

The accepted compositor-visible sprite, particles and combined captures have
SHA-256 values `da592df0f150849fe1008ab57115e0ff14c6d742e1a9fe06b09f06b73a2cb980`,
`d11be0a14328f34714aa380a112926a6e4b362a18992d38712aa5edda5155b75`
and `7034111a275c25f02e78e089ca4f4aa6c0853fa121b9281e6b3e19cc979ff730`.
The registered Chiaki entry was reused without pairing or opening its main
client. This closes Phase 4 sprites/particles only; studio models, brush
entities and PVS/frustum culling remain open. `PPSA99998` remains absent.

## Phase 4 animated Studio-model gate

The next artifact baked a privately owned GoldSrc Studio v10 model into a
checked runtime bundle, then animated and CPU-skinned its embedded seven-frame
`fire` sequence into each current transient-ring slot. Five deterministic modes
isolated control, textured, normal-generated chrome, additive and the combined
three-instance image:

- Run: `20260907T003611716Z_PPSA99996_ps5-xash3d_0x84cd5cd0ac8a`
- Native ELF SHA-256:
  `a78675524a21b2a7b2264e3b271a4b80954333b01cd82456ee4fda3da7af1e52`
- Signed fSELF SHA-256:
  `0e0614f13bef7a0121ac6bde5cde0480f4e1162e8c6c6e8bfd008df71cd4dace`
- Private BSP bundle SHA-256/bytes:
  `0e6396cf2dbec287c4e2bc28f90a90e8f5cb26b98f43ebcd539dba7d9c171105` /
  9,573,888
- Private Studio bundle SHA-256/bytes:
  `d5b3a1f9b5c9035b02e678079b3586a5fe35987d55167dab27868050969b3e31` /
  93,952
- Transcript/manifest SHA-256:
  `2cf010f8b95529265e9095efe2a4882e31965b3afaa459c02f829333d7acf03d` /
  `fe93f51167b551f47e82831d272513a7886a25564947f82e3b6c7d68791f6290`
- Requested/completed: 10,000/10,000; structured records: 286
- Bundle ABI: 8 bones, 7 frames at 33 fps, 134 vertices, 282 indices,
  4 draws, 4 embedded textures and one chrome material
- Draw/index modes: 0/0, 4/282, 4/282, 4/282 and 12/846
- Readbacks: five modes × two slots after fence zero and exact VideoOut token;
  all feature/control and combined/isolated comparisons were distinct
- Animation: first/final pose hashes differed; per-frame pose and skinned hashes
  were nonzero
- Renderer errors: 0; guards intact; six allocations reclaimed; gap-free BYE
- Closure: isolated Chiaki PID and exact `PPSA99996` title closed; no BigApp,
  all four services healthy, frozen Gears retained and `PPSA99998` absent

The compositor-visible combined capture has SHA-256
`5ec51fbc6a4efec1ec7620dcb24b608fccf61e6478ca73f0d3b537ce14fa65df`.
This closes Phase 4 Studio models only; brush entities and PVS/frustum culling
remain open.

## Phase 4 transformed brush-entity gate

The next artifact extended the BSP bundle with all 95 model records and 94
real brush-entity records. It selected entities 1/26/46 whose source
`rendermode` values are 0/2/5, then rendered their actual face ranges under
independent animated transforms in control, opaque, alpha, additive and
combined modes:

- Run: `20260907T010315223Z_PPSA99996_ps5-xash3d_0x86475c3277bb`
- Native ELF SHA-256:
  `2bb4e66983d4e4e015369fe21b44b7573f8f037e63d6eaf3fd816cd6912f98da`
- Signed fSELF SHA-256:
  `5cd37ec664b377d2136c0bcc111d6705cc97f4207290748913713b0b140184d5`
- Private BSP bundle SHA-256/bytes:
  `a7039ea765d860939bc140791c0cd3653a4c51c6348e497c7f45d13000b64afe` /
  9,588,992
- Transcript/manifest SHA-256:
  `77ae24def4cf11f8c52fedbde625ada6a98bd47013059c3ada334089bfb09bf5` /
  `7ebbdb420918ec40f31641f7bfa287fa60760fb641568654cd0de8ce6f8f22df`
- Requested/completed: 10,000/10,000; structured records: 286
- Per-instance draws/indices: 16/120, 6/36 and 6/24
- Mode totals: 0/0, 16/120, 6/36, 6/24 and 28/180
- Readbacks: five modes × two slots after fence zero and exact VideoOut token;
  all feature/control and combined/isolated comparisons were distinct
- Animation: first/final transform hashes differed
- Renderer errors: 0; guards intact; six allocations reclaimed; gap-free BYE
- Closure: exact `PPSA99996` title closed; no BigApp and all four services
  healthy; frozen Gears retained and `PPSA99998` absent

This closes Phase 4 brush entities only. PVS/frustum culling and the complete
combined Phase 4 soak remain open.

## Phase 3 final 60,000-frame gate

The complete ordered texture path passed its final structured soak on FW 12.02:

- Run: `20260906T182418688Z_PPSA99997_ps5-agc-gears_0x70824724af5d`
- Native ELF SHA-256:
  `64a8c4604bbbd659636ae68ba801b67f9cb91afdbce0104015225ad71bdf11dc`
- Signed fSELF SHA-256:
  `a01454dc626d35e6553e56cbe45c33d1da614d10eaa3db1a6b665347eff5f15c`
- Private bundle SHA-256/bytes:
  `7536b8a28be3b815f93b35f035f9f957952e379722657194e1ab15172f9604e1` /
  8,741,888
- Transcript/manifest SHA-256:
  `b69d5a9cd514d4ef94d3b706fc9005340f054add33c1e4ff8f941649ff4e39f4` /
  `b977861bf0d6ebe08883176c0843ef52674a5a25cd2da40eab5d49e049c87c0d`
- Requested/completed: 60,000/60,000; structured records: 1,144
- Controller connected frames: 4,242; input dependency: none
- Mip chains and opaque/alpha/sky draws: 122 and 2,915/137/158
- Compiled runtime pipelines: 4; sampler: anisotropic 4:1
- Pool-resident/texture-payload bytes: 68,731,904 / 12,251,392
- Total transient/lightmap/upload bytes:
  762,000,000 / 19,471,872 / 781,471,872
- Upload frames: 2 full plus 59,998 bounded; sequence digest:
  `b4f0d5a0fa607141`
- Final alternating-lightmap GPU readbacks:
  `087617a14bd8a957` / `888273881ef5e384` (distinct)
- Presentation max/over-budget: 16.803418 ms / 0; renderer errors: 0
- Fence and VideoOut tokens: exact; guards intact; six allocations reclaimed
- Teardown: gap-free BYE at sequence 1,144, exact-title closure and two stable
  healthy four-service checks

The strict final validator recomputed the upload totals and required the final
sample, summary and completion digest to agree against the exact bundle. A
private compositor-visible Remote Play capture was taken while the exact final
artifact was still presenting; its SHA-256 is
`2a80b29d643f33fe3c0f85e4153ab1b346d9d408f4b0a95bdd6a322859a751e9`.
Chiaki reused its registered console entry through the direct CLI stream helper,
without opening the main client, pairing or re-registration. The DualSense was
connected during part of the run, but neither connection nor movement was a
success condition because the input path was unchanged. This gate closes Phase
3; entities, PVS, water, sprites/models, platform/audio and engine integration
remain later phases.

## Phase 3 resident/upload-accounting gate

The fifth ordered Phase 3 gate passed 10,000 frames on FW 12.02:

- Run: `20260906T181828532Z_PPSA99997_ps5-agc-gears_0x7030c07206e6`
- Native ELF SHA-256:
  `d5c1f7cb0d5f3bfc5731e7b323c05ebe422ee0cb12d50ce1cca3998b4fc95e4f`
- Signed fSELF SHA-256:
  `a62b2efaac7c4c76f6102ee55b43639e2cf7d99bb220864a4656ce3d7209045d`
- Private bundle SHA-256/bytes:
  `7536b8a28be3b815f93b35f035f9f957952e379722657194e1ab15172f9604e1` /
  8,741,888
- Transcript/manifest SHA-256:
  `068b11998fbe7d700be2545886dc646c81a5eb8eb80a4985adaa9824591286c7` /
  `ce04e57cc20b4538082a292dc417ce9eadd493654cf3b6ec2d7a59da035bfa31`
- Requested/completed: 10,000/10,000; controller dependency: none
- Pool-resident/texture-payload bytes: 68,731,904 / 12,251,392
- Total transient/lightmap/upload bytes:
  127,000,000 / 6,671,872 / 133,671,872
- Upload frames: 2 full plus 9,998 bounded; sequence digest:
  `9ee815de96b54c11`
- Mip chains and opaque/alpha/sky draws: 122 and 2,915/137/158
- Final alternating-lightmap GPU readbacks:
  `068f5c03c04419eb` / `6a7e900866458fb0` (distinct)
- Fence and VideoOut tokens: exact; guards intact; renderer errors: 0
- Teardown: gap-free BYE at sequence 226, six allocations reclaimed,
  exact-title closure and two stable healthy four-service checks

The strict validator recomputed every byte total, linked the terminal resource
readbacks to the two deterministic lightmap patterns and accepted the immutable
manifest against the exact bundle identity. The gate used no controller input.
The direct Chiaki CLI path was exercised without its client window, pairing or
re-registration; its post-teardown black frame was rejected rather than
misclassified as visual evidence. The authoritative visual capture is reserved
for the final artifact while its VideoOut lifecycle remains active.

An earlier attempt reached all 10,000 clean frames but was correctly rejected
with `parked-retain`: an inherited success condition required a connected
controller even though input code was unchanged. The accepted artifact makes
this boundary explicit with `input_gate=not-repeated` and
`input_dependency=none`; pad-read failures are still fatal and connection
counts remain observable. This gate proves consolidated texture accounting;
the separate final section above records the later 60,000-frame closure.

## Phase 3 sky-pass gate

The fourth ordered Phase 3 gate passed 10,000 frames on FW 12.02:

- Run: `20260906T165427904Z_PPSA99997_ps5-agc-gears_0x6b9b27deac05`
- Native ELF SHA-256:
  `d3669c1d4b1be1c6dc14a55478ba951bf39364fae6bac96ea07db78e53dba670`
- Signed fSELF SHA-256:
  `037d34a8eb379b8b6f821ceb663427055d1e98fa77882176622ba356f49ab1f6`
- Private bundle SHA-256/bytes:
  `7536b8a28be3b815f93b35f035f9f957952e379722657194e1ab15172f9604e1` /
  8,741,888
- Transcript/manifest SHA-256:
  `30ede3404d9588b3fbf4be74e9787739143255391eeee4b727f4157f7361136c` /
  `d96c761554bc35cb93006e1147b6b04784f392244062c50f4aff296c0814dc88`
- Requested/completed/connected: 10,000/10,000/10,000
- Sky textures/draws: 1 / 158; compiled runtime pipelines: 4
- Final skip-control/sky-pass GPU readbacks:
  `8c9d51a9222a6ce4` / `67cfb3455c938c2e` (distinct)
- Paired final lightmap slots: equal, pattern 1
- Fence and VideoOut tokens: exact; guards intact; renderer errors: 0
- Teardown: gap-free BYE at sequence 224, six allocations reclaimed,
  exact-title closure and healthy four-service state

The strict validator accepted the immutable manifest and exact private bundle
identity. A private capture shows textured geometry beneath the dedicated sky
pass; its SHA-256 is
`199a6b2c8d1d1c901934db52516989dec797f13e20d809fa79d14bb956033be3`.
The input implementation was unchanged; the operator moved the existing camera
to a sky-visible view. Chiaki reused the registered console through the direct
CLI stream path without pairing or re-registration. This gate proves the
separate sky draw class and pass ordering, not the pending consolidated
accounting or final 60,000-frame gates.

## Phase 3 alpha-test gate

The third ordered Phase 3 gate passed 10,000 frames on FW 12.02:

- Run: `20260906T160249452Z_PPSA99997_ps5-agc-gears_0x68c9c058f710`
- Native ELF SHA-256:
  `129f0dbb7074849b3f7fd661e3d8ce2ddfc5caeaa19dbe7718c23c6c49b6c04e`
- Signed fSELF SHA-256:
  `57b3a39fde9b57bb96ec97ed9af5ee0fee2b706ae58c6a79eb112f412c2f1b16`
- Private bundle SHA-256/bytes:
  `05a2f8ecc0b21df1e0ad3f5a159f7f20ae847c8ef252245959561c42f6e3fe52` /
  10,121,728
- Transcript/manifest SHA-256:
  `1ab9e23155eb53abf75994401f9a130ad0b41c141f448f65907c76ebbe3408ce` /
  `b8080f7a2e3b68fde8b4b4fb0e88460f7a235c13caf0b13d143163c1c142b665`
- Requested/completed/connected: 10,000/10,000/10,000
- Alpha textures/draws and opaque draws: 1 / 42 / 3,569
- Compiled opaque/alpha `DB_SHADER_CONTROL`: `0x00000810` / `0x00000850`
- Final opaque-control/alpha-test GPU readbacks:
  `716cc2cc82d8015a` / `221c46ffdbcf3e1c` (distinct)
- Paired final lightmap slots: equal, pattern 1
- Resident/total uploaded bytes: 76,383,232 / 173,632,448
- Fence and VideoOut tokens: exact; guards intact; renderer errors: 0
- Teardown: gap-free BYE at sequence 224, six allocations reclaimed,
  exact-title closure and two stable healthy four-service checks

The strict validator accepted the immutable manifest and exact private bundle
identity. A private capture from the already-running registered Chiaki session
shows the cutout surface with geometry behind it; its SHA-256 is
`6c050673f69e58f1113dcbf99ecd9cf6aef3dec6c881a285dc396cb1b6f147f6`.
Neither Chiaki nor the DualSense movement gate was restarted. This gate proves
the separate `{` alpha-test draw class and compiled shader permutation, not the
pending sky or final 60,000-frame gates.

## Phase 3 mip/sampler gate

The second ordered Phase 3 gate passed 10,000 frames on FW 12.02:

- Run: `20260906T153443296Z_PPSA99997_ps5-agc-gears_0x67412ae3fe5e`
- Native ELF SHA-256:
  `bec19b3e50f762e86713cf386238d697e52389a371cc69418a66e7c78ed7b50f`
- Signed fSELF SHA-256:
  `7adfb55bfb287867f9d5d419262b9d1144330360867ab20d571b46344f0ea7ff`
- Private bundle SHA-256/bytes:
  `05a2f8ecc0b21df1e0ad3f5a159f7f20ae847c8ef252245959561c42f6e3fe52` /
  10,121,728
- Transcript/manifest SHA-256:
  `6d06e1fbd22cb2ad91e0f2e4904ae752675f281dc8387f97da4f1d772ba965db` /
  `8daa88b924288d72b454274e37c0efe569157870826f20ab2c718a0b40456044`
- Requested/completed/connected: 10,000/10,000/10,000
- Texture chains/levels/bytes: 164 / 5–9 / 7,842,816
- Layout/filter pair: smallest-to-base linear / trilinear and anisotropic 4:1
- Paired final lightmap slots: equal, pattern 1
- Final trilinear/anisotropic GPU readbacks:
  `53961843c05e93bf` / `404a458403011f6d` (distinct)
- Resident/total uploaded bytes: 76,383,232 / 173,632,448
- Fence and VideoOut tokens: exact; guards intact; renderer errors: 0
- Teardown: gap-free BYE at sequence 224, six allocations reclaimed,
  exact-title closure and two healthy four-service checks

The strict validator accepted the immutable manifest and exact private bundle
identity. A capture from the already-running registered Chiaki session shows a
correct textured corridor; its SHA-256 is
`e15f89725691d8fcc02e0482f75d5b1d596b7b6e7c603865c7d832499d35125b`.
The capture remained private and neither Chiaki nor the DualSense input gate was
restarted. This gate proves deterministic mips and the two sampler variants,
not the pending alpha-test, sky or final 60,000-frame gates.

## Phase 3 dynamic-lightmap gate

The first ordered Phase 3 gate passed 10,000 frames on FW 12.02:

- Run: `20260906T150442704Z_PPSA99997_ps5-agc-gears_0x659df0b5b957`
- Native ELF SHA-256:
  `a922c3d2fbb028cec80e27f17b9634d1e2317987eac507bf2f2e6b232a6133c6`
- Signed fSELF SHA-256:
  `b7b52e38580e3a22612c279632ad92c2b638c8004894c5df10490e5d183968aa`
- Transcript/manifest SHA-256:
  `e163c166e3ca5a733570c803b75decdbb4251b608907aff8b8a5126ae27b5ac9` /
  `db345dc5ff7be97a22d8975639f6b4f97b3b0535f20fb0d75f4a86150d60c9d5`
- Requested/completed/connected: 10,000/10,000/10,000
- Dynamic patch: 8x8 texels, 256 bytes per bounded update
- A/B patch hashes: `2590af3457808025` / `89d8b73bb162b925`
- Final GPU framebuffer hashes: `7d766580357827ce` / `e3691c96c36c021c`
- Resident/total uploaded bytes: 64,259,072 / 173,632,448
- Image slots/pool allocations reclaimed: 2/6
- Outside-patch bytes and all guards: stable/intact
- Pad reads, presentation-budget and renderer errors: 0/0/0
- Teardown: gap-free BYE at sequence 217, exact-title closure and four healthy
  payload services

The fail-closed validator accepted the immutable manifest against the exact
private bundle identity. A Remote Play screenshot confirms the map remains
visually correct; its SHA-256 is
`32cb65a640e6c319f979aab842916bb1274da1cf6a66126bfe12b842b46a33a6`.
See `BSP_TEXTURE_PATH_PHASE3.md` for the ownership and cache contract. This
gate proves only the first ordered Phase 3 slice, not the later mip, alpha-test,
sky or final 60,000-frame gates.

## Phase 2 resource-foundation status

Phase 2 passed its complete 60,000-frame FW 12.02 hardware gate:

- Run: `20260906T130036578Z_PPSA99997_ps5-agc-gears_0x5ed84765862b`
- Native ELF SHA-256:
  `1a1f33e840d919f090accce6d4a5593044058c0f32386a2986cd2715e64b050c`
- Signed fSELF SHA-256:
  `695a5db926fc36e4d246c866e2d47cb0de8bd55904d495c1bf646f16c211e756`
- Private `c1a0` bundle SHA-256/bytes:
  `ef9661dbfaad03bcefef4e07707ebc21cc8a13812a7e63bc8e58a408ae8ab42b` /
  7,056,384
- Transcript SHA-256:
  `8a7b8ce9aa03552f92c1717ff7bb4d836b616a8b399cf938951ac21f21e4c66e`
- Server manifest SHA-256:
  `2a74e4467910b3bfd8da268582925f508f1dc7b86285f5496e1a37b5f5ce2bcb`
- Requested/completed/connected: 60,000/60,000/60,000
- Resource pipelines/heap allocations: 2/4
- BSP textures/descriptor DWORDs: 164/3,936
- GPU fences: zero before reuse
- VideoOut tokens: exact; final token `72567767493216`
- Transient slots: both reusable after exact completion
- Persistent allocations reclaimed: 4
- Color/depth guards: intact
- Pad read, present-budget and renderer errors: 0/0/0
- Final readbacks: `a00b1153259458f4` and `5a5954a5f43f9ee8`
- Teardown: gap-free BYE at sequence 1,127, followed by exact-title BigApp
  closure and four healthy payload services
- Device span: approximately 1,001.120 seconds

`tools/validate_bsp_resource_evidence.py` accepted the immutable server
manifest against the exact private bundle identity. Two Remote Play captures
of the transient overlay have SHA-256 values
`41b72ff3eed6f8fee5f5558f2af0735b1b03cfdb9807f4a25dbee5e891ba64a5`
and `07dc2971b8fcfdc2570f03c80612393d110dfc9c23ad63216434fb3ee68bb1c6`;
their map region is byte-identical while the overlay green mean changes by
approximately 30.2 levels. The operator also confirmed the pulse live. The
representative full-frame capture SHA-256 is
`4554e6cc8b1d0a610016bcb96080ac46cc61246de3c46032e80ebe658ec959b6`.
Captures remain private because they include game material.

The gate reused the already proven noclip input path only for
connected/read-error continuity. It did not require another DualSense movement
or Remote Play handoff, and Chiaki used its existing registered console entry.
See `BSP_RESOURCE_FOUNDATION_PHASE2.md` for the exact resource contract.

## Phase 4 PVS/frustum gate

- Run: `20260907T013429215Z_PPSA99996_ps5-xash3d_0x87fbad4e6ed0`
- Native ELF SHA-256:
  `fe5bd0f54a700c828a0de215404191276d190d99c735b8ecff1f1912f69980e0`
- Signed fSELF SHA-256:
  `423a8a353c2779825f2fe34ff15e0c4eb49b4d3a3b324f59db13d2a5f6090259`
- Private `c1a0e` bundle SHA-256/bytes:
  `d66be922584d7537e2dca7233293195d6ae383b22fc7959853537a75815c5cfa` /
  9,971,952
- Transcript/server-manifest SHA-256:
  `3252fea371c41a8de03e287fa358dbd638ee406f81bb4f8d5549c80163e94d29` /
  `6ff1227399b3367c4308d205791545266389a969fa8c3e66269164641757a8ce`
- Requested/completed: 10,000/10,000
- World tree: 1,323 nodes, 683 leaves, 86-byte PVS rows
- Visibility references/draw bounds: 2,610/3,210
- Selected draws, control/PVS/frustum/combined: 1,952/646/414/362
- Post-retirement readbacks: 8, both slots and all four modes
- Maximum bright-pixel delta/tolerance: 22/64
- GPU fences/VideoOut tokens: zero/exact before readback and reuse
- Resource guards/reclaimed allocations/renderer errors: intact/6/0
- Teardown: gap-free BYE at sequence 284, exact `PPSA99996` closure, no
  remaining BigApp and four healthy payload services

The fail-closed validator accepted the immutable manifest against the exact
private bundle identity. The gate deliberately locked the camera, so
controller state was observed but not a success dependency. No Remote Play
capture was required: actual post-retirement framebuffer measurements bind the
visibility reduction to stable visible output. `PPSA99998` remained absent.

## Phase 4 final integrated gate

- Run: `20260907T020656141Z_PPSA99996_ps5-xash3d_0x89c0f978ef68`
- Native ELF SHA-256:
  `d5499ae773f72e99a2eb7082206a04cb7deb00e43d6bbd463d7ecceb6c685dee`
- Signed fSELF SHA-256:
  `8af678d50024aa09caeae82abc97101d9f4fd859a7f7ac11420e783461054de0`
- Private BSP bundle SHA-256/bytes:
  `d66be922584d7537e2dca7233293195d6ae383b22fc7959853537a75815c5cfa` /
  9,971,952
- Private Studio bundle SHA-256/bytes:
  `d5b3a1f9b5c9035b02e678079b3586a5fe35987d55167dab27868050969b3e31` /
  93,952
- Transcript/server-manifest SHA-256:
  `0bbccaee2e59eb8f516a29296300fb4f061fa2151aa22ceadf3c511a2b298f9f` /
  `a08dd9d7b851f75e8f22d875358cac76f933246a50821c38759fb2c9c16feebe`
- Requested/completed: 60,000/60,000 in one process
- Final combined draws: world 362, brush 69, Studio 12, effects 3, screen 2
- Final brush scene: five real submodels, including `func_water` entity 65
  (35 draws/312 indices) and `glass_med` entity 27 (6 draws/36 indices)
- Final dynamic work: 48 lit luxels, 704-byte atlas patch and 86,810 transient
  bytes inside each 131,072-byte retired slot
- Post-retirement framebuffer readbacks: 2, both slots
- GPU fences/VideoOut tokens: zero/exact before readback and reuse
- Resource guards/reclaimed allocations/renderer errors: intact/6/0
- Teardown: 2,613 gap-free records, dedicated BYE, exact `PPSA99996` closure,
  no remaining BigApp and four healthy payload services

The final validator accepted the immutable manifest against both exact private
bundle identities. A compositor-visible capture from this artifact shows the
map with source water/glass brush content, transient effects, animated Studio
instances and the blended 2D overlay; its SHA-256 is
`751d0fee9d54bb815acf3a5edc1ded8981cce0aca3f07b83ae4ff2344a8800a1`.
World work is filtered by real BSP PVS plus draw-AABB frustum tests; inline
brush submodels retain their independently transformed render path. Chiaki
used only its registered CLI entry and closed by exact PID. `PPSA99998`
remained absent. This gate closes Phase 4.

## Continuous production-runtime evidence

The production lifecycle has no frame limit and is closed by the PS5
**Close Game** action. Run
`20260905T152643467Z_PPSA99997_ps5-agc-gears_0x183d28b1c66c` used that
continuous architecture in the immediately preceding build for 25,560
completed frames. It emitted healthy heartbeats
through frame 25,200 with two frames in flight, exact retired fences/tokens,
intact guards and zero renderer errors. Transcript SHA-256:
`f618b9843799f9c4fe4da1c859694152b8c7e49ab6c554bdb067112210651feb`.

The stream ended at operator closure without BYE, as expected for system-level
termination. It is evidence for the continuous runtime and ownership
heartbeats, but not a strict application-teardown result. A second continuous
session completed 14,160 frames and emitted healthy heartbeats through 10,800;
its transcript SHA-256 is
`40b2a92b647a131f1a116cdf24ecc9ebf69075ccbe02045ca464ab11a02a2914`.

The strict finite runs below validate ordered application teardown, but predate
the continuous production lifecycle. Together these evidence sets cover the
current renderer path and the explicit teardown path without claiming that one
artifact exercised both policies.

Commit `7087602` changes the telemetry accounting and its fully retired-slot
failure path. Its native SELF builds and passes host contracts, but that exact
artifact has not yet received a hardware run. The evidence above therefore
validates the renderer and continuous lifecycle it retains, not binary identity
with current HEAD.

## Strict standalone 10,000-frame soak

- Run: `20260905T143629462Z_PPSA99997_ps5-agc-gears_0x157f6aa15ea6`
- Artifact SHA-256:
  `f69443913b520510d7493bc51a6ae458895612ab3a903a8459ea5a37d84ca175`
- Transcript SHA-256:
  `22d80e7258370185afbca5ba21cd52084840741fd1e0ec3495bce59608ad71f9`
- Requested/completed/verified: 10,000/10,000/10,000
- Maximum frames in flight: 2
- GPU fences: zero before reuse
- VideoOut tokens: exact
- Color/depth guards: intact
- Renderer errors: 0
- Color clear: render-target draw (`color_dma=false`)
- Depth clear: DMA (`depth_dma=true`)
- Opening identity: schema, transport, filesystem policy and boot token exact
- Teardown: complete, with gap-free BYE
- Device span: approximately 166.863 seconds

This run established the corrected strict contract at the original soak size.

## Strict standalone 60,000-frame soak

- Run: `20260905T144120445Z_PPSA99997_ps5-agc-gears_0x15c32a4befaa`
- Artifact SHA-256:
  `431d7753672ae3922dac28b5d685a49d16f09622d53fce3790ab9d9254da8a9e`
- Transcript SHA-256:
  `51954da00511cf81b27f3aa99c2be5b28ca7b3e55622333dfc68964f58d53360`
- Requested/completed/verified: 60,000/60,000/60,000
- Maximum frames in flight: 2
- GPU fences: zero before reuse
- VideoOut tokens: exact
- Color/depth guards: intact
- Renderer errors: 0
- Color clear: render-target draw (`color_dma=false`)
- Depth clear: DMA (`depth_dma=true`)
- Opening identity: schema, transport, filesystem policy and boot token exact
- Teardown: complete, with gap-free BYE at sequence 1,011
- Device span: approximately 1,001.025 seconds

This is the strict finite reference. It is six times longer than the original
10,000-frame gate and used one uninterrupted process, allocation set and
VideoOut lifecycle. It predates the continuous runtime now at HEAD.

## Classified incomplete finite run

Run `20260905T150602724Z_PPSA99997_ps5-agc-gears_0x171c47e22690` requested
60,000 finite frames but its transcript ended after 32,040 completed frames,
without BYE or a terminal error marker. The preceding records report zero
renderer errors and stable timing. It is classified as an unexplained external
termination/truncated transcript—not a completed soak and not evidence of a GPU
fault. Transcript SHA-256:
`44af0e01addf3fa287111059af0137daa91b8e25b5d6a0ddbec9a25f4c2d1736`.

## Historical 10,000-frame soak

- Run: `20260905T140957943Z_PPSA99997_ps5-agc-gears_0x140cddf522be`
- Artifact SHA-256:
  `584780499d5f6b9e63a01df03c30d9a932cd05ea76d52b24936c9ff4f89ce77a`
- Requested/completed/verified: 10,000/10,000/10,000
- Maximum frames in flight: 2
- GPU fences: zero before reuse
- VideoOut tokens: exact
- Color/depth guards: intact
- Renderer errors: 0
- Color clear: render-target draw (`color_dma=false`)
- Depth clear: DMA (`depth_dma=true`)
- Teardown: complete, with clean BYE
- Runtime: approximately 176.864 seconds

This run predates the correction that emitted the mandatory
`LOG_BOOT_MONOTONIC_NS` opening record. Its GPU, ownership and teardown markers
are internally complete, but it is not described as strict telemetry-contract
evidence.

## Strict 300-frame validation

- Run: `20260905T141618842Z_PPSA99997_ps5-agc-gears_0x14658d16f31f`
- Artifact SHA-256:
  `038280d88975f0395037cbe08fc9e82f8560ee2411a44b42249fc7545c5ba5af`
- Requested/completed/verified: 300/300/300
- Maximum frames in flight: 2
- GPU fences: zero before reuse
- VideoOut tokens: exact
- Color/depth guards: intact
- Renderer errors: 0
- Color clear: render-target draw (`color_dma=false`)
- Depth clear: DMA (`depth_dma=true`)
- Opening identity: schema, transport, filesystem policy and boot token exact
- Teardown: VideoOut close, direct-memory release and clean BYE

This shorter run first established the corrected opening contract. It is
retained as regression history; the later strict 10,000-frame run supersedes it.

## Phase 5 ScePad gate

- Run: `20260907T181827569Z_PPSA99996_xash3d-engine_0xbec4d1cc932e`
- fSELF SHA-256:
  `6681a8a822edf1114a5e9f32d01286b90442430a35a180295909d3ab8ca15d82`
- Linked ELF SHA-256:
  `46da56f13a7d17f0b7d2323e2cc5d0fa16cb0d40997c25f9e5c73e527a15500a`
- Transcript SHA-256:
  `6dd2db62d23b2aa33f0387bacf2c4b534e4e64317562c4489b5bb3a2b21d20da`
- Engine/hlsdk commits: `9aa39ad` / `e277ffa`
- Polls / chronological samples / maximum batch: 4,361 / 24,535 / 62
- Connected / disconnected / intercepted: 24,535 / 0 / 0
- Movement / look samples: 1,286 / 1,258
- Jump / crouch / use / fire edges: 1/1, 2/2, 2/2, 1/1
- Read errors: 0
- Pad close / owned UserService terminate: 0 / 0
- Completion: all six actions true, `ownership=exact`, `errors=0`, `pass=1`
- Transport: clean, gap-free BYE at sequence 58

This run proves the dedicated Phase 5 ScePad backend on FW 12.02. It processed
every record from each oldest-first batch rather than collapsing to the newest
state, neutralized controller-generation state and closed the pad plus the
UserService ownership it acquired. The matching validator was invoked with
`--pad-gate`; the full contract and mapping are in `SCEPAD_PHASE5.md`.

## Phase 5 SceAudioOut gate

- Run: `20260907T194413175Z_PPSA99996_xash3d-engine_0xc372db81ccc6`
- fSELF SHA-256:
  `febef3a565810dd18565a3dfc707506a2fbbad0dd1d55e077cf91a5f540b7f74`
- Linked ELF SHA-256:
  `f6db533ac53728e86768c03c0b1b08e033ce3514348f8ea69cf8a9d9b3b9884e`
- Transcript SHA-256:
  `f949a2d173b82c9415e3adb3f2c458947cf4600c98e254217d7b598c407a10bb`
- Engine/hlsdk commits: `9aa39ad` / `e277ffa`
- Port: system user `0xff`, type `0`, index `0`, handle `0x20000000`
- Acquisition: init 0, open `0x20000000`, volume 0 (flags `3`, eight `0x8000`)
- Source / output frames: 66,150 at 44.1 kHz / 72,192 at 48 kHz
- Blocks: 282 whole 256-frame blocks; terminal padding 193 frames
- Conversion: 71,999 resampled + 193 padding = 72,192, the exact 147/160 relation
- Source hash `0x9fd6b8c32bb54595` equals the generated pattern hash;
  output hash `0xfbcae52a8b451ae1`
- Ring: capacity 8,192 frames, prime 1,024, 8 wraps, high-water 8,192
- Silence carried through: 13,233 frames (13,230 deliberate)
- Underruns / Output errors / discarded / rebases: 0 / 0 / 0 / 0
- Teardown: one drain (rc 256), one close (rc 0), one join, `owner=worker`
- Completion: `ownership=exact`, `pass=1`, `XASH_EXIT result=0`
- Transport: clean, gap-free BYE (`xash-engine-boot-complete`)
- Operator confirmation: low tone, gap, higher tone heard in that order

This run proves the dedicated Phase 5 SceAudioOut backend on FW 12.02. It
converted the engine's 44.1 kHz mix rate to the port's 48 kHz with a continuous
147/160 resampler, submitted only whole grains, zero-filled solely the final
partial block and kept the handle inside the worker for the whole lifetime. The
matching validator was invoked with `--audio-gate`; the full contract, the two
defects the first hardware run exposed and the FW 12.02 facts that differ from
the FW 6.02 reference are in `SCEAUDIOOUT_PHASE5.md`.

Repeated with the identical artifact in run
`20260907T195320217Z_PPSA99996_xash3d-engine_0xc3f2392f8174` (transcript
`278602c1f84d7396e5a0e0f3ed5863a14b0d7bf5c1a95fe35c8ff4d93b4cd462`), which
reproduced every counter and both PCM hashes bit for bit and was confirmed
audible again. A rebuild from the merged tree yields the same ELF and fSELF
hashes, so the published code is the accepted artifact.

## Phase 5 direct-memory allocator gate

- Run: `20260907T212512180Z_PPSA99996_xash3d-engine_0xc8f58f777975`
- fSELF SHA-256:
  `1a77a5abc51a52f2f23d04ef5bed47edf89852e449eaa3a57a06290daa943552`
- Linked ELF SHA-256:
  `d396471dc8a18b82574ffdda16a4d123a85931e1d37806d0c78d862fc339e2b1`
- Transcript SHA-256:
  `a43462a7e46de35fee6764273ca3e7622375d2e79455aebff93ef3cb6c32166d`
- Engine/hlsdk commits: `9aa39ad` / `e277ffa`
- Root: 128 MiB fixed VA, 64 KiB alignment, type `0x0c`, protection
  `0xf2`, fixed flag `0x10`
- Root acquisition: reserve / allocate / map = 1 / 1 / 1, all rc 0
- Representative resources: command 2 MiB, buffer 4 MiB, texture 8 MiB,
  depth 4 MiB; combined hash `0xc4b367e53de116f7`
- Resource ownership: four unique generations, 4 retire / 4 reclaim, zero
  live/retiring GPU resources, guards intact
- Engine workload: 20,687 allocations, 1,091 reallocations, 35,632,245-byte
  peak; complete 4,823-entry index and `c1a0` loaded for 30 seconds
- Allocation / guard / stale / foreign errors: 0 / 0 / 0 / 0
- Process-lifetime CPU ownership: 8 blocks / 22,565 bytes, exactly reclaimed
- Root teardown: one unmap and one release, both rc 0; final arena empty
- Transport: 31 records, no gaps, clean `xash-engine-boot-complete` BYE

The matching validator was invoked with `--memory-gate`. A prior iteration
proved that successful non-fixed `MapDirectMemory` can return a VA different
from the reserved one; it was rejected and rolled back. A second iteration
identified the eight `_exit`-lifetime C++ objects. The accepted implementation
records and bulk-reclaims that root-owned class only after guards pass and all
GPU ownership has ended. The gate proves allocator semantics and representative
resource lifetime; real `ref_agc` binding remains Phase 6.

## Phase 5 threads and monotonic-time gate

- Run: `20260907T220548886Z_PPSA99996_xash3d-engine_0xcb2ce47a2f65`
- fSELF SHA-256:
  `cd691f19664e44cd8cd6cfb9b019f5b6794f7a8410470a77ba86aec11e95bdde`
- Linked ELF SHA-256:
  `3e22c9f8d686dee19f94a4780e7494ccc6e0e9312c98662b854ee4c4e7b75bbf`
- Transcript/manifest SHA-256:
  `45a5cb16d0f1fd2123db8075626c2007e7a01948609f14a1f31fc7a01146a50b`
- Engine/hlsdk commits: `9aa39ad` / `e277ffa`; map `c1a0`
- Worker ownership: create / join / detach = 2 / 1 / 1, all rc 0; two
  distinct workers completed before mutex destruction
- Mutex-protected counter: 32,768 / 32,768; mutex errors 0
- Monotonic clock: 8,192 reads, 8,191 advances, zero errors/regressions,
  minimum step 801 ns, observed span 7,221,259 ns
- Sleep surface: 16 samples for each API/duration pair; both `nanosleep` and
  `usleep` passed 1, 2, 5 and 10 ms with zero errors and zero early wakes
- Engine workload: complete 4,823-entry index and `c1a0` for 30 seconds;
  `Host_Main` result 0
- Inherited memory gate: 35,632,244-byte peak, final zero ownership and exact
  reserve/allocate/map/unmap/release with zero failures
- Transport: 37 structured records, no gaps, clean
  `BYE reason=xash-engine-boot-complete`

The validator was invoked with `--thread-time-gate`; it requires every marker,
the exact pthread lifecycle and counter, positive monotonic progress, all eight
sleep buckets, zero errors/early wakes, the normal engine exit and the matching
immutable manifest. The build's 171 imports contain 35 hardware-pass entries,
three fail-guarded entries and 133 exported-only entries; no banned import is
present. Temporary remote backups were deleted only after acceptance, leaving
the passing `PPSA99996` executable installed.

## Phase 5 GPU end-of-pipe and VideoOut timing gate

- Run: `20260907T225446311Z_PPSA99996_ps5-xash3d_0xcdd8ce3a668a`
- Native ELF SHA-256:
  `bfbbd5fd89404765e0d52992df4abd1a1699d0e4df6398824748522392740ec1`
- Signed fSELF SHA-256:
  `fdb489280c1bac1f2489f0449bbf8f914eaf7b4cb2f8436ea11d59abaa72e798`
- Private BSP / Studio bundle SHA-256:
  `d66be922584d7537e2dca7233293195d6ae383b22fc7959853537a75815c5cfa`
  / `d5b3a1f9b5c9035b02e678079b3586a5fe35987d55167dab27868050969b3e31`
- Transcript / manifest SHA-256:
  `6d987ea639085670e67e06d8c4eec99b53a698318555c7347c85bc9a4e922b97`
  / `74866e6be699bcf1253143a8180140785003d676aea9ded0ba20b306194d859a`
- Correlation: 60,000 consecutive in-memory frame/slot/token records; 101
  structured samples; zero sequence gaps or CPU-order errors
- GPU EOP clock: 60,000 selector-3 writes, 59,999 strict changes, zero
  regressions; raw range 22,580,929,665,192 to 22,681,573,336,968 ticks
- Submit-to-fence min/average/max: 899,600 / 16,823,795 / 96,444,947 ns
- Submit-to-flip min/average/max: 1,156,515 / 32,754,596 / 96,455,852 ns
- Observed fence-to-flip min/average/max: 9,542 / 15,930,800 /
  30,502,938 ns
- Renderer: complete Phase 4 scene, 60,000/60,000 frames, exact fences and
  VideoOut tokens, intact guards, zero errors and clean
  `gpu-flip-timing-soak-complete` BYE
- Closure: exact `PPSA99996` close, independently observed no BigApp and all
  four services healthy; no transactional deployment backup or staging file
  remained

The GPU counter is intentionally recorded as raw ticks: no frequency or
nanosecond conversion is claimed. The CPU submit-to-flip average includes two
frames of pipeline residence and is therefore about twice the roughly
16.81 ms interval between consecutive retirements. The gate replaces the old
deadline interpretation with these explicit, separately named measurements.

## Phase 5 project-owned libc-shim gate

- Run: `20260907T235551519Z_PPSA99996_xash3d-engine_0xd12e2a9238fb`
- Linked ELF SHA-256:
  `b87e61fc52230d2503290f92942fe2ab4b7d58730929bd30765aaaa926f957d0`
- Signed fSELF SHA-256:
  `14c7c9e13d668ed782a2d98a19ada2e21f2003a8d8e7dccf52085a291096e569`
- Transcript / manifest SHA-256:
  `f4f4c9ac51c122dc36f45e5645d75c37d3676992234279691e1a7ef3674c8961`
  / `fdc1aef7159312a3173e09114dc8dad7879a56e01bde74d242ffa23d728fe4b2`
- Engine/hlsdk commits: `9aa39ad` / `e277ffa`; map `c1a0`; 30 seconds
- Symbol boundary: `__assert`, `getpwuid` and `dladdr` absent from the dynamic
  undefined symbols and retained as local definitions in the full ELF table
- Runtime probes: exact assert message formatting with `ps5log` reporter and
  `noreturn` abort policy; uid `0xff` returned as fixed identity `ps5`;
  `dladdr` returned zero with a cleared result for the `argv[0]` fallback
- Engine workload: complete 4,823-entry index, `c1a0` loaded, `Host_Main`
  result 0, no refused listing or allocation failure
- Transport/closure: 30 structured records, 40 raw console lines, no gaps or
  errors, clean `BYE reason=xash-engine-boot-complete`; no BigApp or temporary
  deployment file remained

The immutable validator accepted the manifest with `--libc-shim-gate`. The
hardware probe intentionally does not trigger a fatal assertion; it verifies
the same formatter plus the reporter/termination policy embedded in the local
definition, while the host test exercises normal and truncating formatter
paths. Existing crash logging uses the kernel module-list APIs and therefore
does not rely on `dladdr`.

## Phase 6 application-owned PRX loader gate

- Accepted run:
  `20260908T054317837Z_PPSA99996_xash3d-engine_0xe423c3826406`
- Gate ELF / signed fSELF SHA-256:
  `1c8fd80e7cbcdadc4a03cb96449d1d49544c7b9741f229ede40255bb43034f6e` /
  `c67f1cb7f1bd9e966d9364dec9ad9388afb89ee0bb07ee3091443a0e45f85b8f`
- Probe ELF / signed fSELF SHA-256:
  `f9f276d47c2626d7d848523263e17ccc34fbf74eb5ef3ccf80fc2af55338eea0` /
  `e1a1591fcc2f06de915345f8b6ab17505dd6b78b67d28f65b0d383791d804f69`
- Transcript / manifest SHA-256:
  `ebbec4fb52731b11726da8e246c41bf9400c5a13103a16cc4ad7fef1e461bfe1` /
  `7d09166be6700f0f6f9076fc824fde63b48170ca5e2b39d0ac08be12d74f7c4c`
- Loader: positive handle, four validated mappings, six `PRXDESC1`
  exports; missing-symbol lookup returned NULL
- Calls: `19 + 23 = 42`, two calls around the PRX's
  `sceKernelUsleep` import, version `0x10000`, function-name round trip
- Startup observation: `auto_started=0`; explicit idempotent `module_start`
  returned zero and changed `started` to one
- Teardown: one successful `sceKernelStopUnloadModule`, active modules zero,
  ownership exact
- Engine regression in the same run: static filesystem/server loaded the full
  private asset tree, spawned `c1a0`, timed out after 15 seconds and exited
  cleanly; 31 records, 40 raw lines, no gaps or errors

The fail-closed engine validator accepted this manifest with `--prx-gate`.
Two earlier diagnostics established that FW 12.02 clears the 0x160 module-info
input size word on successful return and does not automatically mutate this
probe through its ELF `module_start` entry. Both facts are preserved in
`PRX_LOADER_PHASE6.md`; neither diagnostic is classified as acceptance.

Afterward the probe and all deployment transaction files were removed. Normal
hybrid-backend regression run
`20260908T054524368Z_PPSA99996_xash3d-engine_0xe441394ac877` packaged no
probe (`prx_gate=0`), loaded the static modules and `c1a0`, and closed cleanly.
Its transcript / manifest SHA-256 are
`9af22c4abc996bebf209c3d2c4af79607e7fdd84b150afba7744606f443061ca` /
`5dc161cf4cff639ab815b304b1ac0bdb73c6d729a20c71ef4be4ab2f429abcb4`;
production ELF / fSELF SHA-256 are
`15264acb49412810228151df0019efb85ae61448b25c75cc9dc5f3ce3917c3c8` /
`422bf298926dea76937e3f834f85fe584eb48ccc47157761a8c1893979600b69`.

## Phase 6 dynamic filesystem PRX gate

- Accepted run:
  `20260908T071044664Z_PPSA99996_xash3d-engine_0xe8e95e4c0974`
- Host ELF / signed fSELF SHA-256:
  `0bdba330bbbe58f940f166fc9b2980fe21457ba8b1d7474b35b6ecda26a1d25d` /
  `2e1f31f70403661c4f0e9a7e5f0d408816e5800c5f9c2169d790c831b3260861`
- Filesystem PRX ELF / signed fSELF SHA-256:
  `4a6f0d34200bad5892f0af3d2d194b31f930834d9178d11f7336391084b3588d` /
  `888e0e73e6a228a9277600a009facb26933929752688b36633aa2f5c3f54740c`
- Transcript / manifest SHA-256:
  `808cc9a79c3892829f38f8865b9405d055a31c7aff37aa3571db14fdcdc09efb` /
  `26752b034280ed22d99bc3112d2407e939729b85cc72df90e45f2962f338bf45`
- Dynamic boundary: four validated mappings, eight `PRXDESC1` exports,
  explicit start result zero and shared-libc `LoadFileMalloc` contract.
- Filesystem workload: 4,823 indexed entries, 22 `gfx/*` results, mixed-case
  `GfX/PaLeTtE.LmP` at 768 bytes and `maps/c1a0.bsp` at 2,546,336 bytes, with
  non-zero stable hashes and zero refused listings.
- Engine regression: static server loaded Half-Life, spawned `c1a0`, ran 15
  seconds and closed normally.
- Teardown: module state valid before shutdown, explicit stop result zero,
  unload result zero, no active modules, engine arena balanced and exact
  ownership; 30 records, 41 raw lines, no gaps or oversized lines.

The validator accepted the immutable manifest with `--filesystem-prx-gate`.
Rejected diagnostic run
`20260908T065949157Z_PPSA99996_xash3d-engine_0xe850bfc43c41` isolated an
invalid private-arena implementation of the cross-module `LoadFileMalloc`
contract; it passed the reads but aborted when host `COM_FreeFile` reached
libc. Full design and fault analysis are in `FILESYSTEM_PRX_PHASE6.md`.

## Timing interpretation

The historical deadline counter measured a frame from preparation until
retirement. With two frames in flight that interval spans pipeline depth and
therefore reports approximately frames minus one. It is not a dropped-frame
metric. HEAD replaces it with average/maximum intervals between consecutive
retirements and a 17 ms over-budget count. Historical command composition was
about 2.2 microseconds, GPU wait about 1.1 milliseconds and VideoOut wait about
15.56 milliseconds.

## Acceptance rule for later soaks

A later run supersedes the 60,000-frame strict reference only when its artifact hash is
known and its manifest proves matching title/app/boot identity, gap-free
`ps5log/1`, the exact requested frame count, two frames in flight, exact
fences/tokens, intact guards, zero renderer errors and ordered teardown through
BYE. A launch return code, elapsed timeout or visual observation alone is not a
soak result.
