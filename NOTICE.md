# Notices

## Repository origin

This repository contains the public PS5 Xash3D port and its project-owned
platform adapters, renderer bindings, tests and build tooling. It is released
under GPL-3.0-or-later; upstream components retain their own licenses.

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

The Xash3D builder uses the same public fork's `exp/prx-module`
revision `1e9b564a4dd1d567e63ee0d292ed9a026ce06008` for its PRX conversion
tool. The `PRXDESC1` descriptor ABI and kernel module-loading flow in
`xash/platform_ps5/prx_loader_ps5.*` derive from that GPL-3.0-or-later work;
the engine adapter, validation hardening, rollback ownership, tests and
telemetry are maintained in this repository.

## Project icon

The title icon now uses the established Xash3D material mark from the pinned
`FWGS/xash3d-fwgs` tree at `game_launch/icon-xash-material.png`. The source
artwork was introduced by Alibek Omarov in upstream commit
`92b72a7d330ddaa9b8d5c6e8f4a1b6ad1f166d48`; it is redistributed under the
same GPL-3.0-or-later terms as that project. The PS5 derivative only resizes,
centers and flattens the source on a dark neutral background. It contains no
Sony or PlayStation marks.

- Upstream source SHA-256:
  `4a66332b800fa0653645d95c627697f72ca4e26cf19fbeb2d9184ec39d089837`
- PS5 512×512 RGB derivative SHA-256:
  `244c67fd7147267425ce66b5dcf6031bdc9e2f373a04d958758fae847955bde5`

## Hardware capture

`assets/screenshots/half-life-mainui.png` is a direct Remote Play capture
of this project rendering on Manuel Pereira's PlayStation 5. It was produced by
the project owner from his own hardware and is distributed under this
repository's GPL-3.0-or-later license.

- Published capture SHA-256:
  `7feb13b43d4e175fa67e1f76d6f0ccfeeb801c944d061d4ac5ba3ddaff5e5450`

## Mesa geometry attribution

`src/gears_mesh.c` adapts the gear construction from Mesa demos
`src/egl/opengles2/es2gears.c` at revision
`649baedafcb90313ade69909fdef1ee156ab5f8d`. The original is Copyright
(C) 1999-2001 Brian Paul, with the GLES2 port/refactor credited to Kristian
Høgsberg and Alexandros Frantzis, and is licensed under the MIT License. This
project expands its seven triangle strips per tooth into non-indexed triangle
lists suitable for the AGC backend; it does not include EGL or OpenGL code.

## AGC shader source

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
