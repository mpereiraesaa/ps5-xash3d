# Phase 6 dynamic server PRX gate

This gate moves the upstream `hlsdk-portable` server from the executable into
`/app0/sce_module/server.prx`. The already accepted dynamic
`filesystem_stdio.prx` remains enabled, so the rollback boundary after this
gate is one executable plus two independently owned application modules.

## Build boundary

```sh
XASH_GAME_DATA=/private/path/half-life \
  PS5LOG_DEV_CONF=/private/path/dev.conf \
  XASH_GATE_SECONDS=15 make engine-server-prx-native-release
```

`XASH_SERVER_PRX=1` requires `XASH_FILESYSTEM_PRX=1`. The build compiles the
server source set as PIC, derives the export list from the actual linked
objects, and packages the signed module beside the executable. For the
accepted build the descriptor contains 257 entries:

- `GiveFnptrsToDll`, `GetEntityAPI` and `GetEntityAPI2`;
- 248 entity factories present in the linked HLSDK objects (250 were scanned;
  two were not compiled into this target);
- two bounded ABI probes;
- state, export-count, `module_start` and `module_stop` lifecycle entries.

The executable keeps no static filesystem or server fallback in this build.
The three dynamic-symbol audits cover the host and both modules independently;
all three must contain zero banned imports.

## C++ initialization contract

Application-owned PRXs cannot assume that FW 12.02 has executed their C++
initializers when Xash receives the loaded handle. The generated descriptor
therefore references the linker-provided `__init_array_start/end` and
`__fini_array_start/end` ranges. Its idempotent `module_start` runs constructors
in forward order before publishing state 1; `module_stop` runs finalizers in
reverse order before clearing that state. This also remains correct if a later
loader revision invokes `module_start` automatically, because a second call is
a no-op.

This rule is not cosmetic. Rejected run
`20260908T081747518Z_PPSA99996_xash3d-engine_0xec9200150ba2` proved that all
six initial `CVarGetPointer` callbacks worked but the first
`CVarRegister(&build_commit)` received `name=NULL`. `build_commit` and
`build_branch` live in BSS and are populated by `_GLOBAL__sub_I_game.cpp`.
The server ELF contained two valid relocated `.init_array` entries, but the
module lifecycle had not called them. Running the initializer array corrected
the fault without changing HLSDK source or replacing any Prospero library.

## Engine ABI and ownership

Xash still resolves the ordinary GoldSrc API. A narrow
`GiveFnptrsToDll` trampoline calls the real PRX export and then records:

- the presence of `pfnCVarGetPointer`, `pfnCVarRegister` and `gpGlobals` as a
  three-bit mask (`7` required);
- successful calls from PRX code back into the engine for `sv_gravity` and
  `developer`.

The probe does not register a persistent cvar or leave a host pointer to
module-owned storage. The full `GameDLLInit` registration sequence and map
spawn then exercise the wider callback table through the unmodified engine
flow. On shutdown Xash calls `module_stop`, unloads exactly the server handle,
clears its saved trampoline target, and only then unloads the filesystem PRX.

## Accepted FW 12.02 evidence

- Run:
  `20260908T082646982Z_PPSA99996_xash3d-engine_0xed0f9a243abc`
- Host ELF / signed fSELF SHA-256:
  `10284d275fa5ec6cdbd194b9682d0b7ab5c813ebe69d86aceffc8a3a200478c5` /
  `53548f84c50942e49edeeee0ce2d1283db5fa3286a9c76d71f0070bb1b43488a`
- Server PRX ELF / signed fSELF SHA-256:
  `26eb2e10b966918692e378166307bb4ac3b52bc76f2a0dccc4cbe26266e889a5` /
  `c3aa4956510f9e638cedaa54178f5fb313a76eb7601336cc39180b22bad9295c`
- Transcript / manifest SHA-256:
  `69cb7dd0f0fb5dacfde1de0486c183da63b6b5a11643dcfbde2860b6a9bb5a3f` /
  `fe73667d6a764d5e5e363afcd2cd75b29232e3d2cc4c498e4ce7c1c6a4435428`
- Server load: positive handle, four segments, 257 descriptor entries,
  explicit start result zero and 251 engine exports.
- ABI: table mask 7 and both PRX-to-engine callback smokes passed.
- Workload: 4,823-entry filesystem index, 768-byte mixed-case palette read,
  2,546,336-byte `c1a0.bsp` read, `Spawn Server: c1a0`, graph load and four
  player server start.
- Runtime: bounded 15 seconds, zero structured errors and normal host result.
- Teardown: server stop/unload zero with one filesystem module still active;
  filesystem stop/unload zero with zero modules active; exact engine-memory
  teardown and gap-free `BYE reason=xash-engine-boot-complete`.

The immutable validator accepts this evidence only with both
`--filesystem-prx-gate` and `--server-prx-gate`. A launch return code, a module
handle, or `Dll loaded` without map spawn and exact unload is insufficient.

## Next rollback point

The next Phase 6 gate is `menu` as its own application PRX while retaining
this accepted executable/filesystem/server combination as the independently
bootable rollback point. `client` follows menu; `ref_agc` remains the final
engine module boundary.
