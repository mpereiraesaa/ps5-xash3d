#!/usr/bin/env python3
"""Fail-closed audit for the standalone public repository."""

from __future__ import annotations

import ipaddress
import re
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ALLOWLIST = ROOT / "PUBLICATION_ALLOWLIST.txt"
TEXT_SUFFIXES = {
    "", ".c", ".cpp", ".example", ".h", ".json", ".map", ".md",
    ".pipe", ".py", ".s", ".sh", ".txt", ".yml", ".cfg",
}
PINNED_BINARY_SHA256 = {
    "assets/branding/icon-master.png":
        "4a66332b800fa0653645d95c627697f72ca4e26cf19fbeb2d9184ec39d089837",
    "assets/screenshots/half-life-mainui.png":
        "7feb13b43d4e175fa67e1f76d6f0ccfeeb801c944d061d4ac5ba3ddaff5e5450",
    "sce_sys/icon0.png":
        "244c67fd7147267425ce66b5dcf6031bdc9e2f373a04d958758fae847955bde5",
}
GENERATED_ROOTS = {".deps", "build", "dist", "release"}
FORBIDDEN_PARTS = {"captures", "dumps", "ghidra", "sessions"}
FORBIDDEN_TERMS = (
    "/home/" + "manuel/",
    "/data/homebrew/" + "PPSA99998",
    "authorized-graphics-" + "contract",
    "stage-e-live-" + "last.log",
)


def fail(message: str) -> None:
    raise SystemExit(f"publication audit failed: {message}")


def submodule_paths() -> set[str]:
    """Pinned upstream trees declared in .gitmodules are audited upstream."""
    modules = ROOT / ".gitmodules"
    if not modules.exists():
        return set()
    paths = set()
    for line in modules.read_text(encoding="utf-8").splitlines():
        key, _, value = line.strip().partition("=")
        if key.strip() == "path":
            paths.add(value.strip())
    return paths


def main() -> int:
    submodules = submodule_paths()
    for name in GENERATED_ROOTS:
        path = ROOT / name
        if path.exists():
            ignored = subprocess.run(
                ["git", "check-ignore", "--quiet", "--", name],
                cwd=ROOT, check=False,
            )
            if ignored.returncode != 0:
                fail(f"generated path is not git-ignored: {name}")
    allowed = {
        line.strip() for line in ALLOWLIST.read_text().splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    }
    observed: set[str] = set()
    for path in ROOT.rglob("*"):
        relative = path.relative_to(ROOT)
        if ".git" in relative.parts:
            continue
        if relative.parts and relative.parts[0] in GENERATED_ROOTS:
            continue
        if any("/".join(relative.parts[:depth]) in submodules
               for depth in range(1, len(relative.parts))):
            continue
        if any(part in FORBIDDEN_PARTS or part == "__pycache__"
               for part in relative.parts):
            fail(f"forbidden path: {relative}")
        if path.is_symlink():
            fail(f"symlink is not publishable: {relative}")
        if not path.is_file():
            continue
        name = relative.as_posix()
        observed.add(name)
        if name not in allowed:
            fail(f"file is not allowlisted: {name}")
        if name in PINNED_BINARY_SHA256:
            import hashlib
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            if digest != PINNED_BINARY_SHA256[name]:
                fail(f"binary digest changed: {name}")
            continue
        if path.suffix.lower() not in TEXT_SUFFIXES:
            fail(f"non-text file is not approved: {name}")
        try:
            text = path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            fail(f"non-UTF-8 content: {name}")
        for term in FORBIDDEN_TERMS:
            if term in text:
                fail(f"private term in {name}: {term}")
        for candidate in re.findall(r"(?<![\w.])(?:\d{1,3}\.){3}\d{1,3}(?![\w.])", text):
            try:
                address = ipaddress.ip_address(candidate)
            except ValueError:
                continue
            if address.is_private and not address.is_loopback:
                fail(f"private IP address in {name}")
    missing = allowed - observed
    if missing:
        fail(f"allowlisted files missing: {', '.join(sorted(missing))}")
    print(f"publication audit passed: {len(observed)} allowlisted files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
