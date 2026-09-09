#!/usr/bin/env python3
"""Generate the eight explicit Phase 4 surface shader permutations."""

from __future__ import annotations

import argparse
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TEMPLATE = ROOT / "shaders/goldsrc_surface.template.pipe"
SCREEN_SOURCE = ROOT / "shaders/goldsrc_screen_2d.pipe"
TOKENS = ("@MASK_BLOCK@", "@LIGHTMAP_EXPR@", "@FOG_BLOCK@", "@QA_LIGHTMAP_EXPR@")


def render(template: str, *, masked: bool, lightmap: bool, fog: bool) -> str:
    for token in TOKENS:
        if template.count(token) != 1:
            raise ValueError(f"template must contain exactly one {token}")
    values = {
        "@QA_LIGHTMAP_EXPR@": (
            "texture(lightmap_texture, light_uv).rgb" if lightmap else "vec3(1.0)"
        ),
        "@MASK_BLOCK@": "    if (base.a < 0.5)\n        discard;" if masked else "",
        "@LIGHTMAP_EXPR@": (
            "base.rgb * texture(lightmap_texture, light_uv).rgb"
            if lightmap else "base.rgb"
        ),
        "@FOG_BLOCK@": (
            "    float distance_to_eye = 1.0 / max(gl_FragCoord.w, 0.0001);\n"
            "    float density = max(fog_color_density.a, 0.0);\n"
            "    float fog_amount = 1.0 - exp2(-1.442695 * density * density *\n"
            "                                  distance_to_eye * distance_to_eye);\n"
            "    surface = mix(surface, fog_color_density.rgb,\n"
            "                  clamp(fog_amount, 0.0, 1.0));"
            if fog else ""
        ),
    }
    output = template
    for token, value in values.items():
        output = output.replace(token, value)
    return output


def variants(template: str) -> dict[str, str]:
    definitions = (
        ("goldsrc_surface", False, False, False),
        ("goldsrc_surface_lightmap", False, True, False),
        ("goldsrc_surface_fog", False, False, True),
        ("goldsrc_surface_lightmap_fog", False, True, True),
        ("goldsrc_masked", True, False, False),
        ("goldsrc_masked_lightmap", True, True, False),
        ("goldsrc_masked_fog", True, False, True),
        ("goldsrc_masked_lightmap_fog", True, True, True),
    )
    return {
        name: render(template, masked=masked, lightmap=lightmap, fog=fog)
        for name, masked, lightmap, fog in definitions
    }


def screen_masked(source: str) -> str:
    assignment = "    out_color = texture(image, uv) * color;"
    if source.count(assignment) != 1:
        raise ValueError("screen shader must contain exactly one color assignment")
    # Pinned GL_DEFAULT_ALPHATEST is zero; do not reuse the 3D 0.5 cutoff.
    return source.replace(assignment, assignment +
                          "\n    if (out_color.a <= 0.0)\n        discard;")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--output-dir", type=Path,
        default=ROOT / "build/generated-shaders"
    )
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    rendered = variants(TEMPLATE.read_text(encoding="utf-8"))
    rendered["goldsrc_screen_2d_masked"] = screen_masked(
        SCREEN_SOURCE.read_text(encoding="utf-8"))
    for name, source in sorted(rendered.items()):
        (args.output_dir / f"{name}.pipe").write_text(source, encoding="utf-8")
    print(f"generated {len(rendered)} GoldSrc shader variants")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
