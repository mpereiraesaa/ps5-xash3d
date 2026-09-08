# Dynamic filesystem module — Phase 6 gate 2

Phase 6 gate 2 converts only `filesystem_stdio` from the static Xash module
table to an application-owned PRX. The HLSDK server remains statically linked,
so the checkpoint is independently bootable and the next module boundary can
be attempted without combining failures.

**Status: passed on PS5 FW 12.02 on 2026-09-08.** Accepted run
`20260908T071044664Z_PPSA99996_xash3d-engine_0xe8e95e4c0974` loaded the PRX
through the ordinary `COM_LoadLibrary` path, mounted the complete private
4,823-entry tree, exercised listing, mixed-case lookup and a large read,
spawned `c1a0`, ran for 15 seconds, stopped the module explicitly and released
all module and engine ownership before a clean gap-free BYE.

## Build boundary

The gate is reproduced with:

```sh
git submodule update --init third_party/xash3d-fwgs third_party/hlsdk-portable
XASH_GAME_DATA=/private/path/half-life \
  PS5LOG_DEV_CONF=/private/path/dev.conf \
  XASH_REF=none XASH_GATE_SECONDS=15 \
  make engine-filesystem-prx-native-release
```

The private game tree is copied only into the ignored package directory. It is
never committed. The builder produces `sce_module/filesystem_stdio.prx`, omits
the filesystem from the static table, preserves the static server and rejects
application-owned unresolved imports. The accepted PRX has 64 audited dynamic
imports and zero banned imports; the host has 168 and zero banned imports.

The module publishes eight bounded `PRXDESC1` exports: `GetFSAPI`,
`CreateInterface`, three state queries, the started state and explicit
`module_start`/`module_stop` entries. FW 12.02 does not execute those entries
automatically for this loading path, so the `COM_*` owner calls start after a
validated load and stop before unload. Start failure rolls back; failed stop or
unload retains the node for a later shutdown retry.

## Allocator boundary

`fs_api_t.LoadFileMalloc` promises a buffer that the caller may release with
the standard C allocator. That is a cross-module ABI, not module-private
storage. Consequently the filesystem PRX deliberately uses the process libc
for `malloc`/`free`; allocations requested through the filesystem engine
callbacks still use the engine's guarded 128 MiB direct-memory arena.

A rejected diagnostic run,
`20260908T065949157Z_PPSA99996_xash3d-engine_0xe850bfc43c41`, wrapped the
PRX's standard allocator in a private 16 MiB arena. The filesystem workload
itself passed, but `PM_InitTextureTypes` loaded `sound/materials.txt` through
`LoadFileMalloc` and the host's `COM_FreeFile` reached libc with a pointer from
the foreign arena. libc raised `SIGABRT`, and the symbolized stack contained
`PM_InitTextureTypes`, `_Mem_Alloc` and the allocator wrappers. The accepted
design records `allocator_contract=libc-shared`; the validator rejects any
other contract.

## Hardware workload

The workload runs from the PRX after `FS_AddGameHierarchy`, when the `valve`
search path is live. Acceptance requires all of the following in one run:

- dynamic load from `/app0/sce_module/filesystem_stdio.prx`, four validated
  mappings, eight exports and explicit start result zero;
- `.dirindex` count exactly 4,823 and zero refused directory listings;
- `FS_Search("gfx/*", true, true)` returning at least one result (22 in the
  accepted run);
- mixed-case `GfX/PaLeTtE.LmP` resolving to exactly 768 bytes with non-zero
  FNV-1a hash `7e1b360cd412cc44`;
- `maps/c1a0.bsp` reading 2,546,336 bytes with non-zero FNV-1a hash
  `d690c3a9f5cb01d2`;
- the static server loading Half-Life and spawning `c1a0`;
- state still valid immediately before shutdown, `module_stop=0`, unload
  result zero, no active dynamic modules and exact ownership;
- normal engine arena cleanup, zero allocation/guard failures and a clean BYE.

Validate an immutable logger manifest with:

```sh
python3 tools/validate_engine_boot_evidence.py RUN.json \
  --engine-commit 9aa39ad --hlsdk-commit e277ffa --map c1a0 \
  --filesystem-prx-gate
```

## Immutable evidence

- Accepted run:
  `20260908T071044664Z_PPSA99996_xash3d-engine_0xe8e95e4c0974`
- Transcript / manifest SHA-256:
  `808cc9a79c3892829f38f8865b9405d055a31c7aff37aa3571db14fdcdc09efb` /
  `26752b034280ed22d99bc3112d2407e939729b85cc72df90e45f2962f338bf45`
- Host ELF / signed fSELF SHA-256:
  `0bdba330bbbe58f940f166fc9b2980fe21457ba8b1d7474b35b6ecda26a1d25d` /
  `2e1f31f70403661c4f0e9a7e5f0d408816e5800c5f9c2169d790c831b3260861`
- Filesystem PRX ELF / signed fSELF SHA-256:
  `4a6f0d34200bad5892f0af3d2d194b31f930834d9178d11f7336391084b3588d` /
  `888e0e73e6a228a9277600a009facb26933929752688b36633aa2f5c3f54740c`
- 30 structured records, 41 raw console lines, no sequence gaps or oversized
  lines; engine/hlsdk commits `9aa39ad` / `e277ffa`.

## Next gate

Convert only the HLSDK server to a PRX while preserving this now-proven dynamic
filesystem checkpoint. Menu, client and `ref_agc` remain later independent
module boundaries.
