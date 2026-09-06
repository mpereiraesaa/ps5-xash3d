#!/usr/bin/env python3
"""Verify deterministic and explicit Phase 4 shader generation."""

import importlib.util
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True
SPEC = importlib.util.spec_from_file_location(
    "generate_goldsrc_shader_variants",
    ROOT / "tools/generate_goldsrc_shader_variants.py",
)
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def main() -> int:
    template = MODULE.TEMPLATE.read_text(encoding="utf-8")
    first = MODULE.variants(template)
    second = MODULE.variants(template)
    assert first == second and len(first) == 8
    assert set(first) == {
        "goldsrc_surface", "goldsrc_surface_lightmap",
        "goldsrc_surface_fog", "goldsrc_surface_lightmap_fog",
        "goldsrc_masked", "goldsrc_masked_lightmap",
        "goldsrc_masked_fog", "goldsrc_masked_lightmap_fog",
    }
    for name, source in first.items():
        assert all(token not in source for token in MODULE.TOKENS)
        assert ("discard;" in source) == name.startswith("goldsrc_masked")
        assert ("texture(lightmap_texture" in source) == (
            "lightmap" in name
        )
        assert ("distance_to_eye" in source) == name.endswith("fog")
        assert "color = vec4(surface, base.a * render_color.a);" in source
    pipeline_spec = json.loads(
        (ROOT / "shaders/pipeline_permutations.json").read_text(encoding="utf-8")
    )
    ids = [entry["id"] for entry in pipeline_spec["pipelines"]]
    assert len(ids) == 13 and len(set(ids)) == 13
    assert ids[-9:] == list(first) + ["goldsrc_screen_2d"]
    print("GoldSrc shader variant generator tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
