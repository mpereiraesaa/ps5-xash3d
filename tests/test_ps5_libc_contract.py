#!/usr/bin/env python3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    build = (ROOT / "xash" / "build_engine.sh").read_text()
    assert "-DHAVE_STRCASESTR=0" in build
    assert "-DHAVE_STRCASESTR=1" not in build
    for macro in ("STRCASECMP", "STRNLEN", "STRLCPY", "STRLCAT"):
        assert f"-DHAVE_{macro}=1" in build
    assert "XASH_LIBC_SMOKE" in build
    assert "did not retain the $symbol dynamic import" in build
    assert "did not retain marker $marker" in build
    assert "--dyn-syms" in build
    assert "grep -qw strcasestr" in build
    assert "Q_stristr fallback" in build
    assert "XASH_LIBC_SHIM_GATE" in build
    assert "libc_shims_ps5.c" in build
    assert "leaked dynamic import $symbol" in build
    assert "did not retain project definition $symbol" in build

    crtlib = (ROOT / "third_party" / "xash3d-fwgs" / "public" / "crtlib.c").read_text()
    assert "#if !HAVE_STRCASESTR" in crtlib
    assert "char *Q_stristr" in crtlib

    boot = (ROOT / "xash" / "platform_ps5" / "boot_ps5.c").read_text()
    for symbol in ("strcasecmp", "strnlen", "strlcpy", "strlcat"):
        assert f"system_{symbol}" in boot
        assert f"symbol={symbol}" in boot
    assert "XASH_LIBC_SMOKE_END pass=%d" in boot
    for marker in ("XASH_LIBC_SHIM_BEGIN", "XASH_LIBC_SHIM_RESULT",
                   "XASH_LIBC_SHIM_END"):
        assert marker in boot

    shims = (ROOT / "xash" / "platform_ps5" / "libc_shims_ps5.c").read_text()
    assert "void __assert(" in shims
    assert "struct passwd *getpwuid(" in shims
    assert "int dladdr(" in shims
    assert 'ps5_identity_name[] = "ps5"' in shims
    assert "ps5log_close( \"xash-assert-failed\" )" in shims
    assert "abort( );" in shims

    system = (ROOT / "xash" / "platform_ps5" / "sys_ps5.c").read_text()
    for definition in ("void __assert(", "struct passwd *getpwuid(",
                       "int dladdr("):
        assert definition not in system

    symbols = (ROOT / "xash" / "platform_ps5" / "app-symbols.map").read_text()
    for symbol in ("__assert;", "getpwuid;", "dladdr;"):
        assert symbol in symbols

    print("PS5 libc contract tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
