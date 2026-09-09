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


def validate_live_brush(messages: list[str], views: int, surfaces: int) -> dict:
    complete = one(messages, "REF_AGC_LIVE_BRUSH_COMPLETE")
    samples = [parse_fields(m) for m in messages
               if m.startswith("REF_AGC_LIVE_BRUSH_FRAME ")]
    entities = [parse_fields(m) for m in messages
                if m.startswith("REF_AGC_LIVE_BRUSH_ENTITY ")]
    if not exact(complete, {"schema": "1", "errors": "0",
                           "ownership": "fence+videoout+ack"}) \
            or not samples or not entities \
            or not 0 < int(complete.get("frames", "0")) <= views \
            or any(int(complete.get(k, "0")) <= 0 for k in ("instances", "draws", "indices")) \
            or complete.get("transform_hash") in (None, "0000000000000000"):
        fail("live brush completion missing or invalid")
    prior = {}
    moved = set()
    for sample in samples:
        selected = [e for e in entities if e.get("serial") == sample.get("serial")]
        count = int(sample.get("instances", "0"))
        if count <= 0 or len(selected) != count or len({e["index"] for e in selected}) != count \
                or int(sample.get("input_entities", "0")) < count \
                or sample.get("rejected") != "0" \
                or sample.get("ownership") != "transient-slot" \
                or sum(int(sample.get(k, "0")) for k in ("opaque", "alpha", "additive")) != count \
                or int(sample.get("draws", "0")) <= 0 \
                or int(sample.get("indices", "0")) <= 0:
            fail("live brush sample accounting mismatch")
        for entity in selected:
            first, length = int(entity["first_surface"]), int(entity["surface_count"])
            if first < 0 or length <= 0 or first + length > surfaces \
                    or not entity.get("model", "").startswith("*") \
                    or int(entity["mode"]) not in range(6):
                fail("live brush entity range/model mismatch")
            pose = tuple(int(v) for k in ("origin_milli", "angles_milli")
                         for v in entity[k].split(","))
            if len(pose) != 6:
                fail("live brush pose mismatch")
            key = (entity["index"], entity["model"])
            if key in prior and prior[key] != pose:
                moved.add(key)
            prior[key] = pose
    for key in ("instances", "draws", "indices"):
        if sum(int(s[key]) for s in samples) > int(complete[key]):
            fail("live brush sample totals exceed completion")
    return {"frames": int(complete["frames"]), "instances": int(complete["instances"]),
            "draws": int(complete["draws"]), "indices": int(complete["indices"]),
            "samples": len(samples), "entities_observed": len(prior),
            "moving_entities": [list(k) for k in sorted(moved)]}


