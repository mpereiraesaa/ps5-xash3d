#!/usr/bin/env python3
from __future__ import annotations

import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "xash/tools"))

import deploy_engine_bundle as deploy  # noqa: E402


def main() -> int:
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        module_dir = root / "sce_module"
        module_dir.mkdir()
        self_data = bytes.fromhex("4f153d1d") + bytes(64)
        (root / "eboot.bin").write_bytes(self_data)
        (module_dir / "server.prx").write_bytes(self_data)
        (root / "map.ps5bsp").write_bytes(b"checked-map")
        items = deploy.bundle(root, ["server.prx"], ["map.ps5bsp"])
        assert [remote.as_posix() for _, remote, _ in items] == [
            "/data/homebrew/PPSA99996/eboot.bin",
            "/data/homebrew/PPSA99996/sce_module/server.prx",
            "/data/homebrew/PPSA99996/map.ps5bsp",
        ]
        assert [is_self for _, _, is_self in items] == [True, True, False]
        for modules in (["server.prx", "server.prx"], ["../server.prx"]):
            try:
                deploy.bundle(root, modules)
            except ValueError:
                pass
            else:
                raise AssertionError(f"accepted unsafe module set: {modules}")
        try:
            deploy.bundle(root, ["missing.prx"])
        except FileNotFoundError:
            pass
        else:
            raise AssertionError("accepted missing module")
        for assets in (["map.ps5bsp", "map.ps5bsp"], ["../map.ps5bsp"],
                       ["valve/maps/c1a0.bsp"]):
            try:
                deploy.bundle(root, [], assets)
            except ValueError:
                pass
            else:
                raise AssertionError(f"accepted unsafe asset set: {assets}")
    print("engine bundle deployment tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
