#!/usr/bin/env python3
"""Emit private Studio bundle identity into a generated native header."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bundle", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    data = args.bundle.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        "#ifndef PS5_XASH3D_STUDIO_BUILD_METADATA_H\n"
        "#define PS5_XASH3D_STUDIO_BUILD_METADATA_H\n\n"
        f"#define PS5_STUDIO_BUNDLE_SHA256 \"{digest}\"\n"
        f"#define PS5_STUDIO_BUNDLE_BYTES {len(data)}u\n\n"
        "#endif\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
