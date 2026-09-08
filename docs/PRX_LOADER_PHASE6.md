# Application-owned PRX loader — Phase 6 gate 1

Phase 6 replaces Xash3D's static module graph incrementally. Its first gate is
the loader itself: the engine calls its ordinary `COM_LoadLibrary`,
`COM_GetProcAddress`, `COM_NameForFunction` and `COM_FreeLibrary` API against a
real application-owned PRX. The filesystem and server remain in the proven
static table, so this change is independently reversible and does not claim
that an engine module has already been converted.

**Status: passed on PS5 FW 12.02 on 2026-09-08.** Accepted run
`20260908T054317837Z_PPSA99996_xash3d-engine_0xe423c3826406` loaded the probe,
validated four mapped segments and six exports, resolved a missing symbol to
NULL, called code and read data, executed the PRX's `sceKernelUsleep` import,
round-tripped a function name and released the module exactly. The normal
engine then loaded the static filesystem/server, spawned `c1a0`, ran for 15
seconds and exited through a clean gap-free BYE.

## Architecture

`xash/platform_ps5/lib_ps5.c` is a hybrid replacement for Xash's
`lib_static.c`:

- an exact static-table match preserves `filesystem_stdio`, `server` and the
  client diagnostic modules until each has its own migration gate;
- any other safe basename maps to `/app0/sce_module/<name>.prx`; absolute paths
  are accepted only inside that same directory with a `.prx` suffix;
- dynamic handles are explicit list nodes, never tagged pointers, so a static
  export table cannot be mistaken for a loaded module;
- `sceKernelLoadStartModule`, `sceKernelGetModuleInfo` and
  `sceKernelStopUnloadModule` own the complete dynamic lifetime;
- shutdown retries every retained handle. A failed unload or failed rollback
  never removes the node or reports ownership as released.

This path deliberately does not use `dlopen`, `sceKernelDlsym` or ELF
`DT_NEEDED` module edges. Symbols are published through a project-controlled
read-only descriptor named `PRXDESC1`. Each entry is a NUL-terminated name and
an address inside one of the module's reported mappings. Before accepting it,
the loader validates magic, ABI version, count, segment arithmetic, readable
extent, name termination, address membership and duplicate names. The maximum
is four segments and 4,096 exports. `COM_GetProcAddress` is therefore a bounded
descriptor lookup, and `COM_NameForFunction` scans the same already-validated
table.

The flow and descriptor ABI derive from BlackBearReloaded's GPL-3.0-or-later
`ps5-native-app-boilerplate` `exp/prx-module` research, consumed through public
fork commit `1e9b564a4dd1d567e63ee0d292ed9a026ce06008`. The local implementation
adds the engine adapter, stricter descriptor validation, diagnostics, host
fault injection and retained-ownership rollback. The builder stamps the
compiled conversion tool with that commit, preventing reuse of an older
cached tool.

## Probe and acceptance contract

`make engine-prx-loader-native-release` builds
`xash_prx_probe.prx` and packages it only when `XASH_PRX_GATE=1`. Its six
exports are an integer add function, a call counter around
`sceKernelUsleep`, a version word, a started word, `module_start` and
`module_stop`. The accepted transcript must contain exactly one of each:

1. `XASH_PRX_BEGIN` naming `COM_LoadLibrary`, `PRXDESC1` and the probe;
2. `XASH_PRX_LOAD` with a positive handle, one to four segments, six exports
   and result zero;
3. `XASH_PRX_RESOLVE` with every expected export present and the deliberately
   missing export absent;
4. `XASH_PRX_CALL` with `19 + 23 = 42`, two counted calls, version `0x10000`,
   successful startup, the kernel import and function-name round trip;
5. `XASH_PRX_UNLOAD` with result zero and ownership released;
6. `XASH_PRX_COMPLETE` with every stage passing, active count zero and exact
   ownership.

