#!/usr/bin/env python3
from __future__ import annotations

import sys
import tempfile
import hashlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "xash/tools"))

import deploy_engine_bundle as deploy  # noqa: E402


class FakeFtp:
    def __init__(self, data: bytes, responses: list[str] | None = None):
        self.data = data
        self.responses = responses or ["200 SELF decryption disabled"]
        self.commands: list[str] = []

    def sendcmd(self, command: str) -> str:
        self.commands.append(command)
        return self.responses.pop(0)

    def size(self, _remote: str) -> int:
        return len(self.data)

    def retrbinary(self, command: str, consume) -> None:
        self.commands.append(command)
        consume(self.data[:3])
        consume(self.data[3:])


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
        for name in deploy.AGC_MODULES:
            (module_dir / name).write_bytes(self_data)
        (root / "model.ps5mdl").write_bytes(b"model")
        try:
            deploy.bundle(root, ["ref_agc.prx"], ["map.ps5bsp"])
        except ValueError as exc:
            assert "all six PRXs" in str(exc)
        else:
            raise AssertionError("accepted a partial AGC deployment")
        try:
            deploy.bundle(root, sorted(deploy.AGC_MODULES), sorted(deploy.SAFE_ASSETS))
        except ValueError as exc:
            assert "identity mismatch" in str(exc)
        else:
            raise AssertionError("accepted mismatched renderer and bundle")
        (module_dir / "ref_agc.prx").write_bytes(
            self_data + deploy.digest(root / "map.ps5bsp").encode("ascii"))
        assert len(deploy.bundle(root, sorted(deploy.AGC_MODULES),
                                 sorted(deploy.SAFE_ASSETS))) == 9
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
        raw = FakeFtp(self_data, ["200 SELF decryption enabled",
                                 "200 SELF decryption disabled"])
        assert "disabled" in deploy.disable_self_decryption(raw).lower()
        assert raw.commands == ["SELF", "SELF"]
        assert deploy.disable_self_decryption(raw) == \
            "SELF transfer mode already disabled"
        assert raw.commands == ["SELF", "SELF"]
        assert deploy.verify_remote_exact(raw, "/stage/eboot.bin",
                                          root / "eboot.bin") == len(self_data)
        assert raw.commands[-1] == "RETR /stage/eboot.bin"
        corrupt = FakeFtp(self_data[:-1] + b"x")
        assert hashlib.sha256(corrupt.data).digest() != \
            hashlib.sha256(self_data).digest()
        try:
            deploy.verify_remote_exact(corrupt, "/stage/eboot.bin",
                                       root / "eboot.bin")
        except RuntimeError as exc:
            assert "digest mismatch" in str(exc)
        else:
            raise AssertionError("accepted a corrupted staged SELF")
    print("engine bundle deployment tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
