# Phase 6 dynamic MainUI menu PRX gate

This gate moves upstream MainUI out of the executable and into
`/app0/sce_module/menu.prx`. It is the port's first engine-owned UI boundary:
the real menu initializes and redraws through Xash's software renderer. It is
not yet TV-visible native AGC output; replacing `ref_soft` with `ref_agc` is a
later Phase 6 gate.

## Build and module boundary

```sh
XASH_GAME_DATA=/private/path/half-life \
  PS5LOG_DEV_CONF=/private/path/dev.conf \
  XASH_GATE_SECONDS=15 make engine-menu-prx-native-release
```

`XASH_MENU_PRX=1` requires client mode and the accepted dynamic filesystem and
server checkpoints. The host keeps `client`, `ref_null` and `ref_soft` static;
the signed bundle contains the executable plus `filesystem_stdio.prx`,
`server.prx` and `menu.prx`. MainUI is compiled as PIC, linked with its C++
runtime support and receives the same explicit constructor/finalizer lifecycle
already proven by the server gate.

The menu descriptor has six entries: `GetMenuAPI`, `GetExtAPI`, state,
export-count, `module_start` and `module_stop`. The accepted module has 46
dynamic imports and zero denied imports. The independently audited filesystem,
server and executable have 64, 43 and 186 imports respectively, also with zero
denied imports.

## ABI and UI workload

The loader wraps both MainUI entry points without changing their ABI. The base
API must publish all 16 callbacks and receive six representative engine
services plus non-null globals. The extended version-1 API must publish all 12
callbacks and receive four representative extended services. Initialization,
activation, redraw and shutdown are counted around the original callbacks.

The gate intentionally omits `+map c1a0`, keeping the menu active for the full
bounded run. Acceptance requires a visible MainUI redraw and a non-black
640x480 software framebuffer. Xash still initializes `server.prx` during client
startup; the validator therefore preserves its ABI probes and exact unload,
but does not claim a second map-spawn gate.

The retail Half-Life data does not request internal legacy VGUI1 support. Xash
therefore probes the optional `libvgui_support.prx`, then probes the client and
continues without VGUI1. The application loader records the absent optional
module as one bounded `WARN` (`fallback=client-probe`), not as a failed MainUI
load. MainUI is independent and remains active. Packaging legacy VGUI1 is not
part of this gate.

## Accepted FW 12.02 evidence

- Run: `20260908T094038112Z_PPSA99996_xash3d-engine_0xf1174a815840`.
- Host ELF / signed fSELF SHA-256:
  `8bb9e1106db5c6394b0a4bd65c9509f9f9a2db0b91d1e2c14ab0f9d5bc8cf9b8` /
  `8bcd0033abb3230841467196adec209146c20b7b4ec3b3a3932b18df7c957680`.
- Menu ELF / signed PRX SHA-256:
  `64099d2824a41580d482435a5c567ef30bcecf9e868463c915b5cf3c5697686d` /
  `ec496e4c978dbef7f12305134eb2ba441de2983f551c5ef853e7291c8045aa1b`.
- Transcript / manifest SHA-256:
  `5f2b7da6f0bb0c9a0c248f3f97800a841c9211fe0876c3e34c62ab55ab6dc579` /
  `a7c17f88e0160a9d6ae216eea034eaae3452122f788a4730d20e738252705d29`.
- 16/16 base callbacks, engine mask 63, 12/12 extended callbacks and extended
  engine mask 15; both APIs passed.
- One init, one activation, 5,127 redraws and 5,100 presented frames; the last
  framebuffer hash was `b12dbb47c69ddcb2` and non-zero content was observed.
- The 4,823-entry filesystem probe retained mixed-case `palette.lmp` and the
  2,546,336-byte `c1a0.bsp` read.
- Server, menu and filesystem stopped and unloaded in that order, ending with
  zero active modules, exact memory teardown, 73 structured records, 77 raw
  lines, no errors, gaps or oversized records, and a clean BYE.

Validate the immutable run with `--mode client --filesystem-prx-gate
--menu-prx-gate`. The next rollback point converts the client module while
retaining this accepted filesystem/server/menu bundle.
