#!/usr/bin/env python3
"""Fail-closed validator for the Phase 5 GPU EOP/VideoOut timing gate."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from validate_phase4_render_state_evidence import (
    EvidenceError,
    fail,
    load,
    many,
    number,
    one,
)


FRAMES = 60_000
SAMPLE_FRAMES = (0, *range(599, FRAMES, 600))


def exact(row: dict[str, str], expected: dict[str, str]) -> bool:
    return all(row.get(key) == value for key, value in expected.items())


def validate(path: Path, *, bundle_sha256: str, bundle_bytes: int,
             studio_sha256: str, studio_bytes: int) -> dict[str, object]:
    messages, data = load(path.resolve())
    boot = one(messages, "BSP_TEXTURE_PATH_BOOT")
    if not exact(boot, {
        "schema": "1", "slice": "gpu-flip-timing", "target": "gfx1013",
        "fw": "12.02", "transient_slots": "2",
        "ownership": "fence+videoout",
        "gpu_timestamp": "eop-release-mem",
        "bundle_sha256": bundle_sha256, "studio_sha256": studio_sha256,
        "input_gate": "not-required",
    }) or number(boot, "bundle_bytes") != bundle_bytes or \
            number(boot, "studio_bytes") != studio_bytes or \
            number(boot, "soak_frames") != FRAMES:
        fail("GPU/flip timing boot contract mismatch")

    begin = one(messages, "GPU_FLIP_TIMING_BEGIN")
    if not exact(begin, {
        "schema": "1", "slots": "2", "gpu_clock_unit": "raw-ticks",
        "gpu_packet": "release_mem-data_sel_3",
        "packet_order": "draws,timestamp,setflip,ownership-fence",
        "cpu_clock": "monotonic", "latency_anchor": "submit-begin",
        "flip_observation": "exact-videoout-event",
    }) or number(begin, "frames") != FRAMES:
        fail("GPU/flip timing begin contract mismatch")

    loop = one(messages, "BSP_LOOP_BEGIN")
    if not exact(loop, {
        "mode": "gpu-flip-timing-soak", "buffers": "2",
        "color_dma": "false", "depth_dma": "true", "indexed": "true",
        "frames": "60000", "features": "phase4-final-integrated",
        "timestamp": "eop-release-mem", "camera": "locked-bsp-tree",
        "geometry": "world+entities", "descriptors": "per-frame",
        "overlay": "fixed", "retirement": "fence+videoout",
        "input_dependency": "none",
    }):
        fail("GPU/flip timing loop contract mismatch")

    samples = many(messages, "GPU_FLIP_TIMING_SAMPLE")
    if [number(row, "frame") for row in samples] != list(SAMPLE_FRAMES):
        fail("GPU/flip timing sampled frame sequence mismatch")
    previous_tick = 0
    for index, row in enumerate(samples):
        frame = number(row, "frame")
        submit = number(row, "cpu_submit_ns")
        gpu = number(row, "cpu_gpu_observed_ns")
        flip = number(row, "cpu_flip_observed_ns")
        tick = number(row, "gpu_eop_ticks")
        delta = number(row, "gpu_eop_delta_ticks")
        if row.get("schema") != "1" or number(row, "slot") != frame % 2 or \
                number(row, "token") == 0 or not submit <= gpu <= flip or \
                number(row, "submit_to_gpu_ns") != gpu - submit or \
                number(row, "submit_to_flip_ns") != flip - submit or \
                number(row, "gpu_to_flip_ns") != flip - gpu or tick == 0 or \
                (index == 0 and delta != 0) or \
                (index != 0 and (tick <= previous_tick or delta <= 0)) or \
                row.get("fence") != "zero" or \
                row.get("token_match") != "exact":
            fail("GPU/flip timing sample contract mismatch")
        previous_tick = tick

    summary = one(messages, "GPU_FLIP_TIMING_SUMMARY")
    if not exact(summary, {
        "schema": "1", "gpu_timestamp_regressions": "0",
        "cpu_order_errors": "0", "sequence_gaps": "0",
        "ownership": "fence+exact-videoout-event", "result": "pass",
    }) or number(summary, "frames") != FRAMES or \
            number(summary, "records") != FRAMES or \
            number(summary, "gpu_timestamp_writes") != FRAMES or \
            number(summary, "gpu_timestamp_changes") != FRAMES - 1 or \
            number(summary, "first_gpu_eop_ticks") <= 0 or \
            number(summary, "last_gpu_eop_ticks") <= \
                number(summary, "first_gpu_eop_ticks"):
        fail("GPU/flip timing summary contract mismatch")
    for prefix in ("submit_to_gpu", "submit_to_flip", "gpu_to_flip"):
        minimum = number(summary, f"{prefix}_min_ns")
        average = number(summary, f"{prefix}_avg_ns")
        maximum = number(summary, f"{prefix}_max_ns")
        if not 0 <= minimum <= average <= maximum:
            fail(f"invalid {prefix} latency range")

    complete = one(messages, "GOLDSRC_PHASE4_FINAL_COMPLETE")
    if number(complete, "frames") != FRAMES or \
            complete.get("ownership") != "fence+videoout" or \
            complete.get("tokens") != "exact" or \
            complete.get("guards") != "intact" or \
            number(complete, "errors") != 0:
        fail("integrated renderer completion mismatch")
    return {
        "run_id": json.loads(path.read_text(encoding="utf-8")).get("run_id"),
        "frames": FRAMES,
        "samples": len(samples),
        "records": number(summary, "records"),
        "first_gpu_eop_ticks": number(summary, "first_gpu_eop_ticks"),
        "last_gpu_eop_ticks": number(summary, "last_gpu_eop_ticks"),
        "submit_to_flip_avg_ns": number(summary, "submit_to_flip_avg_ns"),
        "log_sha256": hashlib.sha256(data).hexdigest(),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--bundle-sha256", required=True)
    parser.add_argument("--bundle-bytes", required=True, type=int)
    parser.add_argument("--studio-sha256", required=True)
    parser.add_argument("--studio-bytes", required=True, type=int)
    args = parser.parse_args()
    try:
        for label, value in (("bundle", args.bundle_sha256),
                             ("studio", args.studio_sha256)):
            if len(value) != 64 or any(char not in "0123456789abcdef"
                                       for char in value):
                fail(f"invalid expected {label} SHA-256")
        print(json.dumps(validate(
            args.manifest, bundle_sha256=args.bundle_sha256,
            bundle_bytes=args.bundle_bytes,
            studio_sha256=args.studio_sha256,
            studio_bytes=args.studio_bytes,
        ), sort_keys=True))
    except EvidenceError as exc:
        raise SystemExit(f"GPU/flip timing evidence rejected: {exc}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
