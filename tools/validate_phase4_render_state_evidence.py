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
    if not lines[-1].startswith("BYE seq=") or \
            "reason=bsp-texture-path-lightmap-soak-complete" not in lines[-1]:
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
             bundle_bytes: int, require_viewport: bool) -> dict[str, object]:
    messages, data = load(path.resolve())
    boot = one(messages, "BSP_TEXTURE_PATH_BOOT")
    if boot.get("schema") != "1" or boot.get("target") != "gfx1013" or \
            boot.get("fw") != "12.02" or \
            boot.get("ownership") != "fence+videoout" or \
            boot.get("bundle_sha256") != bundle_sha256 or \
            number(boot, "bundle_bytes") != bundle_bytes or \
            number(boot, "soak_frames") != 10_000:
        fail("Phase 4 boot contract mismatch")
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
    if resource.get("buffer0") == resource.get("buffer1") or \
            number(resource, "bright_pixels0") <= 0 or \
            number(resource, "bright_pixels1") <= 0 or \
            resource.get("guards") != "intact" or \
            number(resource, "frames") != 10_000 or \
            number(resource, "errors") != 0:
        fail("framebuffer readback contract mismatch")
    dynamic = one(messages, "DYNAMIC_LIGHTMAP_READBACK")
    if dynamic.get("buffers_distinct") != "true" or \
            dynamic.get("surrounding") != "stable" or \
            dynamic.get("guards") != "intact" or \
            number(dynamic, "frames") != 10_000:
        fail("dynamic lightmap readback mismatch")
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
    pool = one(messages, "RESOURCE_POOL_RETIRED")
    if number(pool, "reclaimed") != 6 or \
            pool.get("completion") != "fence+videoout":
        fail("resource pool retirement mismatch")
    return {
        "manifest": str(path.resolve()),
        "sha256": hashlib.sha256(data).hexdigest(),
        "frames": 10_000,
        "viewport": require_viewport,
        "semantic_permutations": 99,
        "shader_variants": 9,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--bundle-sha256", required=True)
    parser.add_argument("--bundle-bytes", required=True, type=int)
    parser.add_argument("--require-viewport", action="store_true")
    args = parser.parse_args()
    try:
        result = validate(args.manifest,
                          bundle_sha256=args.bundle_sha256,
                          bundle_bytes=args.bundle_bytes,
                          require_viewport=args.require_viewport)
    except EvidenceError as exc:
        raise SystemExit(f"Phase 4 evidence validation failed: {exc}") from exc
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
