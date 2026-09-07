#!/usr/bin/env python3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    build = (ROOT / "xash" / "build_engine.sh").read_text()
    assert "-DHAVE_STRCASESTR=0" in build
    assert "-DHAVE_STRCASESTR=1" not in build
    assert "--dyn-syms" in build
    assert "grep -qw strcasestr" in build
    assert "Q_stristr fallback" in build

    crtlib = (ROOT / "third_party" / "xash3d-fwgs" / "public" / "crtlib.c").read_text()
    assert "#if !HAVE_STRCASESTR" in crtlib
    assert "char *Q_stristr" in crtlib

    print("PS5 libc contract tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
