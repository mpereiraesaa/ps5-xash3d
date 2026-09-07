#!/usr/bin/env python3
"""Fail-closed host inspection for a private Studio runtime bundle."""

from __future__ import annotations

import argparse
import struct
import zlib
from pathlib import Path

from bake_studio import (DRAW, HEADER, MAGIC, OUTPUT_TEXTURE, POSE,
                         SOURCE_VERTEX, VERSION)


def fail(message: str) -> None:
    raise SystemExit(f"studio bundle invalid: {message}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("bundle", type=Path)
    args = parser.parse_args()
    data = args.bundle.read_bytes()
    if len(data) < HEADER.size:
        fail("truncated header")
    row = HEADER.unpack_from(data)
    if row[0] != MAGIC or row[1] != VERSION or row[2] != HEADER.size or \
            row[3] != len(data):
        fail("header contract mismatch")
    if zlib.crc32(data[HEADER.size:]) & 0xFFFFFFFF != row[4]:
        fail("payload checksum mismatch")
    bones, frames, vertices, indices, draws, textures = (
        row[8], row[9], row[12], row[13], row[14], row[15])
    offsets = row[22:29]
    sizes = (bones * 2, bones * frames * POSE.size,
             vertices * SOURCE_VERTEX.size, indices * 2,
             draws * DRAW.size, textures * OUTPUT_TEXTURE.size, row[29])
    for offset, size in zip(offsets, sizes):
        if offset < HEADER.size or offset > len(data) or size > len(data) - offset:
            fail("section range mismatch")
    print(f"studio bundle valid: bytes={len(data)} bones={bones} "
          f"frames={frames} fps={row[10]:.3f} vertices={vertices} "
          f"indices={indices} draws={draws} textures={textures} "
          f"source_fnv64={row[5]:016x}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
