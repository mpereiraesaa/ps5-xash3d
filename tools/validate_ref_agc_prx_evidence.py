#!/usr/bin/env python3
"""Fail-closed validation for the paired Phase 6 ref_agc hardware run."""

from __future__ import annotations

import argparse
from datetime import datetime
import hashlib
import json
from pathlib import Path

from validate_engine_boot_evidence import (
    EvidenceError,
    fail,
    one,
    parse_fields,
    split_transcript,
    validate as validate_engine,
)


def load_renderer(manifest_path: Path) -> tuple[dict[str, object], list[str], bytes]:
    manifest_path = manifest_path.resolve()
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        fail(f"invalid renderer manifest: {exc}")
    identity = manifest.get("identity")
    if not isinstance(identity, dict) or identity.get("title") != "PPSA99996" \
            or identity.get("app") != "ps5-xash3d":
        fail("renderer title/app identity mismatch")
    if manifest.get("protocol") != "ps5log/1" or manifest.get("transport") != "tcp":
        fail("renderer protocol/transport mismatch")
    if not all(manifest.get(key) for key in ("hello", "bye", "clean")) \
            or manifest.get("gaps") != [] or manifest.get("oversized_lines") != 0:
        fail("renderer run is not a clean gap-free transcript")
    name = manifest.get("log_path")
    if not isinstance(name, str) or Path(name).name != name:
        fail("unsafe renderer transcript path")
    log_path = (manifest_path.parent / name).resolve()
    if log_path.parent != manifest_path.parent:
        fail("renderer transcript escaped manifest directory")
    data = log_path.read_bytes()
    if len(data) != manifest.get("bytes") \
            or hashlib.sha256(data).hexdigest() != manifest.get("sha256"):
        fail("renderer transcript size/hash mismatch")
    lines = data.decode("utf-8", errors="replace").splitlines()
    if not lines or not lines[0].startswith(
            "HELLO ps5log/1 title=PPSA99996 app=ps5-xash3d "):
        fail("renderer HELLO identity mismatch")
    records, raw = split_transcript(lines[1:-1])
    if raw:
        fail("renderer transcript contains unexpected RAW lines")
    if [record[0] for record in records] != list(range(1, len(records) + 1)) \
            or len(records) != manifest.get("records") \
            or records[-1][0] != manifest.get("last_seq"):
        fail("renderer structured sequence mismatch")
    if any(level in ("ERR", "ERROR") for _, level, _ in records):
        fail("renderer transcript contains ERROR records")
    expected_bye = f"BYE seq={records[-1][0]} reason=ref-agc-runtime-complete"
    if lines[-1] != expected_bye:
        fail("renderer BYE reason/sequence mismatch")
    return manifest, [message for _, _, message in records], data


def exact(fields: dict[str, str], expected: dict[str, str]) -> bool:
    return all(fields.get(key) == value for key, value in expected.items())


