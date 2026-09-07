#!/usr/bin/env python3
from __future__ import annotations

import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / "xash" / "tools"))

import deploy_game_data as deploy  # noqa: E402


def main() -> int:
    assert deploy.TITLE_ID == "PPSA99996"
    assert str(deploy.REMOTE_TITLE) == "/data/homebrew/PPSA99996"
    assert deploy.parse_list_line(
        "-rw-r--r-- 1 0 0 12 Jan 01 00:00 delta.lst"
    ) == ("delta.lst", False)
    assert deploy.parse_list_line(
        "drwxr-xr-x 1 0 0 0 Jan 01 00:00 folder with spaces"
    ) == ("folder with spaces", True)
    assert str(deploy.validated_backup_path(
        "/data/homebrew/PPSA99996/.xash3d.previous-80629ad44ebe"
    )).endswith(".xash3d.previous-80629ad44ebe")
    try:
        deploy.validated_backup_path("/data/homebrew/PPSA99997/.xash3d.previous-bad")
    except ValueError:
        pass
    else:
        raise AssertionError("cross-title backup path was accepted")
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        (root / "valve" / "gfx").mkdir(parents=True)
        (root / ".dirindex").write_text("valve/delta.lst\tf\n")
        (root / "valve" / "delta.lst").write_bytes(b"delta")
        (root / "valve" / "gfx" / "palette.lmp").write_bytes(b"palette")
        items = deploy.manifest(root)
        assert [item[1] for item in items] == [
            ".dirindex", "valve/delta.lst", "valve/gfx/palette.lmp"]
        assert sum(item[2] for item in items) == 30
        assert deploy.sha256(root / "valve" / "delta.lst") == (
            "4f4a9410ffcdf895c4adb880659e9b5c0dd1f23a30790684340b3eaacb045398"
        )
    print("game-data deploy tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
