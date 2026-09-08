#!/usr/bin/env python3
from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    with tempfile.TemporaryDirectory() as temporary:
        directory = Path(temporary)
        exports = directory / "exports.txt"
        source = directory / "module.c"
        version_script = directory / "module.map"
        exports.write_text("GiveFnptrsToDll\nGetEntityAPI2\n", encoding="utf-8")
        result = subprocess.run(
            [sys.executable, "-B", str(ROOT / "xash/tools/generate_prx_descriptor.py"),
             "--module", "server", "--exports", str(exports),
             "--extra", "PS5_ServerPrxEngineTableMask",
             "--source", str(source), "--version-script", str(version_script)],
            text=True, capture_output=True, check=False)
        assert result.returncode == 0, result.stderr
        generated = source.read_text(encoding="utf-8")
        exports_map = version_script.read_text(encoding="utf-8")
        assert "__init_array_start" in generated
        assert "__init_array_end" in generated
        assert "ps5_prx_run_initializers( );" in generated
        assert "ps5_prx_run_finalizers( );" in generated
        assert "if( ps5_server_prx_started ) return 0;" in generated
        assert "if( !ps5_server_prx_started ) return 0;" in generated
        assert "return 2;" in generated
        assert "PS5_ServerPrxEngineTableMask;" in exports_map
        assert "server_prx_exports;" in exports_map
        duplicate = subprocess.run(
            [sys.executable, "-B", str(ROOT / "xash/tools/generate_prx_descriptor.py"),
             "--module", "server", "--exports", str(exports),
             "--extra", "Probe", "--extra", "Probe",
             "--source", str(source), "--version-script", str(version_script)],
            text=True, capture_output=True, check=False)
        assert duplicate.returncode != 0
    print("PRX descriptor generator tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
