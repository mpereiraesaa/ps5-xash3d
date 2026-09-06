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


def make_evidence(directory: Path, *, screen_2d: bool = False,
                  lighting: bool = False,
                  sprite_particles: bool = False) -> Path:
    matrix = (
        ("opaque", 68, 0, 1, 0, 0, 1, "surface_lightmap",
         "00000000", "000000b6", "00000240"),
        ("alpha", 65, 1, 0, 0, 0, 1, "surface_lightmap",
         "65010504", "000000b2", "00000240"),
        ("additive", 66, 2, 0, 0, 0, 1, "surface_lightmap",
         "61010104", "000000b2", "00000240"),
        ("alpha-test", 71, 3, 1, 0, 0, 1, "masked_lightmap",
         "00000000", "000000b6", "00000240"),
        ("depth-write-off", 64, 0, 0, 0, 0, 1, "surface_lightmap",
         "00000000", "000000b2", "00000240"),
        ("cull-front", 76, 0, 1, 1, 0, 1, "surface_lightmap",
         "00000000", "000000b6", "00000241"),
        ("cull-back", 84, 0, 1, 2, 0, 1, "surface_lightmap",
         "00000000", "000000b6", "00000242"),
        ("fog", 100, 0, 1, 0, 1, 1, "surface_lightmap_fog",
         "00000000", "000000b6", "00000240"),
        ("lightmap-off", 4, 0, 1, 0, 0, 0, "surface",
         "00000000", "000000b6", "00000240"),
    )
    matrix_frames = [
        f"GOLDSRC_STATE_MATRIX_FRAME schema=1 frame={index * 300} slot=0 "
        f"case={index} name={name} key={key} blend={blend} "
        f"depth_write={depth} cull={cull} fog={fog} lightmap={lightmap} "
        f"shader={shader} blend_cx={blend_cx} depth_cx={depth_cx} "
        f"raster_cx={raster_cx}"
        for index, (name, key, blend, depth, cull, fog, lightmap, shader,
                    blend_cx, depth_cx, raster_cx) in enumerate(matrix)
    ]
    matrix_readbacks = [
        f"GOLDSRC_STATE_MATRIX_READBACK schema=1 frame={index * 300 + slot} "
        f"slot={slot} case={index} name={item[0]} key={item[1]} "
        f"hash={0x100 + index * 2 + slot:016x} bright_pixels=1 "
        "fence=zero videoout_token=exact"
        for index, item in enumerate(matrix) for slot in range(2)
    ]
    lighting_names = ("base", "lightstyle", "dlight", "combined")
    lighting_frames = [
        "GOLDSRC_LIGHTING_FRAME schema=1 "
        f"frame={mode * 600 + slot} slot={slot} mode={mode} "
        f"name={name} style_tick={mode * 100} "
        f"animated_scale={256 if mode in (1, 3) else 0} "
        f"dynamic_luxels={100 if mode in (2, 3) else 0} "
        "dynamic_center=8,8 "
        f"style_hash={0x300 + mode:016x} "
        f"patch_hash={0x400 + mode:016x} patch_bytes=1156 "
        f"upload_bytes={2056192 if mode == 0 else 1156} "
        f"dirty_span_bytes={2056192 if mode == 0 else 65552} "
        f"first_upload={'true' if mode == 0 else 'false'}"
        for mode, name in enumerate(lighting_names) for slot in range(2)
    ]
    lighting_readbacks = [
        "GOLDSRC_LIGHTING_READBACK schema=1 "
        f"frame={mode * 600 + slot} slot={slot} mode={mode} name={name} "
        f"hash={0x500 + mode * 2 + slot:016x} bright_pixels=10 "
        "fence=zero videoout_token=exact"
        for mode, name in enumerate(lighting_names) for slot in range(2)
    ]
    effect_names = ("control", "sprite", "particles", "combined")
    effect_frames = [
        "GOLDSRC_SPRITE_PARTICLE_FRAME schema=1 "
        f"frame={mode * 600 + slot} slot={slot} mode={mode} name={name} "
        "sprite_quads=1 alpha_particles=24 additive_particles=48 "
        "sprite_indices=6 alpha_particle_indices=144 "
        "additive_particle_indices=288 atlas_hash=1234567890abcdef "
        f"layout_hash={0x600 + mode * 2 + slot:016x} "
        "transient_bytes=20000 geometry=per-frame-transient"
        for mode, name in enumerate(effect_names) for slot in range(2)
    ]
    effect_draws = [
        "GOLDSRC_SPRITE_PARTICLE_DRAW schema=1 "
        f"frame={mode * 600} slot=0 mode={mode} name={name} "
        f"alpha_key={0 if mode == 0 else 1} "
        f"additive_key={2 if mode in (2, 3) else 0} "
        f"shader={'none' if mode == 0 else 'surface'} "
        f"draws={(0, 1, 2, 3)[mode]} "
        f"indices={(0, 6, 432, 438)[mode]} "
        f"sprite_draws={1 if mode in (1, 3) else 0} "
        f"particle_draws={2 if mode in (2, 3) else 0} "
        "depth_write=false cull=none lightmap=false "
        "ownership=fence+videoout"
        for mode, name in enumerate(effect_names)
    ]
    effect_readbacks = [
        "GOLDSRC_SPRITE_PARTICLE_READBACK schema=1 "
        f"frame={mode * 600 + slot} slot={slot} mode={mode} name={name} "
        f"hash={0x700 + mode * 2 + slot:016x} bright_pixels=10 "
        "fence=zero videoout_token=exact"
        for mode, name in enumerate(effect_names) for slot in range(2)
    ]
    slice_name = "goldsrc-sprite-particles" if sprite_particles else (
        "goldsrc-lighting" if lighting else (
        "goldsrc-2d" if screen_2d else "dynamic-lightmap")
    )
    input_gate = "not-required" if (
        screen_2d or lighting or sprite_particles) else "not-repeated"
    messages = [
        "LOG_BOOT_MONOTONIC_NS=0x1234",
        f"BSP_TEXTURE_PATH_BOOT schema=1 slice={slice_name} target=gfx1013 fw=12.02 transient_slots=2 ownership=fence+videoout bundle_sha256={BUNDLE_SHA} bundle_bytes=42 soak_frames=10000 input_gate={input_gate}",
        "GOLDSRC_VIEWPORT_READY schema=1 framebuffer=1920x1080 inset_viewport=320,180+1280x720 inset_scissor=400,220+1120x640 restore=full-frame registers=8",
        "GOLDSRC_PIPELINES_READY schema=1 semantic_permutations=99 shader_variants=9 native_slots=9 base_depth=000000b6 base_raster=00000240 state_register_bytes=2376 target=gfx1013",
        "GOLDSRC_STATE_MATRIX_READY schema=1 cases=9 hold_frames=300 readback_slots=2 coverage=blend+depth-write+cull+fog+lightmap",
        "GOLDSRC_2D_READY schema=1 framebuffer=1920x1080 projection=orthographic atlas=128x32 format=rgba8 sampler=point batches=alpha+additive components=hud+console+menu+font geometry=per-frame-transient ownership=fence+videoout",
        *( ["GOLDSRC_LIGHTING_READY schema=1 face=2021 draw=42 styles=4 styled_faces=528 styled_layers=1084 atlas=1024x502 patch=177,257+17x17 patch_bytes=1156 source_samples=3468 source_hash=82562233136c9b4c modes=base+lightstyle+dlight+combined hold_frames=600 lightstyle_hz=10 dlight=face-local-radial camera=face-normal-standoff upload=phase3-bounded"] if lighting else []),
        *( ["GOLDSRC_SPRITE_PARTICLE_READY schema=1 atlas=64x32 format=rgba8 modes=control+sprite+particles+combined hold_frames=600 sprite_quads=1 alpha_particles=24 additive_particles=48 batches=alpha+additive geometry=per-frame-transient camera=locked-relative ownership=fence+videoout"] if sprite_particles else []),
        *matrix_frames,
        *matrix_readbacks,
        *(lighting_frames if lighting else []),
        *(lighting_readbacks if lighting else []),
        *(effect_frames if sprite_particles else []),
        *(effect_draws if sprite_particles else []),
        *(effect_readbacks if sprite_particles else []),
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
        ("DYNAMIC_LIGHTMAP_READBACK slot0=3333333333333333 slot1=3333333333333333 final_mode=base slots_equal=true surrounding=stable guards=intact frames=10000" if lighting else "DYNAMIC_LIGHTMAP_READBACK pattern0=3333333333333333 pattern1=4444444444444444 gpu_buffer0=1111111111111111 gpu_buffer1=2222222222222222 buffers_distinct=true surrounding=stable guards=intact frames=10000"),
        "GOLDSRC_PIPELINE_GATE_COMPLETE schema=1 frames=10000 semantic_permutations=99 shader_variants=9 opaque_key=68 opaque_shader=surface_lightmap alpha_key=71 alpha_shader=masked_lightmap framebuffer_distinct=true input_required=false tokens=exact guards=intact errors=0",
        "GOLDSRC_VIEWPORT_GATE_COMPLETE schema=1 frames=10000 sequence=full-clear,inset-opaque,inset-alpha,full-restore framebuffer_distinct=true input_required=false tokens=exact guards=intact errors=0",
        "GOLDSRC_STATE_MATRIX_COMPLETE schema=1 frames=10000 cases=9 readbacks=18 both_slots=true control_pairs=distinct coverage=opaque+alpha+additive+alpha-test+depth-write+cull-front-back-none+fog+lightmap tokens=exact guards=intact errors=0",
        "GOLDSRC_2D_FRAME schema=1 frame=0 slot=0 alpha_key=129 additive_key=130 shader=screen_2d draws=2 indices=522 hud_quads=4 console_quads=2 menu_quads=3 font_quads=78 atlas_hash=1111111111111111 layout_hash=2222222222222222 transient_bytes=34800 ownership=fence+videoout",
        "GOLDSRC_2D_FRAME schema=1 frame=9999 slot=1 alpha_key=129 additive_key=130 shader=screen_2d draws=2 indices=522 hud_quads=4 console_quads=2 menu_quads=3 font_quads=78 atlas_hash=1111111111111111 layout_hash=2222222222222222 transient_bytes=34800 ownership=fence+videoout",
        "GOLDSRC_2D_COMPLETE schema=1 frames=10000 draws_per_frame=2 passes=alpha+additive projection=orthographic components=hud+console+menu+font atlas=procedural-rgba8 geometry=per-frame-transient tokens=exact guards=intact errors=0",
        *( ["GOLDSRC_LIGHTING_COMPLETE schema=1 frames=10000 modes=4 readbacks=8 both_slots=true control_pairs=distinct lightstyles=real-bsp-planes animated_hz=10 dynamic_lights=face-local-radial atlas_upload=phase3-bounded face=2021 styles=4 styled_faces=528 styled_layers=1084 tokens=exact guards=intact errors=0"] if lighting else []),
        *( ["GOLDSRC_SPRITE_PARTICLE_COMPLETE schema=1 frames=10000 modes=4 readbacks=8 both_slots=true control_pairs=distinct combined_pairs=distinct sprite_quads=1 alpha_particles=24 additive_particles=48 batches=alpha+additive geometry=per-frame-transient camera=locked-proof-view input_dependency=none tokens=exact guards=intact errors=0"] if sprite_particles else []),
        "RESOURCE_POOL_RETIRED token=10099 reclaimed=6 completion=fence+videoout",
    ]
    rows = []
    for index, message in enumerate(messages, 1):
        level = "INFO" if message.startswith("BSP_FRAME ") else "MARK"
        rows.append(f"{index}\t{index}\t{level}\t{message}")
    payload = "\n".join([
        "HELLO ps5log/1 title=PPSA99996 app=ps5-xash3d boot=0x1234 tag=test",
        *rows,
        f"BYE seq={len(rows)} reason={'goldsrc-phase4-sprite-particle-soak-complete' if sprite_particles else ('goldsrc-phase4-lighting-soak-complete' if lighting else ('goldsrc-phase4-2d-soak-complete' if screen_2d else 'bsp-texture-path-lightmap-soak-complete'))}",
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


def run(path: Path, *, matrix: bool = False,
        screen_2d: bool = False,
        lighting: bool = False,
        sprite_particles: bool = False) -> subprocess.CompletedProcess[str]:
    command = [
        "python3", str(VALIDATOR), str(path),
        "--bundle-sha256", BUNDLE_SHA, "--bundle-bytes", "42",
        "--require-viewport",
    ]
    if matrix:
        command.append("--require-matrix")
    if screen_2d:
        command.append("--require-2d")
    if lighting:
        command.append("--require-lighting")
    if sprite_particles:
        command.append("--require-sprite-particles")
    return subprocess.run(command, text=True, capture_output=True, check=False)


def main() -> int:
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        path = make_evidence(root)
        valid = run(path)
        assert valid.returncode == 0, valid.stderr
        valid_matrix = run(path, matrix=True)
        assert valid_matrix.returncode == 0, valid_matrix.stderr
        path_2d = make_evidence(root, screen_2d=True)
        valid_2d = run(path_2d, screen_2d=True)
        assert valid_2d.returncode == 0, valid_2d.stderr
        path_lighting = make_evidence(root, lighting=True)
        valid_lighting = run(path_lighting, lighting=True)
        assert valid_lighting.returncode == 0, valid_lighting.stderr
        path_effects = make_evidence(root, sprite_particles=True)
        valid_effects = run(path_effects, sprite_particles=True)
        assert valid_effects.returncode == 0, valid_effects.stderr
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
