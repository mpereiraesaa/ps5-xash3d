#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import json
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
VALIDATOR = ROOT / "tools" / "validate_phase4_final_evidence.py"
BUNDLE_SHA = "7" * 64
STUDIO_SHA = "8" * 64


def make_evidence(directory: Path) -> Path:
    final_frames = []
    for index, (frame, slot) in enumerate(
            zip((59400, 59401, 59998, 59999), (0, 1, 0, 1))):
        final_frames.append(
            "GOLDSRC_PHASE4_FINAL_FRAME schema=1 "
            f"frame={frame} slot={slot} lighting=3 effects=3 studio=4 "
            "brush=4 visibility=3 world_draws=362 brush_draws=69 "
            "studio_draws=12 effect_draws=3 screen_draws=2 "
            f"dynamic_luxels=40 pose_hash={0x100 + index:016x} "
            f"transform_hash={0x200 + index:016x} "
            f"screen_layout_hash={0x300 + slot:016x} transient_bytes=90000"
        )
    messages = [
        "LOG_BOOT_MONOTONIC_NS=0x1234",
        "BSP_TEXTURE_PATH_BOOT schema=1 slice=goldsrc-phase4-final "
        "target=gfx1013 fw=12.02 transient_slots=2 "
        f"ownership=fence+videoout bundle_sha256={BUNDLE_SHA} "
        f"bundle_bytes=42 studio_sha256={STUDIO_SHA} studio_bytes=93952 "
        "soak_frames=60000 input_gate=not-required",
        "BSP_LOOP_BEGIN mode=goldsrc-phase4-final-soak buffers=2 "
        "color_dma=false depth_dma=true indexed=true frames=60000 "
        "features=lighting+2d+sprites+particles+studio+brush+pvs+frustum "
        "combined_window=59400..59999 camera=locked-bsp-tree "
        "geometry=world+entities descriptors=per-frame overlay=fixed "
        "retirement=fence+videoout input_dependency=none",
        "GOLDSRC_VISIBILITY_READY schema=1 planes=11746 nodes=1323 "
        "leaves=683 pvs_row_bytes=86 draw_refs=2610 world_first_face=0 "
        "world_face_count=1952 draw_bounds=3210 "
        "modes=control+pvs+frustum+combined hold_frames=600 "
        "camera=locked-bsp-tree ownership=fence+videoout",
        "GOLDSRC_PHASE4_SCENE_READY schema=1 water_entity=65 "
        "water_draws=35 water_indices=312 water_texture=water3 "
        "water_mode=alpha glass_entity=27 glass_draws=6 glass_indices=36 "
        "glass_texture=glass_med glass_mode=alpha source=real-bsp-brushes",
        "GOLDSRC_PHASE4_FINAL_READY schema=1 frames=60000 "
        "combined_window=59400..59999 semantic_permutations=99 "
        "shader_variants=9 "
        "components=lighting+2d+sprites+particles+studio+brush+pvs+frustum "
        "world_draws=1952 brush_draws=69 studio_draws=12 effect_draws=3 "
        "screen_draws=2 skinning=cpu transient=per-slot "
        "camera=locked-bsp-tree ownership=fence+videoout",
        *final_frames,
        "GOLDSRC_LIGHTING_FRAME schema=1 frame=59400 slot=0 mode=3 "
        "name=combined dynamic_luxels=40",
        "GOLDSRC_SPRITE_PARTICLE_DRAW schema=1 frame=59400 slot=0 mode=3 "
        "name=combined draws=3 indices=438",
        "GOLDSRC_STUDIO_DRAW schema=1 frame=59400 slot=0 mode=4 "
        "name=combined instances=3 draws=12 indices=846",
        "GOLDSRC_BRUSH_DRAW schema=1 frame=59400 slot=0 mode=4 "
        "name=combined instances=5 draws=69 indices=528",
        "GOLDSRC_VISIBILITY_DRAW schema=1 frame=59400 slot=0 mode=3 "
        "name=combined draws=362 pvs=on frustum=on",
        "GOLDSRC_2D_FRAME schema=1 frame=59999 slot=1 draws=2 indices=522",
        "GOLDSRC_PHASE4_FINAL_READBACK schema=1 frame=59400 slot=0 "
        "hash=1111111111111111 bright_pixels=20 world_draws=362 "
        "brush_draws=69 studio_draws=12 effect_draws=3 screen_draws=2 "
        "modes=3,3,4,4,3 fence=zero videoout_token=exact",
        "GOLDSRC_PHASE4_FINAL_READBACK schema=1 frame=59401 slot=1 "
        "hash=2222222222222222 bright_pixels=21 world_draws=362 "
        "brush_draws=69 studio_draws=12 effect_draws=3 screen_draws=2 "
        "modes=3,3,4,4,3 fence=zero videoout_token=exact",
        "RESOURCE_FRAME_READY frame=0 slot=0 transient_bytes=1",
        "RESOURCE_FRAME_SEALED frame=0 slot=0 token=100 "
        "retirement=fence+videoout",
        "RESOURCE_FRAME_SUBMITTED frame=0 slot=0 token=100 "
        "command_dwords=1",
        "RESOURCE_FRAME_RETIRED frame=0 slot=0 token=100 fence=zero "
        "videoout_token=exact",
        "BSP_VIDEOOUT_TOKEN frame=0 buffer=0 expected=100 observed=100 "
        "exact=true",
        "RESOURCE_FRAME_READY frame=59999 slot=1 transient_bytes=1",
        "RESOURCE_FRAME_SEALED frame=59999 slot=1 token=60099 "
        "retirement=fence+videoout",
        "RESOURCE_FRAME_SUBMITTED frame=59999 slot=1 token=60099 "
        "command_dwords=1",
        "RESOURCE_FRAME_RETIRED frame=59999 slot=1 token=60099 fence=zero "
        "videoout_token=exact",
        "BSP_VIDEOOUT_TOKEN frame=59999 buffer=1 expected=60099 "
        "observed=60099 exact=true",
        "BSP_FRAME frame=59999 completed=60000 compose_avg_ns=1 "
        "gpu_wait_avg_ns=1 video_wait_avg_ns=1 present_interval_avg_ns=1 "
        "present_interval_max_ns=1 present_interval_over_budget=0 errors=0 "
        "terminal=0",
        "RESOURCE_RING_RETIRED slots=2 reusable=true tokens=exact "
        "last_token=60099",
        "BSP_RESOURCE_READBACK buffer0=1111111111111111 "
        "buffer1=2222222222222222 bytes=64 bright_pixels0=20 "
        "bright_pixels1=21 guards=intact frames=60000 errors=0 "
        "overlay=transient",
        "DYNAMIC_LIGHTMAP_READBACK slot0=3333333333333333 "
        "slot1=3333333333333333 final_mode=combined slots_equal=true "
        "surrounding=stable guards=intact frames=60000",
        "GOLDSRC_PIPELINE_GATE_COMPLETE schema=1 frames=60000 "
        "semantic_permutations=99 shader_variants=9 opaque_key=68 "
        "opaque_shader=surface_lightmap alpha_key=71 "
        "alpha_shader=masked_lightmap framebuffer_distinct=true "
        "input_required=false tokens=exact guards=intact errors=0",
        "GOLDSRC_2D_COMPLETE schema=1 frames=60000 draws_per_frame=2 "
        "passes=alpha+additive projection=orthographic "
        "components=hud+console+menu+font atlas=procedural-rgba8 "
        "geometry=per-frame-transient tokens=exact guards=intact errors=0",
        "GOLDSRC_PHASE4_FINAL_COMPLETE schema=1 frames=60000 "
        "combined_frames=600 readbacks=2 both_slots=true world_draws=362 "
        "brush_draws=69 studio_draws=12 effect_draws=3 screen_draws=2 "
        "lightstyles=real-bsp-planes dynamic_lights=face-local-radial "
        "sprites=true particles=true cpu_skinning=true chrome=true "
        "brush_entities=true water=true glass=true pvs=true frustum=true "
        "transient=per-slot "
        "animation_changes=true ownership=fence+videoout tokens=exact "
        "guards=intact errors=0",
        "RESOURCE_POOL_RETIRED token=60099 reclaimed=6 "
        "completion=fence+videoout",
    ]
    rows = [f"{index}\t{index}\tMARK\t{message}"
            for index, message in enumerate(messages, 1)]
    payload = "\n".join([
        "HELLO ps5log/1 title=PPSA99996 app=ps5-xash3d "
        "boot=0x1234 tag=test",
        *rows,
        f"BYE seq={len(rows)} reason=goldsrc-phase4-final-soak-complete",
        "",
    ]).encode()
    (directory / "synthetic.log").write_bytes(payload)
    manifest = {
        "run_id": "synthetic-final",
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
        rewrite(path, "world_draws=362 brush_draws=69",
                "world_draws=363 brush_draws=69")
        invalid = run(path)
        assert invalid.returncode != 0
    print("Phase 4 final evidence validator tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
