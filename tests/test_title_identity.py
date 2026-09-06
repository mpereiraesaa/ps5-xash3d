#!/usr/bin/env python3
"""Keep the package, runtime telemetry and release identity in lockstep."""

from __future__ import annotations

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TITLE_ID = "PPSA99996"
CONTENT_ID = "UP9000-PPSA99996_00-PS5XASH3D000001"


def require(text: str, marker: str, source: str) -> None:
    if marker not in text:
        raise AssertionError(f"{source} is missing {marker!r}")


def main() -> int:
    param = json.loads((ROOT / "sce_sys/param.json").read_text())
    assert param["titleId"] == TITLE_ID
    assert param["conceptId"] == "99996"
    assert param["contentId"] == CONTENT_ID
    assert param["localizedParameters"]["en-US"]["titleName"] == "PS5 Xash3D"

    native = (ROOT / "native/main.c").read_text()
    require(native, f'#define PS5_XASH3D_TITLE_ID "{TITLE_ID}"', "native/main.c")
    require(native, '#define PS5_XASH3D_APP_NAME "ps5-xash3d"', "native/main.c")

    builder = (ROOT / "tools/build_native.sh").read_text()
    require(builder, f"title_id={TITLE_ID}", "tools/build_native.sh")
    require(builder, 'dist="$root/dist/$title_id"', "tools/build_native.sh")

    packager = (ROOT / "tools/package_release.sh").read_text()
    require(packager, f"title_id={TITLE_ID}", "tools/package_release.sh")
    require(packager, 'name="ps5-xash3d-$version"', "tools/package_release.sh")

    print(f"title identity contract passed: {TITLE_ID}/ps5-xash3d")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
