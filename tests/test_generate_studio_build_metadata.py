#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def main() -> None:
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        bundle = root / "model.ps5mdl"
        output = root / "studio_build_metadata.h"
        bundle.write_bytes(b"private-studio-bundle")
        subprocess.run([
            "python3", str(ROOT / "tools/generate_studio_build_metadata.py"),
            "--bundle", str(bundle), "--output", str(output),
        ], check=True)
        text = output.read_text(encoding="utf-8")
        digest = hashlib.sha256(bundle.read_bytes()).hexdigest()
        assert f'#define PS5_STUDIO_BUNDLE_SHA256 "{digest}"' in text
        assert "#define PS5_STUDIO_BUNDLE_BYTES 21u" in text


if __name__ == "__main__":
    main()
