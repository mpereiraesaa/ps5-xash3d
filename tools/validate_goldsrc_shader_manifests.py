#!/usr/bin/env python3
"""Fail closed on compiled Phase 4 shader and permutation metadata."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SURFACE_VARIANTS = (
    "goldsrc_surface", "goldsrc_surface_lightmap",
    "goldsrc_surface_fog", "goldsrc_surface_lightmap_fog",
)
MASKED_VARIANTS = (
    "goldsrc_masked", "goldsrc_masked_lightmap",
    "goldsrc_masked_fog", "goldsrc_masked_lightmap_fog",
)
ALL_VARIANTS = SURFACE_VARIANTS + MASKED_VARIANTS + (
    "goldsrc_screen_2d", "goldsrc_screen_2d_masked")


def fail(message: str) -> None:
    raise SystemExit(f"GoldSrc shader validation: {message}")


def validate(manifest_dir: Path,
             generated_source_dir: Path) -> tuple[int, int]:
    pixel_bytes = 0
    unique_pixel_hashes: set[str] = set()
    for name in ALL_VARIANTS:
        path = manifest_dir / f"{name}.manifest.json"
        if not path.is_file():
            fail(f"missing manifest: {name}")
        manifest = json.loads(path.read_text(encoding="utf-8"))
        source = (
            ROOT / "shaders/goldsrc_screen_2d.pipe"
            if name == "goldsrc_screen_2d"
            else generated_source_dir / f"{name}.pipe"
        )
        expected_source = source.resolve().relative_to(ROOT).as_posix()
        if (
            manifest.get("schema"), manifest.get("name"),
            manifest.get("target"), manifest.get("gfxip"),
            manifest.get("source"), manifest.get("no_relocations")
        ) != (1, name, "gfx1013", "10.1.3", expected_source, True):
            fail(f"identity mismatch: {name}")
        stages = manifest.get("stages", {})
        if set(stages) != {"pre_raster_gs", "pixel"}:
            fail(f"stage set mismatch: {name}")
        pixel = stages["pixel"]
        if int(pixel.get("bytes", 0)) <= 0:
            fail(f"empty pixel ISA: {name}")
        pixel_bytes += int(pixel["bytes"])
        unique_pixel_hashes.add(str(pixel.get("sha256", "")))
        registers = manifest.get("graphics_register_metadata", {})
        control = registers.get(".db_shader_control", {})
        expected_kill = name in MASKED_VARIANTS or name == "goldsrc_screen_2d_masked"
        if control.get(".kill_enable") is not expected_kill:
            fail(f"kill-enable mismatch: {name}")
        source_text = source.read_text(encoding="utf-8")
        if ("discard;" in source_text) is not expected_kill:
            fail(f"source discard mismatch: {name}")
        if not name.startswith("goldsrc_screen_2d"):
            if ("texture(lightmap_texture" in source_text) != (
                "lightmap" in name
            ):
                fail(f"lightmap specialization mismatch: {name}")
            if ("distance_to_eye" in source_text) != name.endswith("fog"):
                fail(f"fog specialization mismatch: {name}")

    if len(unique_pixel_hashes) < len(SURFACE_VARIANTS + MASKED_VARIANTS):
        fail("surface/masked pixel-stage permutations did not remain distinct")
    return len(ALL_VARIANTS), pixel_bytes


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest-dir", type=Path,
                        default=ROOT / "build/shaders")
    parser.add_argument("--generated-source-dir", type=Path,
                        default=ROOT / "build/generated-shaders")
    args = parser.parse_args()
    count, pixel_bytes = validate(
        args.manifest_dir.resolve(), args.generated_source_dir.resolve()
    )
    print(f"GoldSrc shader validation passed: variants={count} "
          f"pixel_isa_bytes={pixel_bytes} target=gfx1013")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
