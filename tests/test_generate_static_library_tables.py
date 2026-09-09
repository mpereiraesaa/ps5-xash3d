#!/usr/bin/env python3
from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / "xash" / "tools"))

import generate_static_library_tables as tables  # noqa: E402


def main() -> int:
    callbacks = {"_ZN10CBaseDelay10DelayThinkEv", "_ZThn8_N5Probe3RunEv"}
    assert tables.server_callback_exports(callbacks | {"worldspawn", "memcpy"}) == sorted(callbacks)
    try:
        tables.server_callback_exports({"_Zbad-name"})
    except ValueError:
        pass
    else:
        raise AssertionError("invalid callback symbol accepted")
    assert tables.parse_exports("GetFSAPI\n") == ["GetFSAPI"]
    assert tables.parse_exports("# comment\nA\n\nB # trailing\n") == ["A", "B"]
    for bad in ("", "#only\n", "1abc\n", "A\nA\n", "bad-name\n"):
        try:
            tables.parse_exports(bad)
        except ValueError:
            pass
        else:
            raise AssertionError(f"accepted invalid export list {bad!r}")

    header = tables.render_tables_header(["filesystem_stdio", "server"])
    assert "extern table_t lib_filesystem_stdio_exports[];" in header
    assert 'struct {const char *name;void *func;} libs[] = {' in header
    assert '{ "server", &lib_server_exports },' in header
    assert header.rstrip().endswith("{0,0}\n};")

    helper = tables.render_link_helper("server", ["GiveFnptrsToDll", "GetEntityAPI"])
    assert "extern void GiveFnptrsToDll(void);" in helper
    assert "lib_server_exports[] = {" in helper
    assert '{ "GetEntityAPI", &GetEntityAPI },' in helper

    with tempfile.TemporaryDirectory() as temporary:
        directory = Path(temporary)
        exports = directory / "fs.txt"
        exports.write_text("GetFSAPI\n")
        out = directory / "gen"
        result = subprocess.run(
            [sys.executable, "-B", str(ROOT / "xash/tools/generate_static_library_tables.py"),
             str(out), f"filesystem_stdio={exports}"],
            text=True, capture_output=True, check=False)
        assert result.returncode == 0, result.stderr
        assert (out / "generated_library_tables.h").exists()
        assert (out / "link_helper_filesystem_stdio.c").exists()
        empty = directory / "empty"
        empty_result = subprocess.run(
            [sys.executable, "-B", str(ROOT / "xash/tools/generate_static_library_tables.py"),
             str(empty)], text=True, capture_output=True, check=False)
        assert empty_result.returncode == 0, empty_result.stderr
        assert "{0,0}" in (empty / "generated_library_tables.h").read_text()
        duplicate = subprocess.run(
            [sys.executable, "-B", str(ROOT / "xash/tools/generate_static_library_tables.py"),
             str(out), f"a={exports}", f"a={exports}"],
            text=True, capture_output=True, check=False)
        assert duplicate.returncode != 0

    checked_in = ROOT / "xash" / "exports"
    for name in ("filesystem_stdio", "server"):
        exports = tables.parse_exports((checked_in / f"{name}.txt").read_text())
        assert exports, name
    assert tables.parse_exports((checked_in / "server.txt").read_text()) == [
        "GiveFnptrsToDll", "GetEntityAPI", "GetEntityAPI2"]
    print("static library table generator tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
