#!/usr/bin/env python3
"""Classify every dynamic import without treating an exported stub as proof."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
from pathlib import Path


UND = re.compile(r"\bUND\s+(\S+)")


def parse_imports(text: str) -> list[str]:
    symbols = set()
    for line in text.splitlines():
        match = UND.search(line)
        if match:
            symbols.add(match.group(1).split("@", 1)[0])
    return sorted(symbols)


def parse_exports(text: str) -> set[str]:
    symbols = set()
    for line in text.splitlines():
        fields = line.split()
        if len(fields) >= 8 and fields[-2] != "UND" and fields[-1] != "Name":
            symbols.add(fields[-1].split("@", 1)[0])
    return symbols


def parse_needed(text: str) -> set[str]:
    return {
        line.strip().removesuffix(".sprx") + ".so"
        for line in text.splitlines()
        if line.strip().endswith(".sprx")
    }


def find_providers(stub_dir: Path, readelf: str, imports: list[str],
                   needed_stubs: set[str]) -> dict[str, list[str]]:
    wanted = set(imports)
    providers = {symbol: [] for symbol in imports}
    for stub in sorted(stub_dir.glob("*.so")):
        if stub.name not in needed_stubs:
            continue
        output = subprocess.run(
            [readelf, "--dyn-syms", str(stub)],
            check=True,
            text=True,
            stdout=subprocess.PIPE,
        ).stdout
        for symbol in parse_exports(output) & wanted:
            providers[symbol].append(stub.name)
    return providers


def classify(symbols: list[str], evidence: dict) -> list[tuple[str, str, str]]:
    groups = (
        ("banned", "BANNED"),
        ("hardware_fail_guarded", "HW FAIL / GUARDED"),
        ("hardware_pass", "HW PASS"),
    )
    rows = []
    for symbol in symbols:
        for key, label in groups:
            if symbol in evidence.get(key, {}):
                rows.append((symbol, label, evidence[key][symbol]))
                break
        else:
            rows.append((symbol, "EXPORTED ONLY", "No symbol-specific hardware evidence yet"))
    return rows


def render(rows: list[tuple[str, str, str]], elf: Path,
           providers: dict[str, list[str]] | None = None) -> str:
    counts: dict[str, int] = {}
    for _, status, _ in rows:
        counts[status] = counts.get(status, 0) + 1
    summary = ", ".join(f"{key}={counts[key]}" for key in sorted(counts))
    lines = [
        "# PS5 dynamic-import audit",
        "",
        f"ELF: `{elf}`",
        "",
        f"Imports: {len(rows)} ({summary}).",
        "",
        "`EXPORTED ONLY` is deliberately not a pass. Promote a symbol only after",
        "a representative FW smoke test and an immutable evidence reference.",
        "",
        "Provider names come from the linked SDK stubs; they identify routing,",
        "not runtime correctness.",
        "",
        "| Symbol | SDK provider | Status | Evidence / restriction |",
        "| --- | --- | --- | --- |",
    ]
    providers = providers or {}
    for symbol, status, reason in rows:
        names = providers.get(symbol, [])
        provider = ", ".join(f"`{name}`" for name in names) if names else "unmapped"
        lines.append(f"| `{symbol}` | {provider} | {status} | {reason} |")
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("elf", type=Path)
    parser.add_argument("--readelf", default="llvm-readelf")
    parser.add_argument("--evidence", type=Path, required=True)
    parser.add_argument("--stub-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = subprocess.run(
        [args.readelf, "--dyn-syms", str(args.elf)],
        check=True,
        text=True,
        stdout=subprocess.PIPE,
    ).stdout
    needed_output = subprocess.run(
        [args.readelf, "--needed-libs", str(args.elf)],
        check=True,
        text=True,
        stdout=subprocess.PIPE,
    ).stdout
    evidence = json.loads(args.evidence.read_text(encoding="utf-8"))
    rows = classify(parse_imports(output), evidence)
    providers = find_providers(
        args.stub_dir,
        args.readelf,
        [row[0] for row in rows],
        parse_needed(needed_output),
    )
    banned = [symbol for symbol, status, _ in rows if status == "BANNED"]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(render(rows, args.elf, providers), encoding="utf-8")
    print(f"dynamic import audit: {len(rows)} imports, {len(banned)} banned")
    if banned:
        print("banned dynamic imports: " + ", ".join(banned))
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
