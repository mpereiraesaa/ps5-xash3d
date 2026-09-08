# Notices

## Repository origin

This repository continues `mpereiraesaa/ps5-agc-gears` from commit
`cbff2649520984696270fedf94a217954b9efddd` with its complete history. The
Gears repository remains published as the standalone demo; the Xash3D port,
including the Phase 1-3 BSP viewer, resource foundation and texture path,
continues here under the same GPL-3.0-or-later license and authorship.

## Native application foundation

The native application shell derives from
`blackbearreloaded/ps5-native-app-boilerplate`. The currently validated tree is
Manuel Pereira's public fork at commit
`37dd53602bdead63936f718004555ba10154be48`; it contains upstream commit
`722f2227a8bb6fa2229120546995b6562552c752` plus the separately documented
RELRO load-segment congruence fix. Original boilerplate code is Copyright (C)
2026 BlackBearReloaded and GPL-3.0-or-later. The foundation is fetched into an
ignored dependency directory and verified at the pinned revision; it is not
vendored into this repository.

The Xash3D Phase 6 builder uses the same public fork's `exp/prx-module`
revision `1e9b564a4dd1d567e63ee0d292ed9a026ce06008` for its PRX conversion
tool. The `PRXDESC1` descriptor ABI and kernel module-loading flow in
`xash/platform_ps5/prx_loader_ps5.*` derive from that GPL-3.0-or-later work;
the engine adapter, validation hardening, rollback ownership, tests and
telemetry are maintained in this repository.

## Project icon

The PS5 AGC Gears icon was generated specifically for this project with
OpenAI's built-in image-generation tool on 2026-09-05. The prompt requested
three original interlocking 3D gears, emerald/cyan illumination, a deep violet
background, strong small-icon readability, no text and no trademarks. It does
not derive from the boilerplate presentation assets.

- Master SHA-256:
  `50accc91e38822a8b11cb6eed916d968184edb8306a1099fc3d2aa0a72b402b0`
- PS5 512×512 RGB derivative SHA-256:
  `cc40f50deb429e8bcf07eb43be5a3176c4f8445a88e045e830b066202b66efb8`

## Hardware capture

`assets/screenshots/ps5-agc-gears-hardware.png` is a direct Remote Play capture
of this project rendering on Manuel Pereira's PlayStation 5. It was produced by
the project owner from his own hardware and is distributed under this
repository's GPL-3.0-or-later license.

- Published capture SHA-256:
  `484e6830835386eee439df6ecec46fa18fa3d8b08780ad56b8aa0b5245f06905`

## Mesa es2gears geometry

`src/gears_mesh.c` adapts the gear construction from Mesa demos
`src/egl/opengles2/es2gears.c` at revision
`649baedafcb90313ade69909fdef1ee156ab5f8d`. The original is Copyright
(C) 1999-2001 Brian Paul, with the GLES2 port/refactor credited to Kristian
Høgsberg and Alexandros Frantzis, and is licensed under the MIT License. This
project expands its seven triangle strips per tooth into non-indexed triangle
lists suitable for the AGC backend; it does not include EGL or OpenGL code.

## Gears shader

`shaders/gears_lit.pipe` is independently authored for this project. It is
distributed as source only; generated PAL ELF files and extracted ISA binaries
are build artifacts and are intentionally excluded from publication.

## ps5log client

`native/ps5log` is the project-owned client extracted from the local
`logging_server` project. The four source files are retained byte-for-byte and
covered by their original host test, with only public test addresses substituted
in the copied test fixture. They are distributed under this repository's
GPL-3.0-or-later license.

## Xash3D FWGS engine

`third_party/xash3d-fwgs` is a pinned, unmodified Git submodule of
[FWGS/xash3d-fwgs](https://github.com/FWGS/xash3d-fwgs) at commit
`9aa39ad4`, GPL-3.0-or-later. Its nested `library_suffix` and `bzip2`
submodules are initialized by the build. `xash/platform_ps5/` and
`xash/build_engine.sh` are this repository's own code; the static module
tables they generate follow the layout of the engine's
`scripts/waifulib/xshlib.py`.

## Half-Life SDK (hlsdk-portable)

`third_party/hlsdk-portable` is a pinned, unmodified Git submodule of
[FWGS/hlsdk-portable](https://github.com/FWGS/hlsdk-portable) at commit
`e277ffaa`, distributed under Valve's Half-Life 1 SDK license. It is compiled
into the engine boot title as the statically linked `server` module for
development on hardware; no game assets, maps or WAD files are part of this
repository or of any published artifact.

## PS5 native gamepad input research

The independently authored ScePad contract in `xash/platform_ps5/in_ps5.c`
was derived from
[blackbearreloaded/ps5-native-gamepad-input-research](https://github.com/blackbearreloaded/ps5-native-gamepad-input-research)
at commit `16e9b953b26a7102bc801a380f08fbf00060d84b`, GPL-3.0. The port contains
its own C adapter and project-owned compatibility declarations; it does not
vendor the research project or any proprietary SDK header.

## PS5 audio research

The native `libSceAudioOut` PCM contract used by `xash/platform_ps5/audio_ps5.c`
derives from the independently authored, device-tested
[ps5-audio-decoding-research](https://github.com/blackbearreloaded/ps5-audio-decoding-research)
at commit `2c81f17910be6e7b26d05ae50f50adb0211581c2`, Copyright (C) 2026
BlackBearReloaded, GPL-3.0-or-later. Specifically the six-argument
`sceAudioOutOpen` signature, the grain/sample-rate/format constraints, the 0 dB
eight-entry volume array, the blocking `Output` pacing and the "no short final
block, hold the tail and zero-fill it" rule.

That project's evidence is firmware 6.02. It informed the design only: no symbol
was recorded as hardware-validated here until this port exercised it on FW
12.02, and the accepted run additionally measured behaviour the reference does
not document (`sceAudioOutOutput` returns the number of frames accepted). The
reference C++ helper and its host test are **not** vendored and no code was
copied from them; `include/ps5_platform.h`, the C core, the resampler, the ring,
the worker, the deterministic pattern and the SNDDMA binding are this
repository's own code under its GPL-3.0-or-later license, and no vendor header
is included.

## Research boundary

No proprietary Sony SDK file, game asset, shader, module, dump or command
buffer is included. Hardware observations are reported only as sanitized facts.
