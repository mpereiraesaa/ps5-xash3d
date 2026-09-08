# Phase 5 project-owned libc shims

This final Phase 5 gate removes three ambiguous runtime dependencies from the
PS5 engine artifact. The goal is not to avoid Prospero libraries generally:
known-good Sce and libc APIs remain in use. These three symbols have deliberately
small application semantics that are safer and more reproducible when owned by
the port.

## Contracts

`__assert` implements the FreeBSD libc destination expected by the imported
headers. It formats a bounded diagnostic, emits the structured
`XASH_ASSERT_FAILURE` marker through `ps5log`, closes the telemetry stream and
calls `abort`. It is declared `noreturn`. The clean hardware gate validates the
formatter and records the reporter/termination policy without intentionally
killing the application; the host test covers exact output and truncation.

`getpwuid` supplies the only field the engine consumes: a stable `pw_name` used
as non-security identity material. It returns `ps5`, preserves the requested
uid and uses `/` for directory and shell. It does not represent a console
account, authorization decision or security boundary.

`dladdr` clears `Dl_info` and returns zero. The whereami library interprets
that result as an instruction to use the supplied `argv[0]`. Crash telemetry
does not lose module attribution: the PS5 backend already uses
`sceKernelGetModuleList` and `sceKernelGetModuleInfo` directly.

The three definitions live in `xash/platform_ps5/libc_shims_ps5.c`, separate
from the broader system adapter. The version script localizes them. A release
build fails unless the dynamic symbol table contains no undefined import for
any of the three and the full symbol table retains every local definition.

## Reproduction and acceptance

```sh
XASH_GAME_DATA=/private/path/half-life \
PS5LOG_DEV_CONF=/private/path/dev.conf \
XASH_GATE_SECONDS=30 make engine-libc-shims-native-release

llvm-readelf --dyn-syms build/engine-boot/eboot.elf | \
  grep -E '(__assert|getpwuid|dladdr)'
llvm-readelf --syms build/engine-boot/eboot.elf | \
  grep -E '(__assert|getpwuid|dladdr)'
```

The first command must produce no lines; the second must show three local
definitions. On hardware, acceptance requires the exact begin/result/end
marker set, `libc_shim_pass=1`, the normal indexed `c1a0` workload, result zero
and a gap-free completion BYE. `tools/validate_engine_boot_evidence.py` enforces
that contract with `--libc-shim-gate`.

FW 12.02 run
`20260907T235551519Z_PPSA99996_xash3d-engine_0xd12e2a9238fb` passed. Its ELF
SHA-256 is
`b87e61fc52230d2503290f92942fe2ab4b7d58730929bd30765aaaa926f957d0`,
its fSELF SHA-256 is
`14c7c9e13d668ed782a2d98a19ada2e21f2003a8d8e7dccf52085a291096e569`,
and its transcript/manifest hashes are
`f4f4c9ac51c122dc36f45e5645d75c37d3676992234279691e1a7ef3674c8961` /
`fdc1aef7159312a3173e09114dc8dad7879a56e01bde74d242ffa23d728fe4b2`.
The run produced 30 structured records with no gap or error, loaded `c1a0` for
30 seconds and returned cleanly to the shell. No Remote Play client was used.

With filesystem, ScePad, SceAudioOut, direct memory, thread/time, GPU/flip
timing and these shims all accepted, Phase 5 is complete. Loading application-
owned PRX modules and binding `ref_agc` belong to Phase 6.
