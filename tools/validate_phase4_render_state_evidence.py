#!/usr/bin/env python3
"""Fail-closed validation for the Phase 4 pipeline/viewport PS5 gate."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


class EvidenceError(RuntimeError):
    pass


def fail(message: str) -> None:
    raise EvidenceError(message)


def fields(message: str) -> dict[str, str]:
    parsed: dict[str, str] = {}
    for token in message.split()[1:]:
        if "=" not in token:
            fail(f"malformed marker field: {token}")
        key, value = token.split("=", 1)
        parsed[key] = value
    return parsed


def number(item: dict[str, str], key: str) -> int:
    try:
        return int(item[key], 0)
    except (KeyError, ValueError) as exc:
        fail(f"invalid integer field: {key}")
        raise AssertionError from exc


def hex_number(item: dict[str, str], key: str) -> int:
    try:
        return int(item[key], 16)
    except (KeyError, ValueError) as exc:
        fail(f"invalid hexadecimal field: {key}")
        raise AssertionError from exc


def one(messages: list[str], prefix: str) -> dict[str, str]:
    matches = [fields(message) for message in messages
               if message.startswith(prefix + " ")]
    if len(matches) != 1:
        fail(f"expected exactly one {prefix}, found {len(matches)}")
    return matches[0]


def many(messages: list[str], prefix: str) -> list[dict[str, str]]:
    return [fields(message) for message in messages
            if message.startswith(prefix + " ")]


def load(path: Path) -> tuple[list[str], bytes]:
    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        fail(f"invalid manifest: {exc}")
    identity = manifest.get("identity")
    if not isinstance(identity, dict) or \
            identity.get("title") != "PPSA99996" or \
            identity.get("app") != "ps5-xash3d":
        fail("title/app identity mismatch")
    if manifest.get("protocol") != "ps5log/1" or \
            manifest.get("transport") != "tcp" or \
            not all(manifest.get(key) for key in ("hello", "bye", "clean")) or \
            manifest.get("gaps") != [] or manifest.get("raw_lines") != 0 or \
            manifest.get("oversized_lines") != 0:
        fail("run is incomplete or transport-corrupt")
    log_name = manifest.get("log_path")
    if not isinstance(log_name, str) or Path(log_name).name != log_name:
        fail("unsafe transcript path")
    log = (path.parent / log_name).resolve()
    if log.parent != path.parent.resolve():
        fail("transcript escaped manifest directory")
    try:
        data = log.read_bytes()
    except OSError as exc:
        fail(f"missing transcript: {exc}")
    if len(data) != manifest.get("bytes") or \
            hashlib.sha256(data).hexdigest() != manifest.get("sha256"):
        fail("transcript size/hash mismatch")
    lines = data.decode("utf-8").splitlines()
    boot = str(identity.get("boot", ""))
    if not lines or not lines[0].startswith("HELLO ps5log/1 ") or \
            f"title=PPSA99996 app=ps5-xash3d boot={boot}" not in lines[0]:
        fail("HELLO identity mismatch")
    if not lines[-1].startswith("BYE seq=") or not any(
            reason in lines[-1] for reason in (
                "reason=bsp-texture-path-lightmap-soak-complete",
                "reason=goldsrc-phase4-2d-soak-complete",
                "reason=goldsrc-phase4-lighting-soak-complete",
                "reason=goldsrc-phase4-sprite-particle-soak-complete")):
        fail("BYE reason mismatch")
    records: list[tuple[int, str, str]] = []
    for line in lines[1:-1]:
        parts = line.split("\t", 3)
        if len(parts) != 4:
            fail("malformed structured record")
        try:
            sequence = int(parts[0])
        except ValueError:
            fail("invalid sequence number")
        records.append((sequence, parts[2], parts[3]))
    if not records or [row[0] for row in records] != \
            list(range(1, len(records) + 1)) or \
            len(records) != manifest.get("records") or \
            records[-1][0] != manifest.get("last_seq") or \
            any(level in {"ERR", "ERROR"} for _, level, _ in records):
        fail("record sequence/count/error contract failed")
    return [message for _, _, message in records], data


def validate(path: Path, *, bundle_sha256: str,
             bundle_bytes: int, require_viewport: bool,
             require_matrix: bool,
             require_2d: bool,
             require_lighting: bool,
             require_sprite_particles: bool) -> dict[str, object]:
    messages, data = load(path.resolve())
    boot = one(messages, "BSP_TEXTURE_PATH_BOOT")
    if boot.get("schema") != "1" or boot.get("target") != "gfx1013" or \
            boot.get("fw") != "12.02" or \
            boot.get("ownership") != "fence+videoout" or \
            boot.get("bundle_sha256") != bundle_sha256 or \
            number(boot, "bundle_bytes") != bundle_bytes or \
            number(boot, "soak_frames") != 10_000:
        fail("Phase 4 boot contract mismatch")
    if require_2d and (boot.get("slice") != "goldsrc-2d" or
                       boot.get("input_gate") != "not-required"):
        fail("2D boot contract mismatch")
    if require_lighting and (
            boot.get("slice") != "goldsrc-lighting" or
            boot.get("input_gate") != "not-required"):
        fail("lighting boot contract mismatch")
    if require_sprite_particles and (
            boot.get("slice") != "goldsrc-sprite-particles" or
            boot.get("input_gate") != "not-required"):
        fail("sprite/particle boot contract mismatch")
    ready = one(messages, "GOLDSRC_PIPELINES_READY")
    if number(ready, "semantic_permutations") != 99 or \
            number(ready, "shader_variants") != 9 or \
            number(ready, "native_slots") != 9 or \
            number(ready, "state_register_bytes") != 2376 or \
            ready.get("base_depth") != "000000b6" or \
            ready.get("base_raster") != "00000240":
        fail("pipeline catalog contract mismatch")
    state = one(messages, "GOLDSRC_STATE_FRAME")
    if number(state, "frame") != 0 or number(state, "slot") != 0 or \
            number(state, "opaque_key") != 68 or \
            state.get("opaque_pass") != "0" or \
            state.get("opaque_shader") != "surface_lightmap" or \
            state.get("opaque_blend") != "00000000" or \
            state.get("opaque_depth") != "000000b6" or \
            state.get("opaque_raster") != "00000240" or \
            number(state, "alpha_key") != 71 or \
            state.get("alpha_pass") != "1" or \
            state.get("alpha_shader") != "masked_lightmap" or \
            state.get("alpha_blend") != "00000000" or \
            state.get("alpha_depth") != "000000b6" or \
            state.get("alpha_raster") != "00000240":
        fail("draw-selected state contract mismatch")
    if require_viewport:
        viewport = one(messages, "GOLDSRC_VIEWPORT_READY")
        if viewport.get("framebuffer") != "1920x1080" or \
                viewport.get("inset_viewport") != "320,180+1280x720" or \
                viewport.get("inset_scissor") != "400,220+1120x640" or \
                viewport.get("restore") != "full-frame" or \
                number(viewport, "registers") != 8:
            fail("viewport plan contract mismatch")
        frame = one(messages, "GOLDSRC_VIEWPORT_FRAME")
        if frame.get("sequence") != \
                "full-clear,inset-opaque,inset-alpha,full-restore" or \
                frame.get("inset_scissor_tl") != "80dc0190" or \
                frame.get("inset_scissor_br") != "035c05f0" or \
                frame.get("full_scissor_tl") != "80000000" or \
                frame.get("full_scissor_br") != "04380780":
            fail("mid-frame viewport/scissor sequence mismatch")
    if require_matrix:
        expected = (
            ("opaque", 68, 0, 1, 0, 0, 1, "surface_lightmap",
             "00000000", "000000b6", "00000240"),
            ("alpha", 65, 1, 0, 0, 0, 1, "surface_lightmap",
             "65010504", "000000b2", "00000240"),
            ("additive", 66, 2, 0, 0, 0, 1, "surface_lightmap",
             "61010104", "000000b2", "00000240"),
            ("alpha-test", 71, 3, 1, 0, 0, 1, "masked_lightmap",
             "00000000", "000000b6", "00000240"),
            ("depth-write-off", 64, 0, 0, 0, 0, 1,
             "surface_lightmap", "00000000", "000000b2", "00000240"),
            ("cull-front", 76, 0, 1, 1, 0, 1, "surface_lightmap",
             "00000000", "000000b6", "00000241"),
            ("cull-back", 84, 0, 1, 2, 0, 1, "surface_lightmap",
             "00000000", "000000b6", "00000242"),
            ("fog", 100, 0, 1, 0, 1, 1, "surface_lightmap_fog",
             "00000000", "000000b6", "00000240"),
            ("lightmap-off", 4, 0, 1, 0, 0, 0, "surface",
             "00000000", "000000b6", "00000240"),
        )
        matrix_ready = one(messages, "GOLDSRC_STATE_MATRIX_READY")
        if matrix_ready.get("schema") != "1" or \
                number(matrix_ready, "cases") != len(expected) or \
                number(matrix_ready, "hold_frames") != 300 or \
                number(matrix_ready, "readback_slots") != 2 or \
                matrix_ready.get("coverage") != \
                "blend+depth-write+cull+fog+lightmap":
            fail("state-matrix ready contract mismatch")
        state_rows = many(messages, "GOLDSRC_STATE_MATRIX_FRAME")
        for index, item in enumerate(expected):
            rows = [row for row in state_rows if row.get("case") == str(index)]
            if not rows:
                fail(f"missing state-matrix draw case {index}")
            row = rows[0]
            name, key, blend, depth, cull, fog, lightmap, shader, \
                blend_cx, depth_cx, raster_cx = item
            if row.get("schema") != "1" or row.get("name") != name or \
                    number(row, "key") != key or \
                    number(row, "blend") != blend or \
                    number(row, "depth_write") != depth or \
                    number(row, "cull") != cull or \
                    number(row, "fog") != fog or \
                    number(row, "lightmap") != lightmap or \
                    row.get("shader") != shader or \
                    row.get("blend_cx") != blend_cx or \
                    row.get("depth_cx") != depth_cx or \
                    row.get("raster_cx") != raster_cx:
                fail(f"state-matrix draw case {index} mismatch")
        readbacks = many(messages, "GOLDSRC_STATE_MATRIX_READBACK")
        if len(readbacks) != len(expected) * 2:
            fail("state-matrix readback count mismatch")
        hashes: dict[tuple[int, int], str] = {}
        for row in readbacks:
            index = number(row, "case")
            slot = number(row, "slot")
            if index >= len(expected) or slot >= 2 or \
                    (index, slot) in hashes or \
                    row.get("schema") != "1" or \
                    row.get("name") != expected[index][0] or \
                    number(row, "key") != expected[index][1] or \
                    hex_number(row, "hash") == 0 or \
                    number(row, "bright_pixels") <= 0 or \
                    row.get("fence") != "zero" or \
                    row.get("videoout_token") != "exact":
                fail("state-matrix GPU readback mismatch")
            hashes[index, slot] = row["hash"]
        for index in range(1, len(expected)):
            for slot in range(2):
                if hashes[index, slot] == hashes[0, slot]:
                    fail("state-matrix feature/control framebuffer collision")
    if require_2d:
        ready_2d = one(messages, "GOLDSRC_2D_READY")
        if ready_2d.get("schema") != "1" or \
                ready_2d.get("framebuffer") != "1920x1080" or \
                ready_2d.get("projection") != "orthographic" or \
                ready_2d.get("atlas") != "128x32" or \
                ready_2d.get("format") != "rgba8" or \
                ready_2d.get("sampler") != "point" or \
                ready_2d.get("batches") != "alpha+additive" or \
                ready_2d.get("components") != "hud+console+menu+font" or \
                ready_2d.get("geometry") != "per-frame-transient" or \
                ready_2d.get("ownership") != "fence+videoout":
            fail("2D ready contract mismatch")
        frames_2d = many(messages, "GOLDSRC_2D_FRAME")
        if [number(row, "frame") for row in frames_2d] != [0, 9_999]:
            fail("2D frame bookends mismatch")
        for index, row in enumerate(frames_2d):
            if number(row, "slot") != index or \
                    number(row, "alpha_key") != 129 or \
                    number(row, "additive_key") != 130 or \
                    row.get("shader") != "screen_2d" or \
                    number(row, "draws") != 2 or \
                    number(row, "indices") != 522 or \
                    number(row, "hud_quads") != 4 or \
                    number(row, "console_quads") != 2 or \
                    number(row, "menu_quads") != 3 or \
                    number(row, "font_quads") != 78 or \
                    hex_number(row, "atlas_hash") == 0 or \
                    hex_number(row, "layout_hash") == 0 or \
                    not 32_768 <= number(row, "transient_bytes") < 65_536 or \
                    row.get("ownership") != "fence+videoout":
                fail("2D frame contract mismatch")
        if frames_2d[0]["atlas_hash"] != frames_2d[1]["atlas_hash"] or \
                frames_2d[0]["layout_hash"] != frames_2d[1]["layout_hash"]:
            fail("2D deterministic atlas/layout mismatch")
    if require_lighting:
        lighting_ready = one(messages, "GOLDSRC_LIGHTING_READY")
        if lighting_ready.get("schema") != "1" or \
                number(lighting_ready, "styles") < 2 or \
                number(lighting_ready, "styled_faces") <= 0 or \
                number(lighting_ready, "styled_layers") <= 0 or \
                number(lighting_ready, "patch_bytes") <= 0 or \
                number(lighting_ready, "source_samples") <= 0 or \
                hex_number(lighting_ready, "source_hash") == 0 or \
                lighting_ready.get("modes") != \
                "base+lightstyle+dlight+combined" or \
                number(lighting_ready, "hold_frames") != 600 or \
                number(lighting_ready, "lightstyle_hz") != 10 or \
                lighting_ready.get("dlight") != "face-local-radial" or \
                lighting_ready.get("camera") != "face-normal-standoff" or \
                lighting_ready.get("upload") != "phase3-bounded":
            fail("lighting ready contract mismatch")
        expected_names = ("base", "lightstyle", "dlight", "combined")
        lighting_frames = many(messages, "GOLDSRC_LIGHTING_FRAME")
        for mode, name in enumerate(expected_names):
            rows = [row for row in lighting_frames
                    if number(row, "mode") == mode]
            if not rows or {number(row, "slot") for row in rows} != {0, 1}:
                fail(f"lighting frame coverage mismatch for mode {mode}")
            for row in rows:
                if row.get("schema") != "1" or row.get("name") != name or \
                        hex_number(row, "style_hash") == 0 or \
                        hex_number(row, "patch_hash") == 0 or \
                        number(row, "patch_bytes") != \
                            number(lighting_ready, "patch_bytes") or \
                        number(row, "upload_bytes") <= 0 or \
                        number(row, "dirty_span_bytes") < \
                            number(row, "patch_bytes"):
                    fail("lighting frame contract mismatch")
            if mode == 0 and any(number(row, "animated_scale") != 0 or
                                 number(row, "dynamic_luxels") != 0
                                 for row in rows):
                fail("base lighting mode is not a true control")
            if mode == 1 and (not any(number(row, "animated_scale") > 0
                                      for row in rows) or
                              any(number(row, "dynamic_luxels") != 0
                                  for row in rows)):
                fail("lightstyle-only mode mismatch")
            if mode == 2 and (any(number(row, "animated_scale") != 0
                                  for row in rows) or
                              not all(number(row, "dynamic_luxels") > 0
                                      for row in rows)):
                fail("dynamic-light-only mode mismatch")
            if mode == 3 and (not any(number(row, "animated_scale") > 0
                                      for row in rows) or
                              not all(number(row, "dynamic_luxels") > 0
                                      for row in rows)):
                fail("combined lighting mode mismatch")
        lighting_readbacks = many(messages, "GOLDSRC_LIGHTING_READBACK")
        if len(lighting_readbacks) != 8:
            fail("lighting readback count mismatch")
        lighting_hashes: dict[tuple[int, int], str] = {}
        for row in lighting_readbacks:
            mode = number(row, "mode")
            slot = number(row, "slot")
            if mode >= 4 or slot >= 2 or (mode, slot) in lighting_hashes or \
                    row.get("schema") != "1" or \
                    row.get("name") != expected_names[mode] or \
                    hex_number(row, "hash") == 0 or \
                    number(row, "bright_pixels") <= 0 or \
                    row.get("fence") != "zero" or \
                    row.get("videoout_token") != "exact":
                fail("lighting GPU readback mismatch")
            lighting_hashes[mode, slot] = row["hash"]
        for slot in range(2):
            if len({lighting_hashes[mode, slot] for mode in range(4)}) != 4:
                fail("lighting feature/control framebuffer collision")
    if require_sprite_particles:
        effects_ready = one(messages, "GOLDSRC_SPRITE_PARTICLE_READY")
        if effects_ready.get("schema") != "1" or \
                effects_ready.get("atlas") != "64x32" or \
                effects_ready.get("format") != "rgba8" or \
                effects_ready.get("modes") != \
                    "control+sprite+particles+combined" or \
                number(effects_ready, "hold_frames") != 600 or \
                number(effects_ready, "sprite_quads") != 1 or \
                number(effects_ready, "alpha_particles") != 24 or \
                number(effects_ready, "additive_particles") != 48 or \
                effects_ready.get("batches") != "alpha+additive" or \
                effects_ready.get("geometry") != "per-frame-transient" or \
                effects_ready.get("camera") != "locked-relative" or \
                effects_ready.get("ownership") != "fence+videoout":
            fail("sprite/particle ready contract mismatch")
        names = ("control", "sprite", "particles", "combined")
        effect_frames = many(messages, "GOLDSRC_SPRITE_PARTICLE_FRAME")
        for mode, name in enumerate(names):
            rows = [row for row in effect_frames
                    if number(row, "mode") == mode]
            if not rows or {number(row, "slot") for row in rows} != {0, 1}:
                fail(f"sprite/particle frame coverage mismatch for mode {mode}")
            for row in rows:
                if row.get("schema") != "1" or row.get("name") != name or \
                        number(row, "sprite_quads") != 1 or \
                        number(row, "alpha_particles") != 24 or \
                        number(row, "additive_particles") != 48 or \
                        number(row, "sprite_indices") != 6 or \
                        number(row, "alpha_particle_indices") != 144 or \
                        number(row, "additive_particle_indices") != 288 or \
                        hex_number(row, "atlas_hash") == 0 or \
                        hex_number(row, "layout_hash") == 0 or \
                        not 8_192 < number(row, "transient_bytes") < 32_768 or \
                        row.get("geometry") != "per-frame-transient":
                    fail("sprite/particle frame contract mismatch")
        effect_draws = many(messages, "GOLDSRC_SPRITE_PARTICLE_DRAW")
        expected_draws = ((0, 0, 0), (1, 6, 1),
                          (2, 432, 0), (3, 438, 1))
        for mode, name in enumerate(names):
            rows = [row for row in effect_draws
                    if number(row, "mode") == mode]
            if not rows:
                fail(f"missing sprite/particle draw mode {mode}")
            for row in rows:
                draws, indices, sprite_draws = expected_draws[mode]
                if row.get("schema") != "1" or row.get("name") != name or \
                        number(row, "alpha_key") != (0 if mode == 0 else 1) or \
                        number(row, "additive_key") != \
                            (2 if mode in (2, 3) else 0) or \
                        row.get("shader") != ("none" if mode == 0 else
                                               "surface") or \
                        number(row, "draws") != draws or \
                        number(row, "indices") != indices or \
                        number(row, "sprite_draws") != sprite_draws or \
                        number(row, "particle_draws") != \
                            (2 if mode in (2, 3) else 0) or \
                        row.get("depth_write") != "false" or \
                        row.get("cull") != "none" or \
                        row.get("lightmap") != "false" or \
                        row.get("ownership") != "fence+videoout":
                    fail("sprite/particle draw contract mismatch")
        effect_readbacks = many(
            messages, "GOLDSRC_SPRITE_PARTICLE_READBACK")
        if len(effect_readbacks) != 8:
            fail("sprite/particle readback count mismatch")
        effect_hashes: dict[tuple[int, int], str] = {}
        for row in effect_readbacks:
            mode = number(row, "mode")
            slot = number(row, "slot")
            if mode >= 4 or slot >= 2 or (mode, slot) in effect_hashes or \
                    row.get("schema") != "1" or \
                    row.get("name") != names[mode] or \
                    hex_number(row, "hash") == 0 or \
                    number(row, "bright_pixels") <= 0 or \
                    row.get("fence") != "zero" or \
                    row.get("videoout_token") != "exact":
                fail("sprite/particle GPU readback mismatch")
            effect_hashes[mode, slot] = row["hash"]
        for slot in range(2):
            if any(effect_hashes[mode, slot] == effect_hashes[0, slot]
                   for mode in range(1, 4)) or \
                    effect_hashes[3, slot] in {
                        effect_hashes[1, slot], effect_hashes[2, slot]}:
                fail("sprite/particle feature/control framebuffer collision")
    for prefix in ("RESOURCE_FRAME_READY", "RESOURCE_FRAME_SEALED",
                   "RESOURCE_FRAME_SUBMITTED", "RESOURCE_FRAME_RETIRED",
                   "BSP_VIDEOOUT_TOKEN"):
        rows = many(messages, prefix)
        if [number(row, "frame") for row in rows] != [0, 9_999]:
            fail(f"{prefix} bookends mismatch")
    terminal = [row for row in many(messages, "BSP_FRAME")
                if row.get("frame") == "9999"]
    if len(terminal) != 1 or number(terminal[0], "completed") != 10_000 or \
            number(terminal[0], "errors") != 0:
        fail("terminal telemetry is not clean")
    ring = one(messages, "RESOURCE_RING_RETIRED")
    if ring.get("reusable") != "true" or ring.get("tokens") != "exact":
        fail("transient ring retirement mismatch")
    resource = one(messages, "BSP_RESOURCE_READBACK")
    if (not require_lighting and
            resource.get("buffer0") == resource.get("buffer1")) or \
            number(resource, "bright_pixels0") <= 0 or \
            number(resource, "bright_pixels1") <= 0 or \
            resource.get("guards") != "intact" or \
            number(resource, "frames") != 10_000 or \
            number(resource, "errors") != 0:
        fail("framebuffer readback contract mismatch")
    dynamic = one(messages, "DYNAMIC_LIGHTMAP_READBACK")
    if dynamic.get("surrounding") != "stable" or \
            dynamic.get("guards") != "intact" or \
            number(dynamic, "frames") != 10_000:
        fail("dynamic lightmap readback mismatch")
    if require_lighting:
        if dynamic.get("slots_equal") != "true" or \
                dynamic.get("final_mode") != "base":
            fail("lighting final atlas convergence mismatch")
    elif dynamic.get("buffers_distinct") != "true":
        fail("dynamic lightmap framebuffer mismatch")
    complete = one(messages, "GOLDSRC_PIPELINE_GATE_COMPLETE")
    if complete.get("schema") != "1" or \
            number(complete, "frames") != 10_000 or \
            number(complete, "semantic_permutations") != 99 or \
            number(complete, "shader_variants") != 9 or \
            complete.get("opaque_shader") != "surface_lightmap" or \
            complete.get("alpha_shader") != "masked_lightmap" or \
            complete.get("framebuffer_distinct") != "true" or \
            complete.get("input_required") != "false" or \
            complete.get("tokens") != "exact" or \
            complete.get("guards") != "intact" or \
            number(complete, "errors") != 0:
        fail("pipeline completion contract mismatch")
    if require_viewport:
        viewport_complete = one(messages, "GOLDSRC_VIEWPORT_GATE_COMPLETE")
        if viewport_complete.get("schema") != "1" or \
                number(viewport_complete, "frames") != 10_000 or \
                viewport_complete.get("sequence") != \
                "full-clear,inset-opaque,inset-alpha,full-restore" or \
                viewport_complete.get("framebuffer_distinct") != "true" or \
                viewport_complete.get("input_required") != "false" or \
                viewport_complete.get("tokens") != "exact" or \
                viewport_complete.get("guards") != "intact" or \
                number(viewport_complete, "errors") != 0:
            fail("viewport completion contract mismatch")
    if require_matrix:
        matrix_complete = one(messages, "GOLDSRC_STATE_MATRIX_COMPLETE")
        if matrix_complete.get("schema") != "1" or \
                number(matrix_complete, "frames") != 10_000 or \
                number(matrix_complete, "cases") != 9 or \
                number(matrix_complete, "readbacks") != 18 or \
                matrix_complete.get("both_slots") != "true" or \
                matrix_complete.get("control_pairs") != "distinct" or \
                matrix_complete.get("coverage") != \
                "opaque+alpha+additive+alpha-test+depth-write+" \
                "cull-front-back-none+fog+lightmap" or \
                matrix_complete.get("tokens") != "exact" or \
                matrix_complete.get("guards") != "intact" or \
                number(matrix_complete, "errors") != 0:
            fail("state-matrix completion contract mismatch")
    if require_2d:
        complete_2d = one(messages, "GOLDSRC_2D_COMPLETE")
        if complete_2d.get("schema") != "1" or \
                number(complete_2d, "frames") != 10_000 or \
                number(complete_2d, "draws_per_frame") != 2 or \
                complete_2d.get("passes") != "alpha+additive" or \
                complete_2d.get("projection") != "orthographic" or \
                complete_2d.get("components") != \
                "hud+console+menu+font" or \
                complete_2d.get("atlas") != "procedural-rgba8" or \
                complete_2d.get("geometry") != "per-frame-transient" or \
                complete_2d.get("tokens") != "exact" or \
                complete_2d.get("guards") != "intact" or \
                number(complete_2d, "errors") != 0:
            fail("2D completion contract mismatch")
    if require_lighting:
        complete_lighting = one(messages, "GOLDSRC_LIGHTING_COMPLETE")
        if complete_lighting.get("schema") != "1" or \
                number(complete_lighting, "frames") != 10_000 or \
                number(complete_lighting, "modes") != 4 or \
                number(complete_lighting, "readbacks") != 8 or \
                complete_lighting.get("both_slots") != "true" or \
                complete_lighting.get("control_pairs") != "distinct" or \
                complete_lighting.get("lightstyles") != "real-bsp-planes" or \
                number(complete_lighting, "animated_hz") != 10 or \
                complete_lighting.get("dynamic_lights") != \
                    "face-local-radial" or \
                complete_lighting.get("atlas_upload") != "phase3-bounded" or \
                number(complete_lighting, "styles") < 2 or \
                number(complete_lighting, "styled_faces") <= 0 or \
                number(complete_lighting, "styled_layers") <= 0 or \
                complete_lighting.get("tokens") != "exact" or \
                complete_lighting.get("guards") != "intact" or \
                number(complete_lighting, "errors") != 0:
            fail("lighting completion contract mismatch")
    if require_sprite_particles:
        complete_effects = one(
            messages, "GOLDSRC_SPRITE_PARTICLE_COMPLETE")
        if complete_effects.get("schema") != "1" or \
                number(complete_effects, "frames") != 10_000 or \
                number(complete_effects, "modes") != 4 or \
                number(complete_effects, "readbacks") != 8 or \
                complete_effects.get("both_slots") != "true" or \
                complete_effects.get("control_pairs") != "distinct" or \
                complete_effects.get("combined_pairs") != "distinct" or \
                number(complete_effects, "sprite_quads") != 1 or \
                number(complete_effects, "alpha_particles") != 24 or \
                number(complete_effects, "additive_particles") != 48 or \
                complete_effects.get("batches") != "alpha+additive" or \
                complete_effects.get("geometry") != "per-frame-transient" or \
                complete_effects.get("camera") != "locked-proof-view" or \
                complete_effects.get("input_dependency") != "none" or \
                complete_effects.get("tokens") != "exact" or \
                complete_effects.get("guards") != "intact" or \
                number(complete_effects, "errors") != 0:
            fail("sprite/particle completion contract mismatch")
    pool = one(messages, "RESOURCE_POOL_RETIRED")
    if number(pool, "reclaimed") != 6 or \
            pool.get("completion") != "fence+videoout":
        fail("resource pool retirement mismatch")
    return {
        "manifest": str(path.resolve()),
        "sha256": hashlib.sha256(data).hexdigest(),
        "frames": 10_000,
        "viewport": require_viewport,
        "matrix": require_matrix,
        "screen_2d": require_2d,
        "lighting": require_lighting,
        "sprite_particles": require_sprite_particles,
        "semantic_permutations": 99,
        "shader_variants": 9,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--bundle-sha256", required=True)
    parser.add_argument("--bundle-bytes", required=True, type=int)
    parser.add_argument("--require-viewport", action="store_true")
    parser.add_argument("--require-matrix", action="store_true")
    parser.add_argument("--require-2d", action="store_true")
    parser.add_argument("--require-lighting", action="store_true")
    parser.add_argument("--require-sprite-particles", action="store_true")
    args = parser.parse_args()
    try:
        result = validate(args.manifest,
                          bundle_sha256=args.bundle_sha256,
                          bundle_bytes=args.bundle_bytes,
                          require_viewport=args.require_viewport,
                          require_matrix=args.require_matrix,
                          require_2d=args.require_2d,
                          require_lighting=args.require_lighting,
                          require_sprite_particles=
                              args.require_sprite_particles)
    except EvidenceError as exc:
        raise SystemExit(f"Phase 4 evidence validation failed: {exc}") from exc
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