The engine-boot validator enforces this with `--prx-gate` in addition to the
normal identity, transcript integrity, filesystem, map-spawn and clean-exit
requirements:

```sh
python3 tools/validate_engine_boot_evidence.py RUN.json \
  --engine-commit 9aa39ad --hlsdk-commit e277ffa --map c1a0 --prx-gate
```

Host tests construct synthetic mapped segments and reject bad versions,
out-of-range pointers, unterminated names, duplicate exports, invalid module
info and every load/start/info/descriptor/unload failure. A failed
post-load rollback is a distinct result: the handle stays owned and a later
shutdown retry must release it.

## FW 12.02 findings

Three diagnostic runs preceded acceptance and are not promoted as passing
evidence:

- `20260908T053236292Z_PPSA99996_xash3d-engine_0xe38e64f7b62c` proved the
  module load succeeded, but the parser rejected module info because it
  required the 0x160 input size word to survive the syscall.
- `20260908T053719782Z_PPSA99996_xash3d-engine_0xe3d06592a8a8` recorded a
  successful `sceKernelGetModuleInfo` with size word zero and four segments.
  FW 12.02 clears that word on success; the parser now accepts zero or the
  SDK-shaped 0x160 while still validating every measured field.
- `20260908T054005332Z_PPSA99996_xash3d-engine_0xe3f6f1917083` proved
  load/resolve/call/unload but observed `auto_started=0`. Although the module
  ELF entry is `module_start`, the loaded image's state was unchanged.

Consequently application modules expose idempotent initialization and the
owner invokes it explicitly when needed. The loader does not silently infer
constructor behavior from the ELF entry. The accepted run records
`auto_started=0 manual_start_rc=0 started=1`; the validator preserves the
automatic-start observation rather than pretending it occurred.

## Immutable evidence

- Transcript SHA-256:
  `ebbec4fb52731b11726da8e246c41bf9400c5a13103a16cc4ad7fef1e461bfe1`
- Manifest SHA-256:
  `7d09166be6700f0f6f9076fc824fde63b48170ca5e2b39d0ac08be12d74f7c4c`
- Gate ELF / fSELF SHA-256:
  `1c8fd80e7cbcdadc4a03cb96449d1d49544c7b9741f229ede40255bb43034f6e` /
  `c67f1cb7f1bd9e966d9364dec9ad9388afb89ee0bb07ee3091443a0e45f85b8f`
- Probe ELF / fSELF SHA-256:
  `f9f276d47c2626d7d848523263e17ccc34fbf74eb5ef3ccf80fc2af55338eea0` /
  `e1a1591fcc2f06de915345f8b6ab17505dd6b78b67d28f65b0d383791d804f69`
- 31 structured records, 40 raw console lines, no gaps or errors; engine/hlsdk
  commits `9aa39ad` / `e277ffa`.

After acceptance, the probe and transactional deployment files were removed
and the normal hybrid build was installed. Regression run
`20260908T054524368Z_PPSA99996_xash3d-engine_0xe441394ac877` had
`prx_gate=0`, packaged no probe, loaded `c1a0` through the static fallback and
closed cleanly. Transcript SHA-256
`9af22c4abc996bebf209c3d2c4af79607e7fdd84b150afba7744606f443061ca`;
production ELF / fSELF SHA-256
`15264acb49412810228151df0019efb85ae61448b25c75cc9dc5f3ce3917c3c8` /
`422bf298926dea76937e3f834f85fe584eb48ccc47157761a8c1893979600b69`.

## Next gate result

The `filesystem_stdio` conversion subsequently passed as Phase 6 gate 2. It
loaded the complete private 4,823-entry asset tree, proved listing, mixed-case
lookup, `gfx/palette.lmp`, the 2,546,336-byte `maps/c1a0.bsp` read, `c1a0`
spawn and exact unload while leaving the server static. See
`FILESYSTEM_PRX_PHASE6.md`. Server conversion is the next independent gate.
