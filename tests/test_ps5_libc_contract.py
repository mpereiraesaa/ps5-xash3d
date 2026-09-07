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

    crtlib = (ROOT / "third_party" / "xash3d-fwgs" / "public" / "crtlib.c").read_text()
    assert "#if !HAVE_STRCASESTR" in crtlib
    assert "char *Q_stristr" in crtlib

    boot = (ROOT / "xash" / "platform_ps5" / "boot_ps5.c").read_text()
    for symbol in ("strcasecmp", "strnlen", "strlcpy", "strlcat"):
        assert f"system_{symbol}" in boot
        assert f"symbol={symbol}" in boot
    assert "XASH_LIBC_SMOKE_END pass=%d" in boot

    print("PS5 libc contract tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
