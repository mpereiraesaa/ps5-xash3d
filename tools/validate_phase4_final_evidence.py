#!/usr/bin/env python3
"""Fail-closed validation for the integrated 60,000-frame Phase 4 soak."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from validate_phase4_render_state_evidence import (
    EvidenceError,
    fail,
    hex_number,
    load,
    many,
    number,
    one,
)


FRAMES = 60_000
FINAL_FRAMES = (59_400, 59_401, 59_998, 59_999)
FINAL_SLOTS = (0, 1, 0, 1)
FINAL_MODES = (3, 3, 4, 4, 3)


def exact(row: dict[str, str], expected: dict[str, str]) -> bool:
    return all(row.get(key) == value for key, value in expected.items())


def frame_row(messages: list[str], prefix: str,
              frame: int) -> dict[str, str]:
    rows = [row for row in many(messages, prefix)
            if number(row, "frame") == frame]
    if len(rows) != 1:
        fail(f"expected one {prefix} at frame {frame}, found {len(rows)}")
    return rows[0]


def validate(path: Path, *, bundle_sha256: str, bundle_bytes: int,
             studio_sha256: str, studio_bytes: int) -> dict[str, object]:
    messages, data = load(path.resolve())
    boot = one(messages, "BSP_TEXTURE_PATH_BOOT")
    if not exact(boot, {
        "schema": "1", "slice": "goldsrc-phase4-final",
        "target": "gfx1013", "fw": "12.02", "transient_slots": "2",
        "ownership": "fence+videoout", "bundle_sha256": bundle_sha256,
        "studio_sha256": studio_sha256, "input_gate": "not-required",
    }) or number(boot, "bundle_bytes") != bundle_bytes or \
            number(boot, "studio_bytes") != studio_bytes or \
            number(boot, "soak_frames") != FRAMES:
        fail("integrated Phase 4 boot contract mismatch")

    loop = one(messages, "BSP_LOOP_BEGIN")
    if not exact(loop, {
        "mode": "goldsrc-phase4-final-soak", "buffers": "2",
        "color_dma": "false", "depth_dma": "true", "indexed": "true",
        "frames": "60000",
        "features": "lighting+2d+sprites+particles+studio+brush+pvs+frustum",
        "combined_window": "59400..59999", "camera": "locked-bsp-tree",
        "geometry": "world+entities", "retirement": "fence+videoout",
        "input_dependency": "none",
    }):
        fail("integrated Phase 4 loop contract mismatch")

    visibility_ready = one(messages, "GOLDSRC_VISIBILITY_READY")
    if not exact(visibility_ready, {
        "schema": "1", "modes": "control+pvs+frustum+combined",
        "camera": "locked-bsp-tree", "ownership": "fence+videoout",
    }) or number(visibility_ready, "planes") != 11_746 or \
            number(visibility_ready, "nodes") != 1_323 or \
            number(visibility_ready, "leaves") != 683 or \
            number(visibility_ready, "pvs_row_bytes") != 86 or \
            number(visibility_ready, "draw_refs") != 2_610 or \
            number(visibility_ready, "world_face_count") != 1_952 or \
            number(visibility_ready, "draw_bounds") != 3_210:
        fail("integrated Phase 4 visibility plan mismatch")

    scene = one(messages, "GOLDSRC_PHASE4_SCENE_READY")
    if not exact(scene, {
        "schema": "1", "water_texture": "water3", "water_mode": "alpha",
        "glass_texture": "glass_med", "glass_mode": "alpha",
        "source": "real-bsp-brushes",
    }) or number(scene, "water_entity") != 65 or \
            number(scene, "water_draws") != 35 or \
            number(scene, "water_indices") != 312 or \
            number(scene, "glass_entity") != 27 or \
            number(scene, "glass_draws") != 6 or \
            number(scene, "glass_indices") != 36:
        fail("integrated Phase 4 water/glass scene mismatch")

    ready = one(messages, "GOLDSRC_PHASE4_FINAL_READY")
    if not exact(ready, {
        "schema": "1", "combined_window": "59400..59999",
        "components": "lighting+2d+sprites+particles+studio+brush+pvs+frustum",
        "skinning": "cpu", "transient": "per-slot",
        "camera": "locked-bsp-tree", "ownership": "fence+videoout",
    }) or number(ready, "frames") != FRAMES or \
            number(ready, "semantic_permutations") != 99 or \
            number(ready, "shader_variants") != 9 or \
            number(ready, "world_draws") != 1_952 or \
            number(ready, "brush_draws") != 69 or \
            number(ready, "studio_draws") != 12 or \
            number(ready, "effect_draws") != 3 or \
            number(ready, "screen_draws") != 2:
        fail("integrated Phase 4 ready contract mismatch")

    rows = many(messages, "GOLDSRC_PHASE4_FINAL_FRAME")
    if [number(row, "frame") for row in rows] != list(FINAL_FRAMES) or \
            [number(row, "slot") for row in rows] != list(FINAL_SLOTS):
        fail("integrated Phase 4 frame checkpoints mismatch")
    pose_hashes: set[int] = set()
    transform_hashes: set[int] = set()
    for row in rows:
        modes = tuple(number(row, key) for key in (
            "lighting", "effects", "studio", "brush", "visibility"))
        if row.get("schema") != "1" or modes != FINAL_MODES or \
                number(row, "world_draws") != 362 or \
                number(row, "brush_draws") != 69 or \
                number(row, "studio_draws") != 12 or \
                number(row, "effect_draws") != 3 or \
                number(row, "screen_draws") != 2 or \
                number(row, "dynamic_luxels") <= 0 or \
                hex_number(row, "pose_hash") == 0 or \
                hex_number(row, "transform_hash") == 0 or \
                hex_number(row, "screen_layout_hash") == 0 or \
                not 1 <= number(row, "transient_bytes") <= 131_072:
            fail("integrated Phase 4 frame contract mismatch")
        pose_hashes.add(hex_number(row, "pose_hash"))
        transform_hashes.add(hex_number(row, "transform_hash"))
    if len(pose_hashes) < 2 or len(transform_hashes) < 2:
        fail("animation or brush transforms did not change")

    lighting = frame_row(messages, "GOLDSRC_LIGHTING_FRAME", 59_400)
    effects = frame_row(messages, "GOLDSRC_SPRITE_PARTICLE_DRAW", 59_400)
    studio = frame_row(messages, "GOLDSRC_STUDIO_DRAW", 59_400)
    brush = frame_row(messages, "GOLDSRC_BRUSH_DRAW", 59_400)
    visibility = frame_row(messages, "GOLDSRC_VISIBILITY_DRAW", 59_400)
    screen_frame = frame_row(messages, "GOLDSRC_2D_FRAME", 59_999)
    if number(lighting, "mode") != 3 or \
            number(lighting, "dynamic_luxels") <= 0 or \
            number(effects, "mode") != 3 or number(effects, "draws") != 3 or \
            number(effects, "indices") != 438 or \
            number(studio, "mode") != 4 or \
            number(studio, "instances") != 3 or \
            number(studio, "draws") != 12 or \
            number(studio, "indices") != 846 or \
            number(brush, "mode") != 4 or \
            number(brush, "instances") != 5 or \
            number(brush, "draws") != 69 or \
            number(brush, "indices") != 528 or \
            number(visibility, "mode") != 3 or \
            number(visibility, "draws") != 362 or \
            visibility.get("pvs") != "on" or \
            visibility.get("frustum") != "on" or \
            number(screen_frame, "draws") != 2 or \
            number(screen_frame, "indices") != 522:
        fail("integrated Phase 4 draw composition mismatch")

    readbacks = many(messages, "GOLDSRC_PHASE4_FINAL_READBACK")
    if len(readbacks) != 2:
        fail("integrated Phase 4 readback count mismatch")
    for index, row in enumerate(readbacks):
        if row.get("schema") != "1" or \
                number(row, "frame") != FINAL_FRAMES[index] or \
                number(row, "slot") != FINAL_SLOTS[index] or \
                hex_number(row, "hash") == 0 or \
                number(row, "bright_pixels") <= 0 or \
                number(row, "world_draws") != 362 or \
                number(row, "brush_draws") != 69 or \
                number(row, "studio_draws") != 12 or \
                number(row, "effect_draws") != 3 or \
                number(row, "screen_draws") != 2 or \
                row.get("modes") != "3,3,4,4,3" or \
                row.get("fence") != "zero" or \
                row.get("videoout_token") != "exact":
            fail("integrated Phase 4 GPU readback mismatch")

    for prefix in ("RESOURCE_FRAME_READY", "RESOURCE_FRAME_SEALED",
                   "RESOURCE_FRAME_SUBMITTED", "RESOURCE_FRAME_RETIRED",
                   "BSP_VIDEOOUT_TOKEN"):
        bookends = many(messages, prefix)
        if [number(row, "frame") for row in bookends] != [0, 59_999]:
            fail(f"{prefix} bookends mismatch")
    terminal = [row for row in many(messages, "BSP_FRAME")
                if row.get("frame") == "59999"]
    if len(terminal) != 1 or number(terminal[0], "completed") != FRAMES or \
            number(terminal[0], "errors") != 0:
        fail("terminal telemetry is not clean")

    ring = one(messages, "RESOURCE_RING_RETIRED")
    if ring.get("reusable") != "true" or ring.get("tokens") != "exact":
        fail("transient ring retirement mismatch")
    resource = one(messages, "BSP_RESOURCE_READBACK")
    if hex_number(resource, "buffer0") == 0 or \
            hex_number(resource, "buffer1") == 0 or \
            number(resource, "bright_pixels0") <= 0 or \
            number(resource, "bright_pixels1") <= 0 or \
            resource.get("guards") != "intact" or \
            number(resource, "frames") != FRAMES or \
            number(resource, "errors") != 0:
        fail("final framebuffer readback contract mismatch")
    dynamic = one(messages, "DYNAMIC_LIGHTMAP_READBACK")
    if dynamic.get("final_mode") != "combined" or \
            dynamic.get("slots_equal") != "true" or \
            dynamic.get("surrounding") != "stable" or \
            dynamic.get("guards") != "intact" or \
            number(dynamic, "frames") != FRAMES or \
            hex_number(dynamic, "slot0") == 0 or \
            dynamic.get("slot0") != dynamic.get("slot1"):
        fail("final lightmap convergence mismatch")

    pipeline = one(messages, "GOLDSRC_PIPELINE_GATE_COMPLETE")
    if not exact(pipeline, {
        "schema": "1", "opaque_shader": "surface_lightmap",
        "alpha_shader": "masked_lightmap", "framebuffer_distinct": "true",
        "input_required": "false", "tokens": "exact", "guards": "intact",
        "errors": "0",
    }) or number(pipeline, "frames") != FRAMES or \
            number(pipeline, "semantic_permutations") != 99 or \
            number(pipeline, "shader_variants") != 9:
        fail("final pipeline contract mismatch")
    screen = one(messages, "GOLDSRC_2D_COMPLETE")
    if number(screen, "frames") != FRAMES or \
            number(screen, "draws_per_frame") != 2 or \
            screen.get("projection") != "orthographic" or \
            screen.get("components") != "hud+console+menu+font" or \
            screen.get("tokens") != "exact" or \
            screen.get("guards") != "intact" or number(screen, "errors") != 0:
        fail("final 2D contract mismatch")

    complete = one(messages, "GOLDSRC_PHASE4_FINAL_COMPLETE")
    if not exact(complete, {
        "schema": "1", "both_slots": "true", "lightstyles": "real-bsp-planes",
        "dynamic_lights": "face-local-radial", "sprites": "true",
        "particles": "true", "cpu_skinning": "true", "chrome": "true",
        "brush_entities": "true", "water": "true", "glass": "true",
        "pvs": "true", "frustum": "true",
        "transient": "per-slot", "animation_changes": "true",
        "ownership": "fence+videoout", "tokens": "exact",
        "guards": "intact", "errors": "0",
    }) or number(complete, "frames") != FRAMES or \
            number(complete, "combined_frames") != 600 or \
            number(complete, "readbacks") != 2 or \
            number(complete, "world_draws") != 362 or \
            number(complete, "brush_draws") != 69 or \
            number(complete, "studio_draws") != 12 or \
            number(complete, "effect_draws") != 3 or \
            number(complete, "screen_draws") != 2:
        fail("integrated Phase 4 completion mismatch")
    pool = one(messages, "RESOURCE_POOL_RETIRED")
    if number(pool, "reclaimed") != 6 or \
            pool.get("completion") != "fence+videoout":
        fail("resource pool reclamation mismatch")
    return {
        "run_id": json.loads(path.read_text(encoding="utf-8")).get("run_id"),
        "records": json.loads(path.read_text(encoding="utf-8")).get("records"),
        "frames": FRAMES,
        "readbacks": 2,
        "world_draws": 362,
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
        raise SystemExit(f"Phase 4 final evidence rejected: {exc}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
