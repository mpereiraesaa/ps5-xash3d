#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import json
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
VALIDATOR = ROOT / "tools" / "validate_phase4_render_state_evidence.py"
BUNDLE_SHA = "7" * 64


def make_evidence(directory: Path) -> Path:
    messages = [
        "LOG_BOOT_MONOTONIC_NS=0x1234",
        f"BSP_TEXTURE_PATH_BOOT schema=1 slice=dynamic-lightmap target=gfx1013 fw=12.02 transient_slots=2 ownership=fence+videoout bundle_sha256={BUNDLE_SHA} bundle_bytes=42 soak_frames=10000",
        "GOLDSRC_VIEWPORT_READY schema=1 framebuffer=1920x1080 inset_viewport=320,180+1280x720 inset_scissor=400,220+1120x640 restore=full-frame registers=8",
        "GOLDSRC_PIPELINES_READY schema=1 semantic_permutations=99 shader_variants=9 native_slots=9 base_depth=000000b6 base_raster=00000240 state_register_bytes=2376 target=gfx1013",
        "GOLDSRC_VIEWPORT_FRAME schema=1 frame=0 slot=0 sequence=full-clear,inset-opaque,inset-alpha,full-restore inset_scissor_tl=80dc0190 inset_scissor_br=035c05f0 full_scissor_tl=80000000 full_scissor_br=04380780",
        "GOLDSRC_STATE_FRAME schema=1 frame=0 slot=0 opaque_key=68 opaque_pass=0 opaque_shader=surface_lightmap opaque_blend=00000000 opaque_depth=000000b6 opaque_raster=00000240 alpha_key=71 alpha_pass=1 alpha_shader=masked_lightmap alpha_blend=00000000 alpha_depth=000000b6 alpha_raster=00000240",
        "RESOURCE_FRAME_READY frame=0 slot=0 transient_bytes=1",
        "RESOURCE_FRAME_SEALED frame=0 slot=0 token=100 retirement=fence+videoout",
        "RESOURCE_FRAME_SUBMITTED frame=0 slot=0 token=100 command_dwords=1",
        "RESOURCE_FRAME_RETIRED frame=0 slot=0 token=100 fence=zero videoout_token=exact",
        "BSP_VIDEOOUT_TOKEN frame=0 buffer=0 expected=100 observed=100 exact=true",
        "RESOURCE_FRAME_READY frame=9999 slot=1 transient_bytes=1",
        "RESOURCE_FRAME_SEALED frame=9999 slot=1 token=10099 retirement=fence+videoout",
        "RESOURCE_FRAME_SUBMITTED frame=9999 slot=1 token=10099 command_dwords=1",
        "RESOURCE_FRAME_RETIRED frame=9999 slot=1 token=10099 fence=zero videoout_token=exact",
        "BSP_VIDEOOUT_TOKEN frame=9999 buffer=1 expected=10099 observed=10099 exact=true",
        "BSP_FRAME frame=9999 completed=10000 compose_avg_ns=1 gpu_wait_avg_ns=1 video_wait_avg_ns=1 present_interval_avg_ns=1 present_interval_max_ns=1 present_interval_over_budget=3 errors=0 terminal=0",
        "RESOURCE_RING_RETIRED slots=2 reusable=true tokens=exact last_token=10099",
        "BSP_RESOURCE_READBACK buffer0=1111111111111111 buffer1=2222222222222222 bytes=64 bright_pixels0=1 bright_pixels1=2 guards=intact frames=10000 errors=0 overlay=transient",
        "DYNAMIC_LIGHTMAP_READBACK pattern0=3333333333333333 pattern1=4444444444444444 gpu_buffer0=1111111111111111 gpu_buffer1=2222222222222222 buffers_distinct=true surrounding=stable guards=intact frames=10000",
        "GOLDSRC_PIPELINE_GATE_COMPLETE schema=1 frames=10000 semantic_permutations=99 shader_variants=9 opaque_key=68 opaque_shader=surface_lightmap alpha_key=71 alpha_shader=masked_lightmap framebuffer_distinct=true input_required=false tokens=exact guards=intact errors=0",
        "GOLDSRC_VIEWPORT_GATE_COMPLETE schema=1 frames=10000 sequence=full-clear,inset-opaque,inset-alpha,full-restore framebuffer_distinct=true input_required=false tokens=exact guards=intact errors=0",
        "RESOURCE_POOL_RETIRED token=10099 reclaimed=6 completion=fence+videoout",
    ]
    rows = []
    for index, message in enumerate(messages, 1):
        level = "INFO" if message.startswith("BSP_FRAME ") else "MARK"
        rows.append(f"{index}\t{index}\t{level}\t{message}")
    payload = "\n".join([
        "HELLO ps5log/1 title=PPSA99996 app=ps5-xash3d boot=0x1234 tag=test",
        *rows,
        f"BYE seq={len(rows)} reason=bsp-texture-path-lightmap-soak-complete",
        "",
    ]).encode()
    (directory / "synthetic.log").write_bytes(payload)
    manifest = {
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
        "--require-viewport",
    ], text=True, capture_output=True, check=False)


def main() -> int:
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        path = make_evidence(root)
        valid = run(path)
        assert valid.returncode == 0, valid.stderr
        log = root / "synthetic.log"
        changed = log.read_text().replace(
            "GOLDSRC_VIEWPORT_GATE_COMPLETE schema=1",
            "GOLDSRC_VIEWPORT_GATE_COMPLETE schema=2")
        log.write_text(changed)
        manifest = json.loads(path.read_text())
        data = log.read_bytes()
        manifest["bytes"] = len(data)
        manifest["sha256"] = hashlib.sha256(data).hexdigest()
        path.write_text(json.dumps(manifest))
        assert run(path).returncode != 0
    print("Phase 4 render-state evidence validator tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