def validate_renderer(
    manifest_path: Path, *, bundle_sha256: str, bundle_bytes: int,
    studio_sha256: str, studio_bytes: int,
    require_live_lightmaps: bool = False,
    require_live_special_surfaces: bool = False,
    require_live_2d: bool = False,
    require_live_menu: bool = False,
    require_live_brush: bool = False,
) -> dict[str, object]:
    manifest, messages, data = load_renderer(manifest_path)
    boot = one(messages, "BSP_TEXTURE_PATH_BOOT")
    if boot.get("slice") == "phase7-live-consumer":
        if not exact(boot, {
            "schema": "1", "slice": "phase7-live-consumer",
            "target": "gfx1013", "fw": "12.02",
            "ownership": "fence+videoout+ack",
            "bundle_sha256": bundle_sha256,
            "lifetime": "engine-owned", "input_owner": "engine",
        }) or int(boot.get("bundle_bytes", "0"), 10) != bundle_bytes:
            fail("Phase 7 renderer boot/assets mismatch")
        studio_present = "studio_sha256" in boot or "studio_bytes" in boot
        if studio_present and (boot.get("studio_sha256") != studio_sha256 \
                or int(boot.get("studio_bytes", "0"), 10) != studio_bytes):
            fail("Phase 7 renderer legacy studio asset mismatch")
        loop = one(messages, "BSP_LOOP_BEGIN")
        legacy_loop = exact(loop, {
            "mode": "phase7-live-consumer", "buffers": "2",
            "color_dma": "false", "depth_dma": "true",
            "indexed": "true", "frames": "engine-owned",
            "camera": "live-refapi", "geometry": "baked-c1a0",
            "lists": "world+entities+2d",
            "retirement": "fence+videoout+ack",
            "input_dependency": "engine",
        })
        live_world_loop = exact(loop, {
            "mode": "phase7-live-consumer", "buffers": "2",
            "color_dma": "false", "depth_dma": "true",
            "indexed": "true", "frames": "engine-owned",
            "camera": "live-refapi", "geometry": "live-refapi",
            "textures": "live-refapi",
            "retirement": "fence+videoout+ack",
            "input_dependency": "engine",
        }) and loop.get("lists") in ("world", "world+2d")
        if not legacy_loop and not live_world_loop:
            fail("Phase 7 live integration loop mismatch")
        if require_live_lightmaps and loop.get("lightmaps") != "live-atlas":
            fail("Phase 7 live lightmap atlas marker is missing")
        if require_live_2d and loop.get("lists") != "world+2d":
            fail("Phase 7 live 2D list marker is missing")
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
        studio_markers = [parse_fields(message) for message in messages
                          if message.startswith("REF_AGC_GPU_STUDIO_CACHE_COMPLETE ")]
        if len(studio_markers) > 1:
            fail("Phase 7 Studio cache completion is duplicated")
        studio_cache = studio_markers[0] if studio_markers else None
        if studio_cache is not None:
            if not live_world_loop or not exact(studio_cache, {
                "schema": "1", "arena_bytes": "33554432",
                "source": "engine-decoded-studio-v10", "memory": "direct",
                "ownership": "fence+videoout-before-reuse", "errors": "0",
            }) or any(int(studio_cache.get(key, "0")) <= 0 for key in (
                "revision", "creates", "active", "resident_bytes", "source_bytes", "flushes"
            )) or not (int(studio_cache["resident_bytes"]) <=
                       int(studio_cache.get("peak_bytes", "0")) <= 33554432) \
                    or not (int(studio_cache["active"]) <=
                            int(studio_cache.get("peak", "0")) <=
                            int(studio_cache["creates"])):
                fail("Phase 7 Studio cache ownership/accounting mismatch")
        allowed_reclaims = ("9",) if studio_cache is not None else ("6", "7", "8")
        frames = int(complete.get("frames", "0"), 10)
        views = int(complete.get("view_frames", "0"), 10)
        bright = int(complete.get("bright_pixels", "0"), 10)
        if frames <= 0 or views <= 0 or views > frames \
                or complete.get("serial") != str(frames) \
                or complete.get("camera_hash") in (None, "0000000000000000") \
                or complete.get("buffer0") in (None, "0000000000000000") \
                or complete.get("buffer1") in (None, "0000000000000000") \
                or complete.get("frame_hash") in (
                    None, "0000000000000000") \
                or bright <= 0 or complete.get("resource_reclaimed") not in allowed_reclaims \
                or complete.get("ownership") != "fence+videoout+ack" \
                or complete.get("guards") != "intact" \
                or complete.get("errors") != "0":
            fail("Phase 7 live renderer completion mismatch")
        texture_markers = [parse_fields(message) for message in messages
                           if message.startswith(
                               "REF_AGC_GPU_TEXTURE_COMPLETE ")]
        if len(texture_markers) > 1:
            fail("Phase 7 GPU texture completion marker is duplicated")
        texture = texture_markers[0] if texture_markers else None
        if texture is not None:
            positive = (
                "revision", "creates", "active", "peak", "resident_bytes",
                "peak_bytes", "source_bytes", "flushes",
            )
            if any(int(texture.get(field, "0"), 10) <= 0
                   for field in positive) \
                    or texture.get("descriptor_hash") in (
                        None, "0000000000000000") \
                    or texture.get("arena_bytes") != "67108864" \
                    or texture.get("descriptors") != "rgba8+bilinear" \
                    or texture.get("memory") != "direct" \
                    or texture.get("ownership") != \
                    "fence+videoout-before-reuse" \
                    or texture.get("errors") != "0" \
                    or int(texture["active"], 10) > int(texture["peak"], 10) \
                    or int(texture["resident_bytes"], 10) > int(
                        texture["peak_bytes"], 10):
                fail("Phase 7 GPU texture cache contract mismatch")
        world_markers = [parse_fields(message) for message in messages
                         if message.startswith("REF_AGC_GPU_WORLD_COMPLETE ")]
        if len(world_markers) > 1:
            fail("Phase 7 GPU world completion marker is duplicated")
        world = world_markers[0] if world_markers else None
        if (world is None) != (not live_world_loop):
            fail("Phase 7 live world loop/evidence mismatch")
        if world is not None:
            positive = (
                "revision", "publishes", "vertices", "indices", "draws",
                "texture_tables", "resident_bytes", "peak_bytes", "flushes",
            )
            if any(int(world.get(field, "0"), 10) <= 0
                   for field in positive) \
                    or world.get("source_hash") in (
                        None, "0000000000000000") \
                    or world.get("upload_hash") in (
                        None, "0000000000000000") \
                    or world.get("arena_bytes") != "33554432" \
                    or world.get("geometry") != "live-refapi" \
                    or world.get("textures") != "live-refapi" \
                    or world.get("memory") != "direct" \
                    or world.get("source_indices") != "u32" \
                    or world.get("gpu_indices") != "per-draw-u16" \
                    or world.get("ownership") != \
                    "fence+videoout-before-reuse" \
                    or world.get("errors") != "0" \
                    or world.get("texture_tables") != world.get("draws") \
                    or int(world["resident_bytes"], 10) > int(
                        world["peak_bytes"], 10):
                fail("Phase 7 GPU world contract mismatch")
            if loop.get("lightmaps") == "live-atlas":
                lightmap_positive = (
                    "lightmapped_draws", "lightmap_bytes",
                    "lightmap_rgb_sum", "lightmap_nonzero_texels",
                )
                dimensions = world.get("lightmap", "").split("x")
                rgb_range = world.get("lightmap_rgb_range", "").split("..")
                if any(int(world.get(field, "0"), 10) <= 0
                       for field in lightmap_positive) \
                        or len(dimensions) != 2 \
                        or any(int(value, 10) <= 0 for value in dimensions) \
                        or int(world.get("row_pitch", "0"), 10) < \
                        int(dimensions[0], 10) * 4 \
                        or len(rgb_range) != 2 \
                        or int(rgb_range[0], 10) < 0 \
                        or int(rgb_range[1], 10) <= 0 \
                        or int(rgb_range[1], 10) > 255 \
                        or int(rgb_range[0], 10) > int(rgb_range[1], 10) \
                        or int(world["lightmapped_draws"], 10) > int(
                            world["draws"], 10):
                    fail("Phase 7 live lightmap atlas contract mismatch")
        special_messages = [parse_fields(message) for message in messages
                            if message.startswith(
                                "REF_AGC_LIVE_SPECIAL_SURFACES ")]
        special_summary = None
        if require_live_special_surfaces:
            if world is None:
                fail("Phase 7 live special surfaces require a live world")
            special_counts = (
                "sky_draws", "sky_indices", "turbulent_draws",
                "turbulent_indices",
            )
            if any(int(world.get(field, "0"), 10) <= 0
                   for field in special_counts):
                fail("Phase 7 GPU world special-surface counts are missing")
            if len(special_messages) < 2:
                fail("Phase 7 live special-surface time samples are missing")
            active_times: list[int] = []
            geometry_hashes: set[str] = set()
            texture_hashes: set[str] = set()
            for marker in special_messages:
                if not exact(marker, {
                    "schema": "1", "skybox_draws": "6",
                    "skybox_indices": "36", "sky_active": "1",
                    "sky": "engine-six-sided-camera-centred",
                    "turbulent": "engine-time-classic-warp",
                    "ownership": "transient-slot",
                }) or marker.get("source_sky_draws") != \
                        world.get("sky_draws") \
                        or marker.get("source_sky_indices") != \
                        world.get("sky_indices") \
                        or marker.get("turbulent_draws") != \
                        world.get("turbulent_draws") \
                        or marker.get("turbulent_indices") != \
                        world.get("turbulent_indices") \
                        or int(marker.get("sky_revision", "0"), 10) <= 0 \
                        or marker.get("sky_geometry_hash") in (
                            None, "0000000000000000") \
                        or marker.get("sky_texture_hash") in (
                            None, "0000000000000000") \
                        or marker.get("paused") not in ("0", "1"):
                    fail("Phase 7 live special-surface contract mismatch")
                geometry_hashes.add(marker["sky_geometry_hash"])
                texture_hashes.add(marker["sky_texture_hash"])
                if marker["paused"] == "0":
                    active_times.append(int(
                        marker.get("animation_time_milli", "-1"), 10))
            if len(active_times) < 2 or min(active_times) < 0 \
                    or max(active_times) <= min(active_times):
                fail("Phase 7 turbulent engine time did not advance")
            if len(geometry_hashes) != 1 or len(texture_hashes) != 1:
                fail("Phase 7 skybox hashes changed without a map revision")
            special_summary = {
                "samples": len(special_messages),
                "animation_time_milli": [min(active_times), max(active_times)],
                "sky_geometry_hash": next(iter(geometry_hashes)),
                "sky_texture_hash": next(iter(texture_hashes)),
                "sky_draws": int(world["sky_draws"], 10),
                "sky_indices": int(world["sky_indices"], 10),
                "turbulent_draws": int(world["turbulent_draws"], 10),
                "turbulent_indices": int(world["turbulent_indices"], 10),
            }
        live_2d_markers = [
            parse_fields(message) for message in messages
            if message.startswith("REF_AGC_LIVE_2D_FRAME ")
        ]
        live_2d_complete_markers = [
            parse_fields(message) for message in messages
            if message.startswith("REF_AGC_LIVE_2D_COMPLETE ")
        ]
        if len(live_2d_complete_markers) > 1:
            fail("Phase 7 live 2D completion marker is duplicated")
        live_2d_summary = None
        if require_live_2d:
            if not live_2d_markers or len(live_2d_complete_markers) != 1:
                fail("Phase 7 live 2D frame/completion evidence is missing")
            draw_samples = 0
            sampled_quads = 0
            sampled_draws = 0
            sampled_indices = 0
            for marker in live_2d_markers:
                numeric = {
                    field: int(marker.get(field, "-1"), 10)
                    for field in (
                        "input_commands", "mode_commands", "stretch_quads",
                        "fill_quads", "batches", "alpha_batches",
                        "additive_batches", "opaque_batches", "draws",
                        "indices", "texture_binds", "unresolved",
                        "transient_bytes",
                    )
                }
                quads = numeric["stretch_quads"] + numeric["fill_quads"]
                if any(value < 0 for value in numeric.values()) \
                        or numeric["input_commands"] != \
                        numeric["mode_commands"] + quads \
                        or numeric["indices"] != quads * 6 \
                        or numeric["draws"] != numeric["batches"] \
                        or numeric["texture_binds"] != numeric["batches"] \
                        or numeric["batches"] != (
                            numeric["alpha_batches"]
                            + numeric["additive_batches"]
                            + numeric["opaque_batches"]) \
                        or numeric["unresolved"] != 0 \
                        or (quads == 0) != (numeric["transient_bytes"] == 0) \
                        or marker.get("command_hash") in (
                            None, "0000000000000000") \
                        or marker.get("layout_hash") in (
                            None, "0000000000000000") \
                        or not exact(marker, {
                            "schema": "1", "order": "source-exact",
                            "geometry": "transient-slot",
                            "ownership": "fence+videoout",
                        }):
                    fail("Phase 7 live 2D frame contract mismatch")
                if numeric["draws"] > 0:
                    draw_samples += 1
                    sampled_quads += quads
                    sampled_draws += numeric["draws"]
                    sampled_indices += numeric["indices"]
            live_2d = live_2d_complete_markers[0]
            totals = {
                field: int(live_2d.get(field, "0"), 10)
                for field in (
                    "frames", "frames_with_draws", "input_commands",
                    "mode_commands", "quads", "draws", "indices",
                    "peak_batches", "unresolved",
                )
            }
            if totals["frames"] != frames \
                    or totals["frames_with_draws"] <= 0 \
                    or totals["frames_with_draws"] > totals["frames"] \
                    or totals["input_commands"] <= 0 \
                    or totals["mode_commands"] <= 0 \
                    or totals["quads"] <= 0 \
                    or totals["input_commands"] != \
                    totals["mode_commands"] + totals["quads"] \
                    or totals["draws"] < totals["frames_with_draws"] \
                    or totals["indices"] != totals["quads"] * 6 \
                    or totals["peak_batches"] <= 0 \
                    or totals["peak_batches"] > totals["draws"] \
                    or totals["unresolved"] != 0 \
                    or draw_samples != totals["frames_with_draws"] \
                    or sampled_quads != totals["quads"] \
                    or sampled_draws != totals["draws"] \
                    or sampled_indices != totals["indices"] \
                    or live_2d.get("command_hash") in (
                        None, "0000000000000000") \
                    or not exact(live_2d, {
                        "schema": "1", "order": "source-exact",
                        "geometry": "transient-slot",
                        "ownership": "fence+videoout", "errors": "0",
                    }):
                fail("Phase 7 live 2D completion contract mismatch")
            live_2d_summary = totals | {
                "command_hash": live_2d["command_hash"],
                "frame_samples": len(live_2d_markers),
                "draw_samples": draw_samples,
            }
        live_menu_summary = None
        if require_live_menu:
            if not require_live_2d:
                fail("Phase 7 live menu requires live 2D validation")
            first = one(messages, "REF_AGC_LIVE_MENU_FIRST")
            transition = one(messages, "REF_AGC_LIVE_MENU_TRANSITION")
            menu_complete = one(messages, "REF_AGC_LIVE_MENU_COMPLETE")
            if not exact(first, {
                "schema": "1", "map_serial": "0", "source": "mainui-2d",
                "ownership": "fence+videoout",
            }):
                fail("Phase 7 live menu first-frame contract mismatch")
            first_serial = int(first.get("serial", "0"), 10)
            first_quads = int(first.get("quads", "0"), 10)
            first_draws = int(first.get("draws", "0"), 10)
            map_first_serial = int(transition.get("serial", "0"), 10)
            map_serial = int(transition.get("map_serial", "0"), 10)
            premap_frames = int(transition.get("premap_frames", "0"), 10)
            premap_quads = int(transition.get("premap_quads", "0"), 10)
            premap_draws = int(transition.get("premap_draws", "0"), 10)
            if first_serial <= 0 or first_quads <= 0 or first_draws <= 0 \
                    or map_first_serial <= first_serial or map_serial <= 0 \
                    or premap_frames != map_first_serial - 1 \
                    or premap_quads < first_quads \
                    or premap_draws < first_draws \
                    or not exact(transition, {
                        "schema": "1", "order": "menu-then-map",
                    }):
                fail("Phase 7 live menu transition contract mismatch")
            complete_values = {
                field: int(menu_complete.get(field, "0"), 10)
                for field in (
                    "frames", "quads", "draws", "first_serial",
                    "map_first_serial",
                )
            }
            if complete_values != {
                    "frames": premap_frames,
                    "quads": premap_quads,
                    "draws": premap_draws,
                    "first_serial": first_serial,
                    "map_first_serial": map_first_serial,
            } or map_first_serial > frames \
                    or live_2d_summary is None \
                    or premap_frames > live_2d_summary["frames_with_draws"] \
                    or premap_quads > live_2d_summary["quads"] \
                    or premap_draws > live_2d_summary["draws"] \
                    or not exact(menu_complete, {
                "schema": "1", "order": "menu-then-map",
                "presentation": "native-agc", "ownership": "exact",
                "errors": "0", "pass": "1",
            }):
                fail("Phase 7 live menu completion contract mismatch")
            order = [
                next(index for index, message in enumerate(messages)
                     if message.startswith(prefix + " "))
                for prefix in (
                    "REF_AGC_LIVE_MENU_FIRST",
                    "REF_AGC_LIVE_MENU_TRANSITION",
                    "REF_AGC_LIVE_MENU_COMPLETE",
                    "REF_AGC_LIVE_COMPLETE",
                )
            ]
            if order != sorted(order) or len(set(order)) != len(order):
                fail("Phase 7 live menu marker order mismatch")
            live_menu_summary = {
                "frames": premap_frames,
                "quads": premap_quads,
                "draws": premap_draws,
                "first_serial": first_serial,
                "map_first_serial": map_first_serial,
                "map_serial": map_serial,
                "presentation": "native-agc",
            }
        brush_summary = None
        if require_live_brush:
            if world is None:
                fail("live brush requires a live world cache")
            brush_summary = validate_live_brush(messages, views, int(world["draws"]))
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
            "frame_hash": complete["frame_hash"],
            "bright_pixels": bright,
            "resource_reclaimed": int(complete["resource_reclaimed"], 10),
            "gpu_texture": texture,
            "gpu_world": world,
            "studio_cache": studio_cache,
            "special_surfaces": special_summary,
            "live_2d": live_2d_summary,
            "live_menu": live_menu_summary,
            "live_brush": brush_summary,
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