def validate_renderer(
    manifest_path: Path, *, bundle_sha256: str, bundle_bytes: int,
    studio_sha256: str, studio_bytes: int,
) -> dict[str, object]:
    manifest, messages, data = load_renderer(manifest_path)
    boot = one(messages, "BSP_TEXTURE_PATH_BOOT")
    if not exact(boot, {
        "schema": "1", "slice": "goldsrc-phase4-final", "target": "gfx1013",
        "fw": "12.02", "ownership": "fence+videoout",
        "bundle_sha256": bundle_sha256, "studio_sha256": studio_sha256,
        "soak_frames": "600", "input_gate": "not-required",
    }) or int(boot.get("bundle_bytes", "0"), 10) != bundle_bytes \
            or int(boot.get("studio_bytes", "0"), 10) != studio_bytes:
        fail("ref_agc renderer boot/assets mismatch")
    loop = one(messages, "BSP_LOOP_BEGIN")
    if not exact(loop, {
        "mode": "ref-agc-integration", "frames": "600",
        "combined_window": "0..599", "geometry": "phase4-baked-c1a0e",
        "retirement": "fence+videoout", "input_dependency": "none",
    }):
        fail("ref_agc integration loop mismatch")
    ready = one(messages, "REF_AGC_RUNTIME_READY")
    if not exact(ready, {
        "backend": "phase4-native", "api": "18", "videoout": "owned",
        "direct_memory": "owned", "agc": "initialized", "scene": "planned",
    }):
        fail("ref_agc native runtime was not ready")
    readback = one(messages, "BSP_RESOURCE_READBACK")
    if readback.get("frames") != "600" or readback.get("errors") != "0" \
            or readback.get("guards") != "intact" \
            or readback.get("buffer0") in (None, "0000000000000000") \
            or readback.get("buffer1") in (None, "0000000000000000") \
            or int(readback.get("bright_pixels0", "0"), 10) <= 0 \
            or int(readback.get("bright_pixels1", "0"), 10) <= 0:
        fail("ref_agc GPU readback mismatch")
    complete = one(messages, "GOLDSRC_PHASE4_FINAL_COMPLETE")
    if not exact(complete, {
        "schema": "1", "frames": "600", "combined_frames": "600",
        "readbacks": "2", "both_slots": "true", "tokens": "exact",
        "guards": "intact", "errors": "0", "ownership": "fence+videoout",
        "sprites": "true", "particles": "true", "cpu_skinning": "true",
        "brush_entities": "true", "water": "true", "glass": "true",
        "pvs": "true", "frustum": "true", "animation_changes": "true",
    }):
        fail("ref_agc Phase 4 composition did not complete")
    pool = one(messages, "RESOURCE_POOL_RETIRED")
    if pool.get("reclaimed") != "6" or pool.get("completion") != "fence+videoout":
        fail("ref_agc resource pool was not reclaimed")
    teardown = one(messages, "REF_AGC_TEARDOWN")
    if not exact(teardown, {
        "videoout": "closed", "direct_memory": "released", "agc": "unloaded",
        "result": "0", "ownership": "exact",
    }):
        fail("ref_agc native teardown mismatch")
    return {
        "run_id": manifest.get("run_id"),
        "log_sha256": hashlib.sha256(data).hexdigest(),
        "frames": 600,
        "buffer0": readback["buffer0"],
        "buffer1": readback["buffer1"],
        "bright_pixels": int(readback["bright_pixels0"], 10)
        + int(readback["bright_pixels1"], 10),
    }


def started_at(path: Path) -> datetime:
    manifest = json.loads(path.read_text(encoding="utf-8"))
    return datetime.fromisoformat(str(manifest["started_utc"]))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("engine_manifest", type=Path)
    parser.add_argument("renderer_manifest", type=Path)
    parser.add_argument("--engine-commit", required=True)
    parser.add_argument("--hlsdk-commit", required=True)
    parser.add_argument("--bundle-sha256", required=True)
    parser.add_argument("--bundle-bytes", required=True, type=int)
    parser.add_argument("--studio-sha256", required=True)
    parser.add_argument("--studio-bytes", required=True, type=int)
    args = parser.parse_args()
    try:
        engine = validate_engine(
            args.engine_manifest,
            engine_commit=args.engine_commit,
            hlsdk_commit=args.hlsdk_commit,
            boot_map="c1a0", mode="client", ref_agc_prx_gate=True,
        )
        renderer = validate_renderer(
            args.renderer_manifest,
            bundle_sha256=args.bundle_sha256, bundle_bytes=args.bundle_bytes,
            studio_sha256=args.studio_sha256, studio_bytes=args.studio_bytes,
        )
        skew = abs((started_at(args.engine_manifest.resolve())
                    - started_at(args.renderer_manifest.resolve())).total_seconds())
        if skew > 1.0:
            fail(f"engine/renderer start skew is {skew:.3f}s, expected <= 1s")
        summary = {
            "engine_run_id": engine["run_id"],
            "renderer_run_id": renderer["run_id"],
            "start_skew_ms": round(skew * 1000),
            "frames": renderer["frames"],
            "engine_frame_hash": engine["ref_agc_frame_hash"],
            "gpu_buffers": [renderer["buffer0"], renderer["buffer1"]],
            "gpu_bright_pixels": renderer["bright_pixels"],
            "ownership": "exact",
            "pass": True,
        }
    except (EvidenceError, OSError, ValueError, KeyError) as exc:
        raise SystemExit(f"ref_agc evidence validation failed: {exc}") from exc
    print(json.dumps(summary, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
