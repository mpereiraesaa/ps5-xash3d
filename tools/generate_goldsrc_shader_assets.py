#!/usr/bin/env python3
"""Generate assembly embeddings for the nine Phase 4 shader pipelines."""

from __future__ import annotations

import argparse
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
VARIANTS = (
    "goldsrc_surface", "goldsrc_surface_lightmap",
    "goldsrc_surface_fog", "goldsrc_surface_lightmap_fog",
    "goldsrc_masked", "goldsrc_masked_lightmap",
    "goldsrc_masked_fog", "goldsrc_masked_lightmap_fog",
    "goldsrc_screen_2d",
)


def render() -> str:
    lines = ["/* Generated from checked Phase 4 shader manifests. */", ""]
    for name in VARIANTS:
        lines.extend((
            f'.section .rodata.{name}_shaders,"a",@progbits',
            "",
            ".balign 256",
            f".global ps5_{name}_gs_start",
            f"ps5_{name}_gs_start:",
            f'.incbin "build/shaders/{name}.gs.bin"',
            f".global ps5_{name}_gs_end",
            f"ps5_{name}_gs_end:",
            "",
            ".balign 256",
            f".global ps5_{name}_ps_start",
            f"ps5_{name}_ps_start:",
            f'.incbin "build/shaders/{name}.ps.bin"',
            f".global ps5_{name}_ps_end",
            f"ps5_{name}_ps_end:",
            "",
        ))
    lines.extend(('.section .note.GNU-stack,"",@progbits', ""))
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--output", type=Path,
        default=ROOT / "build/generated/goldsrc_shader_assets.S"
    )
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(render(), encoding="utf-8")
    print(args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
