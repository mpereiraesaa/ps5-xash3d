#!/usr/bin/env python3
"""Check the generated Phase 4 shader embedding boundary."""

import importlib.util
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True
SPEC = importlib.util.spec_from_file_location(
    "generate_goldsrc_shader_assets",
    ROOT / "tools/generate_goldsrc_shader_assets.py",
)
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def main() -> int:
    assembly = MODULE.render()
    assert len(MODULE.VARIANTS) == 10 and len(set(MODULE.VARIANTS)) == 10
    assert assembly.count(".incbin ") == 20
    for name in MODULE.VARIANTS:
        for stage in ("gs", "ps"):
            assert assembly.count(f"ps5_{name}_{stage}_start") == 2
            assert assembly.count(f"ps5_{name}_{stage}_end") == 2
            assert assembly.count(f"build/shaders/{name}.{stage}.bin") == 1
    assert "/home/" not in assembly and "/data/" not in assembly
    print("GoldSrc shader asset generator tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
