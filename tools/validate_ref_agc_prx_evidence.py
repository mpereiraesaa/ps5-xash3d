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
    phase7 = any(message.startswith("REF_AGC_LIVE_COMPLETE ")
                 for _, _, message in records)
    reason = "ref-agc-live-complete" if phase7 \
        else "ref-agc-runtime-complete"
    expected_bye = f"BYE seq={records[-1][0]} reason={reason}"
    bye_fields = manifest.get("bye_fields")
    if lines[-1] != expected_bye or not isinstance(bye_fields, dict) \
            or bye_fields.get("seq") != str(records[-1][0]) \
            or bye_fields.get("reason") != reason:
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
    if boot.get("slice") == "phase7-live-consumer":
        if not exact(boot, {
            "schema": "1", "slice": "phase7-live-consumer",
            "target": "gfx1013", "fw": "12.02",
            "ownership": "fence+videoout+ack",
            "bundle_sha256": bundle_sha256,
            "studio_sha256": studio_sha256,
            "lifetime": "engine-owned", "input_owner": "engine",
        }) or int(boot.get("bundle_bytes", "0"), 10) != bundle_bytes \
                or int(boot.get("studio_bytes", "0"), 10) != studio_bytes:
            fail("Phase 7 renderer boot/assets mismatch")
        loop = one(messages, "BSP_LOOP_BEGIN")
        if not exact(loop, {
            "mode": "phase7-live-consumer", "buffers": "2",
            "color_dma": "false", "depth_dma": "true",
            "indexed": "true", "frames": "engine-owned",
            "camera": "live-refapi", "geometry": "baked-c1a0",
            "lists": "world+entities+2d",
            "retirement": "fence+videoout+ack",
            "input_dependency": "engine",
        }):
            fail("Phase 7 live integration loop mismatch")
        ready = one(messages, "REF_AGC_RUNTIME_READY")
        if not exact(ready, {
            "backend": "phase4-native", "api": "18",
            "videoout": "owned", "direct_memory": "owned",
            "agc": "initialized", "scene": "planned",
        }):
            fail("Phase 7 native runtime was not ready")
        inputs = [parse_fields(message) for message in messages
                  if message.startswith("REF_AGC_LIVE_FRAME_INPUT ")]
        consumed = [parse_fields(message) for message in messages
                    if message.startswith("REF_AGC_LIVE_CONSUMED ")]
        if not inputs or not consumed:
            fail("Phase 7 live frame/ACK evidence is missing")
        for marker in consumed:
            if marker.get("ack") != "exact" or marker.get("drops") != "zero" \
                    or marker.get("serial") != marker.get("consumed"):
                fail("Phase 7 live frame ACK contract mismatch")
        complete = one(messages, "REF_AGC_LIVE_COMPLETE")
        frames = int(complete.get("frames", "0"), 10)
        views = int(complete.get("view_frames", "0"), 10)
        bright = int(complete.get("bright_pixels", "0"), 10)
        if frames <= 0 or views <= 0 or views > frames \
                or complete.get("serial") != str(frames) \
                or complete.get("camera_hash") in (None, "0000000000000000") \
                or complete.get("buffer0") in (None, "0000000000000000") \
                or complete.get("buffer1") in (None, "0000000000000000") \
                or bright <= 0 or complete.get("resource_reclaimed") != "6" \
                or complete.get("ownership") != "fence+videoout+ack" \
                or complete.get("guards") != "intact" \
                or complete.get("errors") != "0":
            fail("Phase 7 live renderer completion mismatch")
        teardown = one(messages, "REF_AGC_TEARDOWN")
        if not exact(teardown, {
            "videoout": "closed", "direct_memory": "released",
            "agc": "unloaded", "result": "0", "ownership": "exact",
        }):
            fail("Phase 7 native teardown mismatch")
        return {
            "run_id": manifest.get("run_id"),
            "log_sha256": hashlib.sha256(data).hexdigest(),
            "phase": 7, "frames": frames, "view_frames": views,
            "camera_hash": complete["camera_hash"],
            "buffer0": complete["buffer0"],
            "buffer1": complete["buffer1"],
            "bright_pixels": bright,
        }
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
        "phase": 6, "frames": 600,
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
        engine_phase = 7 if engine["ref_agc_consumed_frames"] > 0 else 6
        if engine_phase != renderer["phase"]:
            fail("engine/renderer phase mismatch")
        if renderer["phase"] == 7:
            frames = renderer["frames"]
            views = renderer["view_frames"]
            camera_hash = renderer["camera_hash"]
            if engine["ref_agc_frames"] != frames \
                    or engine["ref_agc_live_frames"] != frames \
                    or engine["ref_agc_consumed_frames"] != frames \
                    or engine["ref_agc_consumed_serial"] != frames \
                    or engine["ref_agc_live_view_frames"] != views \
                    or engine["ref_agc_consumed_view_frames"] != views \
                    or engine["ref_agc_consumed_camera_hash"] != camera_hash \
                    or engine["ref_agc_bright_pixels"] != \
                    renderer["bright_pixels"]:
                fail("Phase 7 engine/renderer consumer accounting mismatch")
        skew = abs((started_at(args.engine_manifest.resolve())
                    - started_at(args.renderer_manifest.resolve())).total_seconds())
        if skew > 1.0:
            fail(f"engine/renderer start skew is {skew:.3f}s, expected <= 1s")
        summary = {
            "engine_run_id": engine["run_id"],
            "renderer_run_id": renderer["run_id"],
            "start_skew_ms": round(skew * 1000),
            "frames": renderer["frames"],
            "phase": renderer["phase"],
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
