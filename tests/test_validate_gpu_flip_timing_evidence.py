#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import json
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
VALIDATOR = ROOT / "tools" / "validate_gpu_flip_timing_evidence.py"
BUNDLE_SHA = "7" * 64
STUDIO_SHA = "8" * 64


def make_evidence(directory: Path) -> Path:
    samples = []
    frames = (0, *range(599, 60_000, 600))
    for index, frame in enumerate(frames):
        submit = 1_000_000 + frame * 20_000
        gpu = submit + 1_000
        flip = submit + 16_000
        samples.append(
            "GPU_FLIP_TIMING_SAMPLE schema=1 "
            f"frame={frame} slot={frame % 2} token={100 + frame} "
            f"cpu_submit_ns={submit} cpu_gpu_observed_ns={gpu} "
            f"cpu_flip_observed_ns={flip} submit_to_gpu_ns=1000 "
            "submit_to_flip_ns=16000 gpu_to_flip_ns=15000 "
            f"gpu_eop_ticks={10_000 + index * 100} "
            f"gpu_eop_delta_ticks={0 if index == 0 else 100} "
            "fence=zero token_match=exact"
        )
    messages = [
        "LOG_BOOT_MONOTONIC_NS=0x1234",
        "BSP_TEXTURE_PATH_BOOT schema=1 slice=gpu-flip-timing "
        "target=gfx1013 fw=12.02 transient_slots=2 "
        "ownership=fence+videoout gpu_timestamp=eop-release-mem "
        f"bundle_sha256={BUNDLE_SHA} bundle_bytes=42 "
        f"studio_sha256={STUDIO_SHA} studio_bytes=93952 "
        "soak_frames=60000 input_gate=not-required",
        "GPU_FLIP_TIMING_BEGIN schema=1 frames=60000 slots=2 "
        "gpu_clock_unit=raw-ticks gpu_packet=release_mem-data_sel_3 "
        "packet_order=draws,timestamp,setflip,ownership-fence "
        "cpu_clock=monotonic latency_anchor=submit-begin "
        "flip_observation=exact-videoout-event",
        "BSP_LOOP_BEGIN mode=gpu-flip-timing-soak buffers=2 "
        "color_dma=false depth_dma=true indexed=true frames=60000 "
        "features=phase4-final-integrated timestamp=eop-release-mem "
        "camera=locked-bsp-tree geometry=world+entities "
        "descriptors=per-frame overlay=fixed retirement=fence+videoout "
        "input_dependency=none",
        *samples,
        "GPU_FLIP_TIMING_SUMMARY schema=1 frames=60000 records=60000 "
        "gpu_timestamp_writes=60000 gpu_timestamp_changes=59999 "
        "gpu_timestamp_regressions=0 cpu_order_errors=0 sequence_gaps=0 "
        "first_gpu_eop_ticks=10000 last_gpu_eop_ticks=20000 "
        "submit_to_gpu_min_ns=900 submit_to_gpu_avg_ns=1000 "
        "submit_to_gpu_max_ns=1100 submit_to_flip_min_ns=15000 "
        "submit_to_flip_avg_ns=16000 submit_to_flip_max_ns=17000 "
        "gpu_to_flip_min_ns=14000 gpu_to_flip_avg_ns=15000 "
        "gpu_to_flip_max_ns=16000 ownership=fence+exact-videoout-event "
        "result=pass",
        "GOLDSRC_PHASE4_FINAL_COMPLETE schema=1 frames=60000 "
        "ownership=fence+videoout tokens=exact guards=intact errors=0",
    ]
    rows = [f"{index}\t{index}\tMARK\t{message}"
            for index, message in enumerate(messages, 1)]
    payload = "\n".join([
        "HELLO ps5log/1 title=PPSA99996 app=ps5-xash3d "
        "boot=0x1234 tag=test",
        *rows,
        f"BYE seq={len(rows)} reason=gpu-flip-timing-soak-complete",
        "",
    ]).encode()
    (directory / "synthetic.log").write_bytes(payload)
    manifest = {
        "run_id": "synthetic-gpu-flip-timing",
        "identity": {"title": "PPSA99996", "app": "ps5-xash3d",
                     "boot": "0x1234"},
        "protocol": "ps5log/1", "transport": "tcp", "hello": True,
        "bye": True, "clean": True, "gaps": [], "raw_lines": 0,
        "oversized_lines": 0, "records": len(rows), "last_seq": len(rows),
        "log_path": "synthetic.log", "bytes": len(payload),
        "sha256": hashlib.sha256(payload).hexdigest(),
    }
    path = directory / "synthetic.json"
    path.write_text(json.dumps(manifest), encoding="utf-8")
    return path


def run(path: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run([
        "python3", str(VALIDATOR), str(path),
        "--bundle-sha256", BUNDLE_SHA, "--bundle-bytes", "42",
        "--studio-sha256", STUDIO_SHA, "--studio-bytes", "93952",
    ], text=True, capture_output=True, check=False)


def rewrite(path: Path, old: str, new: str) -> None:
    manifest = json.loads(path.read_text(encoding="utf-8"))
    log = path.parent / manifest["log_path"]
    log.write_text(log.read_text(encoding="utf-8").replace(old, new),
                   encoding="utf-8")
    payload = log.read_bytes()
    manifest["bytes"] = len(payload)
    manifest["sha256"] = hashlib.sha256(payload).hexdigest()
    path.write_text(json.dumps(manifest), encoding="utf-8")


def main() -> int:
    with tempfile.TemporaryDirectory() as temporary:
        path = make_evidence(Path(temporary))
        valid = run(path)
        assert valid.returncode == 0, valid.stderr
        rewrite(path, "gpu_timestamp_changes=59999",
                "gpu_timestamp_changes=59998")
        invalid = run(path)
        assert invalid.returncode != 0
    print("GPU/flip timing evidence validator tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
