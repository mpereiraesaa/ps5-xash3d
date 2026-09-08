# Phase 6 dynamic GoldSrc client PRX gate

This gate moves the pinned `hlsdk-portable` client DLL out of the executable
and into `/app0/sce_module/client.prx`. It retains the accepted dynamic
filesystem, server and MainUI modules unchanged. The resulting five-file
bundle is the rollback point for the independent `ref_agc` gate; this change
does not begin renderer integration.

## Build and module boundary

```sh
XASH_GAME_DATA=/private/path/half-life \
  PS5LOG_DEV_CONF=/private/path/dev.conf \
  XASH_GATE_SECONDS=15 make engine-client-prx-native-release
```

`XASH_CLIENT_PRX=1` is valid only in client mode with
`XASH_FILESYSTEM_PRX=1`, `XASH_SERVER_PRX=1` and `XASH_MENU_PRX=1`. The build
removes the HLSDK client object set from the host's static module table,
compiles it as PIC and packages it with the same explicit C++ initializer and
finalizer ownership used by `server.prx` and `menu.prx`. `ref_null` and
`ref_soft` remain static; `ref_agc` is deliberately deferred.

The descriptor contains 48 entries: the 42 GoldSrc client exports actually
defined by the pinned source set, two bounded engine-callback probes, module
state/export-count and `module_start`/`module_stop`. Eight names from the
upstream 50-name compatibility list are absent from this build and are not
invented. The accepted `client.elf` has 42 dynamic imports and zero denied
imports. The filesystem, server, menu and host audits independently report
64, 43, 46 and 175 imports, also with zero denied imports.

## ABI and live workload

The host resolves the ordinary client API without changing upstream headers
or the pinned submodule. Narrow trampolines surround the real exports and
record, but do not replace, these calls:

- `Initialize` with GoldSrc interface version 7;
- `HUD_Init`, `HUD_VidInit`, `HUD_Frame`, `HUD_Redraw` and `HUD_Shutdown`.

Acceptance requires six representative engine callbacks (`engine_mask=63`)
and four callbacks invoked from PRX code (`module_mask=15`). The two
non-mutating smokes read the `developer` cvar and the current game directory.
The gate then loads `c1a0`; descriptor presence or repeated `HUD_Frame` calls
without `HUD_VidInit`, successful redraw and real map/server proof are
insufficient.

The preceding MainUI gate intentionally stayed in the menu. This client gate
enters a map so that the client video lifecycle runs. Consequently MainUI's
API, init/shutdown and exact unload remain mandatory, while a visible menu
redraw is not required during this map-focused workload. The standalone menu
validator still requires visible redraw and remains unchanged as the UI proof.

## Accepted FW 12.02 evidence

- Run: `20260908T130114060Z_PPSA99996_xash3d-engine_0xfc0996a1effb`.
- Host ELF / signed fSELF SHA-256:
  `d461cdecc461f0b5472b082b2580b2748f1161e65aa66cba0b0b6c8e26a0d736` /
  `9d215b914097a007f5f8b6f69ab8f92481c8341e5090bb5ccc64e81adeb854ef`.
- Client ELF / signed PRX SHA-256:
  `70b54c8628eab934d1cef3d3cb0c2baa177a3a2ddde5e2cf01daddb81e045ba4` /
  `9600971dcc1dcf4b6e5d1f90b05b54bc3dabd4cfb50eb8a4a5f2b89a3abd321a`.
- Transcript / manifest SHA-256:
  `95ce0a78d96f4f12a72097553d47329a98d35bd4ebf3b40e252d6964e0bd2524` /
  `3e92238bad943b6dc824b6e4d2001ab4a83e8cbf31f8ab43ea4f393ab129a366`.
- Descriptor: 48 entries, 42 actual client exports, four mappings, explicit
  start result zero and state 1.
- ABI: interface version 7, engine mask 63, module mask 15 and both callback
  smokes passed.
- Lifecycle: one initialize, init, video init and shutdown; 4,916 frame calls
  and 4,907 successful HUD redraw calls.
- Workload: the 4,823-entry filesystem retained the 768-byte mixed-case
  palette and 2,546,336-byte map reads, the Half-Life game DLL loaded and
  `c1a0` spawned.
- Presentation: 4,800 non-black software frames; final framebuffer hash
  `3af6afa7ee47ec93`. This is execution evidence, not an AGC/TV claim.
- Teardown: server, menu, client and filesystem stopped/unloaded with active
  module counts 3, 2, 1 and 0. The 15-second run ended with result zero, 89
  structured records, 115 raw lines, no errors, gaps or oversized records and
  a clean BYE.

Validate the immutable manifest with `--mode client --filesystem-prx-gate
--menu-prx-gate --client-prx-gate`. The earlier diagnostic run
`20260908T125432929Z_PPSA99996_xash3d-engine_0xfbac31b2bacd` proved load, ABI,
5,747 frame callbacks and exact teardown but correctly failed because the
menu-only workload never invoked `HUD_VidInit` or `HUD_Redraw`; it is not
accepted evidence.

## Next rollback point

The executable plus `filesystem_stdio.prx`, `server.prx`, `menu.prx` and
`client.prx` is now the hardware-proven rollback bundle. Phase 6 next converts
only the renderer boundary to `ref_agc`, reusing the complete Phase 4 backend
and its direct-memory/fence/VideoOut ownership contracts.
