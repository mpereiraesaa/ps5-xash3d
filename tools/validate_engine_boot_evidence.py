#!/usr/bin/env python3
"""Fail-closed validation for a Xash3D dedicated engine boot ps5log/1 run.

The engine boot title captures stdio into the telemetry stream, so the
transcript mixes structured records (seq, mono_ns, level, text) with RAW
console lines. Structured records must stay contiguous and free of ERROR
levels; RAW lines are the engine console and are searched for the boot,
filesystem, spawn and bounded-quit proofs.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path


class EvidenceError(RuntimeError):
    pass


TITLE = "PPSA99996"
APP = "xash3d-engine"
BYE_REASON = "xash-engine-boot-complete"
HEX7 = re.compile(r"[0-9a-f]{7}")
FATAL_CONSOLE = ("Host_Error:", "Sys_Error:", "FS_Init: couldn't", "couldn't determine current directory")


def fail(message: str) -> None:
    raise EvidenceError(message)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def parse_fields(message: str) -> dict[str, str]:
    fields: dict[str, str] = {}
    for token in message.split()[1:]:
        if "=" not in token:
            fail(f"malformed marker field: {token}")
        key, value = token.split("=", 1)
        fields[key] = value
    return fields


def one(messages: list[str], prefix: str) -> dict[str, str]:
    matches = [message for message in messages if message.startswith(prefix + " ")]
    if len(matches) != 1:
        fail(f"expected exactly one {prefix}, found {len(matches)}")
    return parse_fields(matches[0])


def split_transcript(lines: list[str]) -> tuple[list[tuple[int, str, str]], list[str]]:
    structured: list[tuple[int, str, str]] = []
    raw: list[str] = []
    for line in lines:
        parts = line.split("\t", 3)
        if len(parts) == 4:
            try:
                seq = int(parts[0], 10)
                int(parts[1], 10)
            except ValueError:
                raw.append(line)
                continue
            structured.append((seq, parts[2].strip() or "INFO", parts[3]))
        else:
            raw.append(line)
    return structured, raw


def validate(
    manifest_path: Path,
    *,
    engine_commit: str,
    hlsdk_commit: str,
    boot_map: str,
) -> dict[str, object]:
    manifest_path = manifest_path.resolve()
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        fail(f"invalid manifest: {exc}")

    identity = manifest.get("identity")
    if not isinstance(identity, dict):
        fail("manifest identity missing")
    if identity.get("title") != TITLE or identity.get("app") != APP:
        fail("title/app identity mismatch")
    if manifest.get("protocol") != "ps5log/1" or manifest.get("transport") != "tcp":
        fail("protocol/transport mismatch")
    if not all(manifest.get(key) for key in ("hello", "bye", "clean")):
        fail("run lacks clean HELLO/BYE completion")
    if manifest.get("gaps") != []:
        fail("run has sequence gaps")
    if manifest.get("oversized_lines") != 0:
        fail("run has oversized records")

    log_name = manifest.get("log_path")
    if not isinstance(log_name, str) or Path(log_name).name != log_name:
        fail("unsafe transcript path")
    log_path = (manifest_path.parent / log_name).resolve()
    if log_path.parent != manifest_path.parent:
        fail("transcript escaped manifest directory")
    try:
        data = log_path.read_bytes()
    except OSError as exc:
        fail(f"missing transcript: {exc}")
    if len(data) != manifest.get("bytes") or sha256(data) != manifest.get("sha256"):
        fail("transcript size/hash mismatch")

    lines = data.decode("utf-8", errors="replace").splitlines()
    if not lines or not lines[0].startswith("HELLO ps5log/1 "):
        fail("HELLO line missing")
    expected_boot = str(identity.get("boot", ""))
    hello_fields = parse_fields("HELLO " + lines[0].split(" ", 2)[2])
    if hello_fields.get("title") != TITLE or hello_fields.get("app") != APP:
        fail("HELLO identity mismatch")
    if hello_fields.get("boot") != expected_boot:
        fail("HELLO boot mismatch")

    records, raw = split_transcript(lines[1:-1])
    if [record[0] for record in records] != list(range(1, len(records) + 1)):
        fail("transcript sequence is not contiguous")
    if not records:
        fail("transcript has no structured records")
    if len(records) != manifest.get("records") or records[-1][0] != manifest.get("last_seq"):
        fail("manifest record count/last sequence mismatch")
    if len(raw) != manifest.get("raw_lines"):
        fail("manifest raw line count mismatch")
    if any(level == "ERROR" for _, level, _ in records):
        fail("transcript contains ERROR records")
    messages = [message for _, _, message in records]

    if f"LOG_BOOT_MONOTONIC_NS={expected_boot}" not in messages:
        fail("structured boot token mismatch")
    bye_fields = manifest.get("bye_fields")
    if not isinstance(bye_fields, dict):
        fail("manifest BYE fields missing")
    expected_bye = f"BYE seq={records[-1][0]} reason={BYE_REASON}"
    if lines[-1] != expected_bye or bye_fields.get("reason") != BYE_REASON:
        fail("BYE reason/sequence mismatch")

    boot = one(messages, "XASH_BOOT")
    if boot.get("schema") != "1" or boot.get("slice") != "engine-boot" or boot.get("mode") != "dedicated":
        fail("engine boot schema/slice/mode mismatch")
    if boot.get("engine") != engine_commit or boot.get("hlsdk") != hlsdk_commit:
        fail("engine/hlsdk commit mismatch")
    if boot.get("map") != boot_map:
        fail("boot map mismatch")
    if boot.get("basedir") in (None, "none"):
        fail("no writable base directory was selected")
    if boot.get("rodir_present") != "1":
        fail("read-only game data was not present under the title")
    gate_seconds = boot.get("gate_seconds")
    if gate_seconds is None or int(gate_seconds, 10) <= 0:
        fail("boot gate is not bounded")

    exit_fields = one(messages, "XASH_EXIT")
    if exit_fields.get("result") != "0":
        fail("engine returned a non-zero result")

    console = "\n".join(raw)
    for needle in FATAL_CONSOLE:
        if needle in console:
            fail(f"console reports a fatal error: {needle}")
    proofs = {
        "filesystem": "filesystem_stdio successfully loaded",
        "spawn": f"Spawn Server: {boot_map}",
        "bounded_quit": f"PS5_XASH_GATE_TIMEOUT seconds={gate_seconds} action=quit",
    }
    for name, needle in proofs.items():
        if needle not in console:
            fail(f"console proof missing: {name}")

    return {
        "run_id": manifest.get("run_id"),
        "records": len(records),
        "raw_lines": len(raw),
        "log_sha256": sha256(data),
        "engine": boot["engine"],
        "hlsdk": boot["hlsdk"],
        "map": boot["map"],
        "basedir": boot["basedir"],
        "gate_seconds": int(gate_seconds, 10),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--engine-commit", required=True)
    parser.add_argument("--hlsdk-commit", required=True)
    parser.add_argument("--map", default="c1a0")
    args = parser.parse_args()
    for value in (args.engine_commit, args.hlsdk_commit):
        if not HEX7.fullmatch(value):
            parser.error("commits must be 7 lowercase hex digits")
    try:
        summary = validate(
            args.manifest,
            engine_commit=args.engine_commit,
            hlsdk_commit=args.hlsdk_commit,
            boot_map=args.map,
        )
    except EvidenceError as exc:
        raise SystemExit(f"engine boot evidence validation failed: {exc}") from exc
    print(json.dumps(summary, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
