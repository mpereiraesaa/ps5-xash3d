#!/usr/bin/env python3
import importlib.util
import json
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "xash" / "tools" / "audit_dyn_imports.py"
sys.dont_write_bytecode = True


def load_module():
    spec = importlib.util.spec_from_file_location("audit_dyn_imports", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def main() -> int:
    audit = load_module()
    sample = """
      1: 0000000000000000 0 FUNC GLOBAL DEFAULT UND strcasecmp
      2: 0000000000000000 0 FUNC GLOBAL DEFAULT UND strcasestr
      3: 0000000000000000 0 FUNC GLOBAL DEFAULT UND mystery@LIBC
    """
    assert audit.parse_imports(sample) == ["mystery", "strcasecmp", "strcasestr"]
    assert audit.parse_exports(sample.replace(" UND ", " 5 ")) == {
        "mystery", "strcasecmp", "strcasestr"
    }
    assert audit.parse_needed("NeededLibraries [\n libSceLibcInternal.sprx\n]") == {
        "libSceLibcInternal.so"
    }
    evidence = json.loads((ROOT / "xash" / "ps5_import_evidence.json").read_text())
    rows = audit.classify(audit.parse_imports(sample), evidence)
    assert rows[0][1] == "EXPORTED ONLY"
    assert rows[1][1] == "HW PASS"
    assert rows[2][1] == "BANNED"
    with tempfile.TemporaryDirectory() as temp:
        report = Path(temp) / "report.md"
        providers = {"strcasecmp": ["libSceLibcInternal.so"],
                     "strcasestr": ["libScePosixForWebKit.so"]}
        report.write_text(audit.render(rows, Path("test.elf"), providers), encoding="utf-8")
        text = report.read_text(encoding="utf-8")
        assert "EXPORTED ONLY" in text
        assert "BANNED" in text
        assert "Run 20260907T162442485Z" in text
        assert "libSceLibcInternal.so" in text
        assert "libScePosixForWebKit.so" in text
    print("dynamic import audit tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