def validate_engine_menu(manifest_path: Path, *, boot_map: str,
                         gate_seconds: int) -> dict[str, int | str]:
    """Validate the engine half of the native MainUI-to-map transition."""
    manifest_path = manifest_path.resolve()
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    log_path = manifest_path.parent / str(manifest["log_path"])
    lines = log_path.read_text(encoding="utf-8", errors="replace").splitlines()
    records, raw = split_transcript(lines[1:-1])
    messages = [message for _, _, message in records]
    begin = one(messages, "XASH_PHASE7_MENU_GATE_BEGIN")
    complete = one(messages, "XASH_PHASE7_MENU_GATE_COMPLETE")
    begin_index = next(index for index, message in enumerate(messages)
                       if message.startswith("XASH_PHASE7_MENU_GATE_BEGIN "))
    complete_index = next(index for index, message in enumerate(messages)
                          if message.startswith(
                              "XASH_PHASE7_MENU_GATE_COMPLETE "))
    if begin_index >= complete_index:
        fail("Phase 7 engine menu marker order mismatch")
    if not exact(begin, {
        "schema": "1", "map": boot_map, "boot": "mainui",
        "transition": "engine-command-buffer",
    }):
        fail("Phase 7 engine menu begin contract mismatch")
    menu_seconds = int(begin.get("menu_seconds", "0"), 10)
    if menu_seconds <= 0 or menu_seconds >= gate_seconds:
        fail("Phase 7 engine menu interval is outside the bounded gate")
    if not exact(complete, {
        "schema": "1", "map": boot_map, "map_queued": "1",
        "host_result": "0", "ownership": "engine-command-buffer",
        "pass": "1",
    }):
        fail("Phase 7 engine menu completion contract mismatch")
    transition_matches = [
        (index, line[line.index("XASH_PHASE7_MENU_GATE_TRANSITION"):])
        for index, line in enumerate(raw)
        if "XASH_PHASE7_MENU_GATE_TRANSITION" in line
    ]
    spawn_matches = [
        index for index, line in enumerate(raw)
        if f"Spawn Server: {boot_map}" in line
    ]
    if len(transition_matches) != 1 or len(spawn_matches) != 1:
        fail("Phase 7 engine menu transition/spawn proof is not unique")
    transition_index, transition_message = transition_matches[0]
    transition = parse_fields(transition_message)
    if not exact(transition, {
        "seconds": str(menu_seconds), "action": "map", "map": boot_map,
    }) or transition_index >= spawn_matches[0]:
        fail("Phase 7 engine menu transition did not precede map spawn")
    return {
        "menu_seconds": menu_seconds,
        "map": boot_map,
        "map_queued": 1,
    }


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
    parser.add_argument("--require-live-lightmaps", action="store_true")
    parser.add_argument("--require-live-special-surfaces", action="store_true")
    parser.add_argument("--require-live-2d", action="store_true")
    parser.add_argument("--require-live-menu", action="store_true")
    parser.add_argument("--require-live-brush", action="store_true")
    parser.add_argument("--map", default="c1a0")
    args = parser.parse_args()
    try:
        engine = validate_engine(
            args.engine_manifest,
            engine_commit=args.engine_commit,
            hlsdk_commit=args.hlsdk_commit,
            boot_map=args.map, mode="client", ref_agc_prx_gate=True,
        )
        renderer = validate_renderer(
            args.renderer_manifest,
            bundle_sha256=args.bundle_sha256, bundle_bytes=args.bundle_bytes,
            studio_sha256=args.studio_sha256, studio_bytes=args.studio_bytes,
            require_live_lightmaps=args.require_live_lightmaps,
            require_live_special_surfaces=
                args.require_live_special_surfaces,
            require_live_2d=args.require_live_2d,
            require_live_menu=args.require_live_menu,
            require_live_brush=args.require_live_brush,
        )
        engine_menu = validate_engine_menu(
            args.engine_manifest, boot_map=args.map,
            gate_seconds=engine["gate_seconds"],
        ) if args.require_live_menu else None
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
                    or engine["ref_agc_frame_hash"] != \
                    renderer["frame_hash"] \
                    or engine["ref_agc_bright_pixels"] != \
                    renderer["bright_pixels"]:
                fail("Phase 7 engine/renderer consumer accounting mismatch")
            if engine["ref_agc_exports"] == 40:
                texture = renderer["gpu_texture"]
                world = renderer["gpu_world"]
                expected_reclaims = (8 if world is not None else 7) + \
                    int(renderer.get("studio_cache") is not None)
                if texture is None \
                        or renderer["resource_reclaimed"] != expected_reclaims \
                        or int(texture["revision"], 10) > \
                        engine["ref_agc_texture_revision"] \
                        or int(texture["active"], 10) < \
                        engine["ref_agc_world_texture_refs"]:
                    fail("Phase 7 engine/GPU texture accounting mismatch")
                if world is not None and (
                        int(world["draws"], 10) !=
                        engine["ref_agc_world_surfaces"] \
                        or int(world["texture_tables"], 10) !=
                        engine["ref_agc_world_surfaces"]):
                    fail("Phase 7 engine/GPU world accounting mismatch")
            elif engine["ref_agc_exports"] == 31:
                if renderer["gpu_texture"] is not None \
                        or renderer["gpu_world"] is not None \
                        or renderer["resource_reclaimed"] != 6:
                    fail("Phase 7 camera-only renderer contract mismatch")
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
            "ref_agc_texture_revision": engine["ref_agc_texture_revision"],
            "ref_agc_texture_creates": engine["ref_agc_texture_creates"],
            "ref_agc_texture_handles": engine["ref_agc_texture_handles"],
            "ref_agc_texture_peak_active": engine[
                "ref_agc_texture_peak_active"],
            "ref_agc_texture_peak_bytes": engine[
                "ref_agc_texture_peak_bytes"],
            "ref_agc_world_texture_refs": engine[
                "ref_agc_world_texture_refs"],
            "ref_agc_world_textures_resolved": engine[
                "ref_agc_world_textures_resolved"],
            "gpu_buffers": [renderer["buffer0"], renderer["buffer1"]],
            "gpu_bright_pixels": renderer["bright_pixels"],
            "gpu_texture": renderer.get("gpu_texture"),
            "gpu_world": renderer.get("gpu_world"),
            "special_surfaces": renderer.get("special_surfaces"),
            "live_2d": renderer.get("live_2d"),
            "live_menu": renderer.get("live_menu"),
            "live_brush": renderer.get("live_brush"),
            "engine_menu": engine_menu,
            "ownership": "exact",
            "pass": True,
        }
    except (EvidenceError, OSError, ValueError, KeyError) as exc:
        raise SystemExit(f"ref_agc evidence validation failed: {exc}") from exc
    print(json.dumps(summary, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
