#!/usr/bin/env python3
"""Generate the typed native catalog for Phase 4 shader assets."""

from __future__ import annotations

import argparse
from pathlib import Path

from generate_goldsrc_shader_assets import VARIANTS


ROOT = Path(__file__).resolve().parents[1]


def render() -> str:
    lines = [
        "#ifndef PS5_XASH3D_GENERATED_GOLDSRC_SHADER_CATALOG_H",
        "#define PS5_XASH3D_GENERATED_GOLDSRC_SHADER_CATALOG_H",
    ]
    for name in VARIANTS:
        lines.append(f'#include "{name}_shader_metadata.h"')
    lines.append("")
    for name in VARIANTS:
        lines.extend((
            f"extern const uint8_t ps5_{name}_gs_start[];",
            f"extern const uint8_t ps5_{name}_gs_end[];",
            f"extern const uint8_t ps5_{name}_ps_start[];",
            f"extern const uint8_t ps5_{name}_ps_end[];",
        ))
    lines.append("")
    for name in VARIANTS:
        upper = name.upper()
        lines.extend((
            f"static const struct ps5_shader_metadata {name}_metadata = {{",
            f"    PS5_{upper}_GS_RSRC1, PS5_{upper}_GS_RSRC2,",
            f"    PS5_{upper}_PS_RSRC1, PS5_{upper}_PS_RSRC2,",
            f"    PS5_{upper}_GE_CNTL, PS5_{upper}_SHADER_STAGES_EN,",
            f"    PS5_{upper}_GS_OUT_PRIM_TYPE, PS5_{upper}_DRAW_MODIFIER,",
            f"    ps5_{name}_pre_raster_cx,",
            f"    sizeof(ps5_{name}_pre_raster_cx) /",
            f"        sizeof(ps5_{name}_pre_raster_cx[0]),",
            f"    ps5_{name}_pixel_cx,",
            f"    sizeof(ps5_{name}_pixel_cx) /",
            f"        sizeof(ps5_{name}_pixel_cx[0]),",
            "};",
        ))
    lines.extend((
        "",
        "static const GoldSrcShaderAsset goldsrc_shader_catalog[] = {",
    ))
    for name in VARIANTS:
        enum_name = "GOLDSRC_SHADER_" + name.removeprefix("goldsrc_").upper()
        lines.extend((
            "    {",
            f'        "{name}", {enum_name},',
            f"        ps5_{name}_gs_start, ps5_{name}_gs_end,",
            f"        ps5_{name}_ps_start, ps5_{name}_ps_end,",
            f"        &{name}_metadata,",
            "    },",
        ))
    lines.extend((
        "};",
        "_Static_assert(sizeof(goldsrc_shader_catalog) /",
        "                   sizeof(goldsrc_shader_catalog[0]) ==",
        "               GOLDSRC_SHADER_VARIANT_COUNT,",
        '               "Phase 4 shader catalog count");',
        "#endif",
        "",
    ))
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--output", type=Path,
        default=ROOT / "build/generated/goldsrc_shader_catalog_generated.h"
    )
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(render(), encoding="utf-8")
    print(args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
