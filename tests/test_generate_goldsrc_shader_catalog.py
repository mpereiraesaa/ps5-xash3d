#!/usr/bin/env python3
"""Check the generated Phase 4 native shader catalog."""

import importlib.util
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / "tools"))
SPEC = importlib.util.spec_from_file_location(
    "generate_goldsrc_shader_catalog",
    ROOT / "tools/generate_goldsrc_shader_catalog.py",
)
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def main() -> int:
    header = MODULE.render()
    assert header.count("static const struct ps5_shader_metadata") == 10
    assert header.count("_metadata,") == 10
    for name in MODULE.VARIANTS:
        assert header.count(f'"{name}"') == 1
        assert header.count(f'#include "{name}_shader_metadata.h"') == 1
        for stage in ("gs", "ps"):
            assert header.count(f"ps5_{name}_{stage}_start") == 2
            assert header.count(f"ps5_{name}_{stage}_end") == 2
    assert "GOLDSRC_SHADER_VARIANT_COUNT" in header
    assert "/home/" not in header and "/data/" not in header
    print("GoldSrc shader catalog generator tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
