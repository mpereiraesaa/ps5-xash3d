#!/usr/bin/env python3
"""Fail-closed validation for a Xash3D dedicated engine boot ps5log/1 run.

The engine boot title captures stdio into the telemetry stream, so the
transcript mixes structured records (seq, mono_ns, level, text) with RAW
console lines. Structured records must stay contiguous and free of ERROR
levels; RAW lines are the engine console and are searched for the boot,
filesystem, spawn and bounded-quit proofs.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path


class EvidenceError(RuntimeError):
    pass


TITLE = "PPSA99996"
APP = "xash3d-engine"
BYE_REASON = "xash-engine-boot-complete"
HEX7 = re.compile(r"[0-9a-f]{7}")
FATAL_CONSOLE = ("Host_Error:", "Sys_Error:", "FS_Init: couldn't", "couldn't determine current directory")


def fail(message: str) -> None:
    raise EvidenceError(message)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def parse_fields(message: str) -> dict[str, str]:
    fields: dict[str, str] = {}
    for token in message.split()[1:]:
        if "=" not in token:
            fail(f"malformed marker field: {token}")
        key, value = token.split("=", 1)
        fields[key] = value
    return fields


def one(messages: list[str], prefix: str) -> dict[str, str]:
    matches = [message for message in messages if message.startswith(prefix + " ")]
    if len(matches) != 1:
        fail(f"expected exactly one {prefix}, found {len(matches)}")
    return parse_fields(matches[0])


def one_where(messages: list[str], prefix: str, key: str, value: str) -> dict[str, str]:
    matches = []
    for message in messages:
        if message.startswith(prefix + " "):
            fields = parse_fields(message)
            if fields.get(key) == value:
                matches.append(fields)
    if len(matches) != 1:
        fail(f"expected exactly one {prefix} with {key}={value}, found {len(matches)}")
    return matches[0]


def one_raw(lines: list[str], prefix: str) -> dict[str, str]:
    matches = [line[line.index(prefix):] for line in lines if prefix in line]
    if len(matches) != 1:
        fail(f"expected exactly one raw {prefix}, found {len(matches)}")
    return parse_fields(matches[0])


def split_transcript(lines: list[str]) -> tuple[list[tuple[int, str, str]], list[str]]:
    structured: list[tuple[int, str, str]] = []
    raw: list[str] = []
    for line in lines:
        parts = line.split("\t", 3)
        if len(parts) == 4:
            try:
                seq = int(parts[0], 10)
                int(parts[1], 10)
            except ValueError:
                raw.append(line)
                continue
            structured.append((seq, parts[2].strip() or "INFO", parts[3]))
        else:
            raw.append(line)
    return structured, raw


AUDIO_INPUT_RATE = 44100
AUDIO_OUTPUT_RATE = 48000
AUDIO_RATIO_NUM = 147
AUDIO_RATIO_DEN = 160
AUDIO_GRAIN = 256


def validate_audio_gate(messages: list[str]) -> dict[str, str]:
    """Fail-closed check of the SceAudioOut gate markers.

    The audible confirmation is external evidence tied to the run id and is
    deliberately not derivable from the transcript; everything the device can
    prove about the port, the ring, the resampler and the teardown is here.
    """
    init = one(messages, "XASH_AUDIO_INIT")
    ring = one(messages, "XASH_AUDIO_RING_READY")
    pattern = one(messages, "XASH_AUDIO_PATTERN")
    summary = one(messages, "XASH_AUDIO_SUMMARY")
    teardown = one(messages, "XASH_AUDIO_TEARDOWN")
    complete = one(messages, "XASH_AUDIO_COMPLETE")
    user = one(messages, "XASH_AUDIO_USER")

    for name, fields in (
        ("init", init), ("ring", ring), ("pattern", pattern),
        ("summary", summary), ("teardown", teardown), ("complete", complete),
        ("user", user),
    ):
        if fields.get("schema") != "1":
            fail(f"audio {name} marker is not schema 1")

    # Port contract: main port, index 0, stereo signed-16 at 48 kHz, grain 256.
    if init.get("type") != "0" or init.get("index") != "0":
        fail("audio port type/index is not the main port")
    if init.get("format") != "1" or init.get("channels") != "2":
        fail("audio format is not stereo signed-16")
    if init.get("input_rate") != str(AUDIO_INPUT_RATE):
        fail("audio input rate is not the Xash mix rate")
    if init.get("output_rate") != str(AUDIO_OUTPUT_RATE):
        fail("audio output rate is not 48 kHz")
    if init.get("grain") != str(AUDIO_GRAIN):
        fail("audio grain is not 256 frames")
    if init.get("volume_flags") != "3" or init.get("volume_value") != "0x8000":
        fail("audio volume flags/value are not the 0 dB contract")
    for field in ("init_rc", "open_rc", "volume_rc"):
        if int(init.get(field, "-1"), 10) < 0:
            fail(f"audio acquisition failed: {field}")
    if int(init.get("handle", "-1"), 10) < 0:
        fail("audio handle was not opened")
    # Which user the artifact actually used, recorded rather than inferred.
    if user.get("source") not in ("system", "foreground"):
        fail("audio user source is neither system nor foreground")
    if user.get("source") == "system" and init.get("user") != "0xff":
        fail("system audio user must open the port as 0xff")

    if ring.get("ratio") != f"{AUDIO_RATIO_NUM}/{AUDIO_RATIO_DEN}":
        fail("audio resampler ratio is not 147/160")
    capacity = int(ring.get("capacity_frames", "0"), 10)
    if capacity <= 0 or capacity & (capacity - 1):
        fail("audio ring capacity is not a power of two")
    prime = int(ring.get("prime_frames", "0"), 10)
    if not 0 < prime < capacity:
        fail("audio prime level is outside the ring")

    # The pattern the operator was asked to listen to.
    pattern_frames = int(pattern.get("frames", "0"), 10)
    if pattern.get("segments") != "3" or pattern_frames <= 0:
        fail("audio pattern is not the three-segment sequence")
    if int(pattern.get("silent_frames", "0"), 10) <= 0:
        fail("audio pattern carries no deliberate silence")
    if pattern.get("rate") != str(AUDIO_INPUT_RATE):
        fail("audio pattern is not generated at the Xash mix rate")

    consumed = int(summary.get("consumed", "0"), 10)
    produced = int(summary.get("produced", "0"), 10)
    sent = int(summary.get("sent", "0"), 10)
    blocks = int(summary.get("blocks", "0"), 10)
    padding = int(summary.get("padding", "0"), 10)
    if produced != pattern_frames or consumed != pattern_frames:
        fail("audio run did not carry the whole pattern")
    if summary.get("underruns") != "0":
        fail("audio run contains unintended underruns")
    if summary.get("output_errors") != "0":
        fail("audio run contains Output errors")
    if summary.get("discarded") != "0":
        fail("audio run discarded queued frames")
    if summary.get("rebases") != "0":
        fail("audio run rebased its producer cursor")
    if blocks <= 0 or sent != blocks * AUDIO_GRAIN:
        fail("audio output was not whole 256-frame blocks")
    if int(summary.get("silent", "0"), 10) < int(pattern.get("silent_frames", "0"), 10):
        fail("audio run did not carry the deliberate silence through")
    if int(summary.get("high_water", "0"), 10) > capacity:
        fail("audio ring high-water mark exceeded its capacity")

    # Exact conversion: priming takes one source frame, then ceil(M*160/147).
    expected = -(-(consumed - 1) * AUDIO_RATIO_DEN // AUDIO_RATIO_NUM)
    if sent != expected + padding:
        fail(f"audio ratio mismatch: sent {sent}, expected {expected} + {padding} padding")
    if not 0 <= padding < AUDIO_GRAIN:
        fail("audio terminal padding is not confined to one block")

    if summary.get("source_hash") != pattern.get("source_hash"):
        fail("consumed PCM hash does not match the generated pattern")
    if summary.get("output_hash") in (None, "0x0000000000000000"):
        fail("audio run recorded no post-resampler hash")

    if teardown.get("owner") != "worker":
        fail("audio teardown was not owned by the worker")
    for field, value in (("drain_calls", "1"), ("close_calls", "1"), ("join_calls", "1")):
        if teardown.get(field) != value:
            fail(f"audio teardown {field} is not exactly one")
    # FW 12.02 returns the number of frames accepted from Output and from the
    # NULL drain (256 at this grain), so success is non-negative, not zero.
    if int(teardown.get("drain_rc", "-1"), 10) < 0:
        fail("audio drain returned an error")
    for field in ("close_rc", "result"):
        if teardown.get(field) != "0":
            fail(f"audio teardown {field} is not clean")

    if complete.get("pass") != "1" or complete.get("ownership") != "exact":
        fail("audio completion marker did not pass")
    if complete.get("source_hash") != complete.get("expected_source_hash"):
        fail("audio completion hash does not match the expected pattern hash")
    if complete.get("shutdown_rc") != "0":
        fail("audio shutdown returned an error")

    # Progress lines exist but are not emitted per block.
    progress = [m for m in messages if m.startswith("XASH_AUDIO_PROGRESS ")]
    if not progress:
        fail("audio run reported no progress")
    if len(progress) >= blocks:
        fail("audio telemetry emitted a line per block")
    if any(m.startswith("XASH_AUDIO_UNDERRUN ") for m in messages):
        fail("audio run logged an underrun episode")
    return complete


def validate_memory_gate(messages: list[str]) -> dict[str, str]:
    """Validate the direct-memory arena, GPU lifetime and root teardown."""
    begin = one(messages, "XASH_MEMORY_BEGIN")
    complete = one(messages, "XASH_MEMORY_COMPLETE")
    summary = one(messages, "XASH_MEMORY_SUMMARY")
    teardown = one(messages, "XASH_MEMORY_TEARDOWN")
    if begin.get("schema") != "1" or begin.get("arena") != "direct" \
            or begin.get("root_mib") != "128":
        fail("direct-memory root contract mismatch")

    resources = [parse_fields(message) for message in messages
                 if message.startswith("XASH_MEMORY_RESOURCE ")]
    expected = {
        "command": (2 * 1024 * 1024, 256),
        "buffer": (4 * 1024 * 1024, 65536),
        "texture": (8 * 1024 * 1024, 65536),
        "depth": (4 * 1024 * 1024, 65536),
    }
    if len(resources) != len(expected) or {item.get("kind") for item in resources} != set(expected):
        fail("direct-memory resource set mismatch")
    generations: set[int] = set()
    for resource in resources:
        kind = resource["kind"]
        if (int(resource.get("bytes", "0"), 10),
                int(resource.get("alignment", "0"), 10)) != expected[kind]:
            fail(f"direct-memory {kind} size/alignment mismatch")
        generation = int(resource.get("generation", "0"), 10)
        if generation <= 0 or generation in generations:
            fail("direct-memory generations are not unique and non-zero")
        generations.add(generation)
        if resource.get("owner") != "gpu-active" or not re.fullmatch(
                r"[0-9a-f]{16}", resource.get("hash", "")):
            fail(f"direct-memory {kind} ownership/hash mismatch")

    if complete.get("schema") != "1" or complete.get("result") != "0" \
            or complete.get("resources") != "4" \
            or complete.get("resource_bytes") != str(18 * 1024 * 1024) \
            or complete.get("completion") != "synthetic-contract" \
            or complete.get("guards") != "intact" \
            or complete.get("alloc_failures") != "0" \
            or complete.get("root_calls") != "1/1/1" \
            or complete.get("pass") != "1":
        fail("direct-memory exercise did not complete cleanly")
    for field in ("live_bytes", "live_cpu", "live_gpu", "retiring_gpu"):
        if complete.get(field) != "0":
            fail(f"direct-memory exercise retained {field}")

    if summary.get("schema") != "1" \
            or summary.get("arena_bytes") != str(128 * 1024 * 1024) \
            or summary.get("pass") != "1":
        fail("direct-memory final arena summary mismatch")
    for field in ("live_gpu", "retiring_gpu", "failures",
                  "guard_failures", "stale_errors"):
        if summary.get(field) != "0":
            fail(f"direct-memory final summary reports {field}")
    if summary.get("process_lifetime_cpu") != summary.get("live_cpu") \
            or summary.get("process_lifetime_bytes") != summary.get("live_bytes"):
        fail("direct-memory process-lifetime accounting mismatch")
    if int(summary.get("retire_calls", "0"), 10) < 4 \
            or summary.get("retire_calls") != summary.get("reclaim_calls"):
        fail("direct-memory retirement/reclamation is not balanced")

    if teardown.get("schema") != "1" or teardown.get("result") != "0" \
            or teardown.get("ownership") != "exact" \
            or teardown.get("pass") != "1":
        fail("direct-memory teardown contract failed")
    for field in ("reserve_calls", "allocate_calls", "map_calls",
                  "unmap_calls", "release_calls"):
        if teardown.get(field) != "1":
            fail(f"direct-memory teardown has non-exact {field}")
    for field in ("reserve_rc", "allocate_rc", "map_rc", "unmap_rc",
                  "release_rc", "mapped", "allocated", "live_bytes",
                  "live_cpu", "live_gpu", "retiring_gpu"):
        if teardown.get(field) != "0":
            fail(f"direct-memory teardown reports {field}")
    if teardown.get("lifetime_reclaims") != summary.get("process_lifetime_cpu") \
            or teardown.get("lifetime_bytes") != summary.get("process_lifetime_bytes"):
        fail("direct-memory process-lifetime reclamation mismatch")
    return summary


def validate_thread_time_gate(messages: list[str]) -> dict[str, str]:
    """Validate pthread lifecycle, CLOCK_MONOTONIC and measured sleeps."""
    begin = one(messages, "XASH_THREAD_TIME_BEGIN")
    thread = one(messages, "XASH_THREAD_RESULT")
    clock = one(messages, "XASH_CLOCK_RESULT")
    complete = one(messages, "XASH_THREAD_TIME_COMPLETE")
    sleeps = [parse_fields(message) for message in messages
              if message.startswith("XASH_SLEEP_RESULT ")]

    for name, fields in (("begin", begin), ("thread", thread),
                         ("clock", clock), ("complete", complete)):
        if fields.get("schema") != "1":
            fail(f"thread/time {name} marker is not schema 1")
    expected_begin = {
        "workers": "2", "iterations": "16384", "clock_samples": "8192",
        "sleep_samples": "16", "sleep_buckets": "8",
    }
    if any(begin.get(key) != value for key, value in expected_begin.items()):
        fail("thread/time workload contract mismatch")

    exact_thread = {
        "create_calls": "2", "create_join_rc": "0", "create_detach_rc": "0",
        "join_calls": "1", "join_rc": "0", "detach_calls": "1",
        "detach_rc": "0", "completions": "2", "detached_complete": "1",
        "distinct": "2", "mutex_init_rc": "0", "mutex_destroy_rc": "0",
        "mutex_errors": "0", "counter": "32768", "expected": "32768",
        "ownership": "exact", "pass": "1",
    }
    if any(thread.get(key) != value for key, value in exact_thread.items()):
        fail("pthread execution or exact teardown contract mismatch")

    if clock.get("clock") != "monotonic" or clock.get("reads") != "8192" \
            or clock.get("errors") != "0" or clock.get("regressions") != "0" \
            or clock.get("pass") != "1":
        fail("monotonic clock contract mismatch")
    for field in ("advances", "min_step_ns", "span_ns"):
        if int(clock.get(field, "0"), 10) <= 0:
            fail(f"monotonic clock lacks positive {field}")

    expected_sleeps = {(api, requested) for api in ("nanosleep", "usleep")
                       for requested in (1000, 2000, 5000, 10000)}
    observed_sleeps: set[tuple[str, int]] = set()
    if len(sleeps) != 8:
        fail(f"expected 8 sleep buckets, found {len(sleeps)}")
    for sleep in sleeps:
        if sleep.get("schema") != "1" or sleep.get("samples") != "16" \
                or sleep.get("errors") != "0" or sleep.get("early") != "0" \
                or sleep.get("pass") != "1":
            fail("sleep bucket reports an error or early wake")
        api = sleep.get("api", "")
        requested = int(sleep.get("requested_us", "0"), 10)
        observed_sleeps.add((api, requested))
        minimum = int(sleep.get("min_ns", "0"), 10)
        average = int(sleep.get("average_ns", "0"), 10)
        p95 = int(sleep.get("p95_ns", "0"), 10)
        maximum = int(sleep.get("max_ns", "0"), 10)
        if not 0 < minimum <= average <= maximum or not minimum <= p95 <= maximum:
            fail("sleep distribution fields are inconsistent")
        if minimum + 50_000 < requested * 1000:
            fail("sleep bucket woke earlier than its tolerance")
        if maximum > 500_000_000:
            fail("sleep bucket exceeded the bounded scheduler allowance")
    if observed_sleeps != expected_sleeps:
        fail("sleep API/request matrix mismatch")

    exact_complete = {
        "create": "2", "join": "1", "detach": "1", "workers": "2",
        "counter": "32768", "clock_regressions": "0", "sleep_errors": "0",
        "sleep_early": "0", "ownership": "exact", "pass": "1",
    }
    if any(complete.get(key) != value for key, value in exact_complete.items()):
        fail("thread/time completion contract failed")
    return complete


def validate_libc_shim_gate(messages: list[str]) -> dict[str, str]:
    """Validate the three project-owned libc compatibility shims."""
    begin = one(messages, "XASH_LIBC_SHIM_BEGIN")
    end = one(messages, "XASH_LIBC_SHIM_END")
    results = [parse_fields(message) for message in messages
               if message.startswith("XASH_LIBC_SHIM_RESULT ")]

    if begin.get("schema") != "1" \
            or begin.get("symbols") != "__assert,getpwuid,dladdr":
        fail("libc shim workload contract mismatch")
    if len(results) != 3:
        fail(f"expected 3 libc shim results, found {len(results)}")
    by_symbol = {result.get("symbol", ""): result for result in results}
    if set(by_symbol) != {"__assert", "getpwuid", "dladdr"}:
        fail("libc shim result symbol set mismatch")

    assertion = by_symbol["__assert"]
    expected_assert = {
        "implementation": "project-owned", "reporter": "ps5log",
        "abort": "noreturn", "format_pass": "1",
    }
    if any(assertion.get(key) != value for key, value in expected_assert.items()):
        fail("project-owned __assert contract failed")

    identity = by_symbol["getpwuid"]
    expected_identity = {
        "implementation": "project-owned", "requested_uid": "0xff",
        "returned_uid": "0xff", "username": "ps5", "pass": "1",
    }
    if any(identity.get(key) != value for key, value in expected_identity.items()):
        fail("fixed getpwuid identity contract failed")

    address = by_symbol["dladdr"]
    expected_address = {
        "implementation": "project-owned", "result": "0",
        "fallback": "argv0", "info": "zeroed", "pass": "1",
    }
    if any(address.get(key) != value for key, value in expected_address.items()):
        fail("deterministic dladdr fallback contract failed")
    if end.get("pass") != "1":
        fail("libc shim completion contract failed")
    return end


def validate_prx_gate(messages: list[str]) -> dict[str, str]:
    """Validate load, descriptor resolution, calls and exact PRX teardown."""
    begin = one(messages, "XASH_PRX_BEGIN")
    load = one_where(messages, "XASH_PRX_LOAD", "module", "xash_prx_probe.prx")
    resolve = one(messages, "XASH_PRX_RESOLVE")
    call = one(messages, "XASH_PRX_CALL")
    unload = one_where(messages, "XASH_PRX_UNLOAD", "module", "xash_prx_probe.prx")
    complete = one(messages, "XASH_PRX_COMPLETE")

    if begin != {
        "schema": "1", "backend": "COM_LoadLibrary",
        "resolver": "PRXDESC1", "module": "xash_prx_probe.prx",
    }:
        fail("PRX loader workload contract mismatch")
    if load.get("path") != "/app0/sce_module/xash_prx_probe.prx" \
            or load.get("module") != "xash_prx_probe.prx" \
            or load.get("result") != "0" \
            or not 1 <= int(load.get("segments", "0"), 10) <= 4 \
            or load.get("exports") != "6" \
            or not re.fullmatch(r"0x[1-9a-f][0-9a-f]*", load.get("handle", "")):
        fail("PRX load marker is incomplete")
    required_resolve = ("add", "sleep_count", "module_start", "version", "started", "pass")
    if any(resolve.get(field) != "1" for field in required_resolve) \
            or resolve.get("missing") != "0":
        fail("PRX descriptor resolution contract failed")
    if call.get("add") != "42" or call.get("count") != "2" \
            or call.get("version") != "0x10000" \
            or call.get("kernel_import") != "sceKernelUsleep" \
            or call.get("name_roundtrip") != "1" or call.get("pass") != "1" \
            or call.get("started") != "1":
        fail("PRX call contract failed")
    if call.get("auto_started") not in ("0", "1"):
        fail("PRX automatic-start observation is invalid")
    if call.get("auto_started") == "0" and call.get("manual_start_rc") != "0":
        fail("PRX explicit initialization did not succeed")
    if unload.get("result") != "0" or unload.get("ownership") != "released":
        fail("PRX unload did not release ownership")
    required_complete = {
        "pass": "1", "load": "1", "resolve": "1", "call": "1",
        "unload": "1", "active": "0", "ownership": "exact",
    }
    if any(complete.get(key) != value for key, value in required_complete.items()):
        fail("PRX completion contract failed")
    return complete


def validate_filesystem_prx_gate(
    messages: list[str], raw: list[str]
) -> dict[str, str]:
    """Validate the first real engine module and its filesystem workload."""
    load = one_where(messages, "XASH_PRX_LOAD", "module", "filesystem_stdio.prx")
    ready = one(messages, "XASH_FS_PRX_READY")
    state = one(messages, "XASH_FS_PRX_STATE")
    unload = one_where(messages, "XASH_PRX_UNLOAD", "module", "filesystem_stdio.prx")
    complete = one(messages, "XASH_FS_PRX_COMPLETE")
    probe = one_raw(raw, "XASH_FS_PRX_PROBE")

    if load.get("path") != "/app0/sce_module/filesystem_stdio.prx" \
            or load.get("module") != "filesystem_stdio.prx" \
            or load.get("result") != "0" \
            or load.get("exports") != "8" \
            or not 1 <= int(load.get("segments", "0"), 10) <= 4:
        fail("filesystem PRX load contract failed")
    for marker, fields in (("ready", ready), ("state", state)):
        if fields.get("module") != "filesystem_stdio.prx" \
                or fields.get("index_entries") != "4823" \
                or fields.get("allocator_contract") != "libc-shared" \
                or fields.get("allocator_result") != "0" \
                or fields.get("listing_refused") != "0" \
                or fields.get("resolver") != "PRXDESC1":
            fail(f"filesystem PRX {marker} contract failed")
    if probe.get("schema") != "1" or probe.get("index_entries") != "4823" \
            or probe.get("listing_pattern") != "gfx/*" \
            or int(probe.get("listing_matches", "0"), 10) <= 0 \
            or probe.get("case_path") != "GfX/PaLeTtE.LmP" \
            or probe.get("palette_bytes") != "768" \
            or probe.get("large_path") != "maps/c1a0.bsp" \
            or int(probe.get("large_bytes", "0"), 10) < 1048576 \
            or probe.get("pass") != "1":
        fail("filesystem PRX lookup/read/case workload failed")
    for name in ("palette_hash", "large_hash"):
        if not re.fullmatch(r"[0-9a-f]{16}", probe.get(name, "")) \
                or probe[name] == "0000000000000000":
            fail(f"filesystem PRX {name} is missing")
    if unload.get("module") != "filesystem_stdio.prx" \
            or unload.get("result") != "0" \
            or unload.get("stop_result") != "0" \
            or unload.get("ownership") != "released":
        fail("filesystem PRX unload did not release ownership")
    required_complete = {
        "module": "filesystem_stdio.prx", "stop_result": "0",
        "active_modules": "0", "ownership": "exact",
    }
    if any(complete.get(key) != value for key, value in required_complete.items()):
        fail("filesystem PRX completion contract failed")
    return probe


def validate_server_prx_gate(
    messages: list[str], raw: list[str]
) -> dict[str, str]:
    """Validate the HLSDK server module, map workload and exact unload."""
    load = one_where(messages, "XASH_PRX_LOAD", "module", "server.prx")
    ready = one(messages, "XASH_SERVER_PRX_READY")
    abi = one(messages, "XASH_SERVER_PRX_ABI")
    state = one(messages, "XASH_SERVER_PRX_STATE")
    unload = one_where(messages, "XASH_PRX_UNLOAD", "module", "server.prx")
    complete = one(messages, "XASH_SERVER_PRX_COMPLETE")
    if load.get("path") != "/app0/sce_module/server.prx" \
            or load.get("result") != "0" \
            or load.get("init_result") != "0" \
            or load.get("exports") != "257" \
            or not 1 <= int(load.get("segments", "0"), 10) <= 4:
        fail("server PRX load contract failed")
    for marker, fields in (("ready", ready), ("state", state)):
        if fields.get("module") != "server.prx" \
                or fields.get("state") != "1" \
                or fields.get("exports") != "251" \
                or fields.get("resolver") != "PRXDESC1":
            fail(f"server PRX {marker} contract failed")
    if abi.get("engine_table_mask") != "7" \
            or abi.get("expected") != "7" or abi.get("pass") != "1":
        fail("server PRX engine callback ABI contract failed")
    smoke = [parse_fields(message) for message in messages
             if message.startswith("XASH_SERVER_PRX_ABI_SMOKE ")]
    for step in (1, 2):
        begin_smoke = [fields for fields in smoke
                       if fields.get("step") == str(step)
                       and fields.get("phase") == "begin"]
        complete_smoke = [fields for fields in smoke
                          if fields.get("step") == str(step)
                          and fields.get("phase") == "complete"]
        if len(begin_smoke) != 1 or len(complete_smoke) != 1 \
                or complete_smoke[0].get("result") != "1" \
                or complete_smoke[0].get("pass") != "1":
            fail(f"server PRX engine callback smoke step {step} failed")
    if len(smoke) != 4:
        fail("server PRX engine callback smoke contains unexpected records")
    console = "\n".join(raw)
    if 'Dll loaded for game "Half-Life"' not in console \
            or "4 player server started" not in console:
        fail("server PRX workload did not initialize the Half-Life server")
    if unload.get("result") != "0" or unload.get("stop_result") != "0" \
            or unload.get("ownership") != "released":
        fail("server PRX unload did not release ownership")
    expected = {"module": "server.prx", "stop_result": "0",
                "active_modules": "1", "ownership": "exact"}
    if any(complete.get(key) != value for key, value in expected.items()):
        fail("server PRX completion contract failed")
    ordered = (
        "XASH_FS_PRX_READY", "XASH_SERVER_PRX_READY", "XASH_SERVER_PRX_ABI",
        "XASH_SERVER_PRX_STATE", "XASH_SERVER_PRX_COMPLETE", "XASH_FS_PRX_STATE",
    )
    positions = [next(i for i, message in enumerate(messages)
                      if message.startswith(marker + " ")) for marker in ordered]
    if positions != sorted(positions) or len(set(positions)) != len(positions):
        fail("server/filesystem PRX lifecycle order is invalid")
    return complete


def validate_menu_prx_gate(
    messages: list[str], raw: list[str], client_prx_gate: bool = False
) -> dict[str, str]:
    """Validate the dynamic mainui ABI, visible redraw and exact teardown."""
    load = one_where(messages, "XASH_PRX_LOAD", "module", "menu.prx")
    ready = one(messages, "XASH_MENU_PRX_READY")
    api = one(messages, "XASH_MENU_PRX_API")
    ext_api = one(messages, "XASH_MENU_PRX_EXT_API")
    state = one(messages, "XASH_MENU_PRX_STATE")
    unload = one_where(messages, "XASH_PRX_UNLOAD", "module", "menu.prx")
    complete = one(messages, "XASH_MENU_PRX_COMPLETE")
    optional_vgui = one(messages, "XASH_PRX_OPTIONAL_MISS")
    server_load = one_where(messages, "XASH_PRX_LOAD", "module", "server.prx")
    server_ready = one(messages, "XASH_SERVER_PRX_READY")
    server_abi = one(messages, "XASH_SERVER_PRX_ABI")
    server_state = one(messages, "XASH_SERVER_PRX_STATE")
    server_unload = one_where(messages, "XASH_PRX_UNLOAD", "module", "server.prx")
    server_complete = one(messages, "XASH_SERVER_PRX_COMPLETE")

    if load.get("path") != "/app0/sce_module/menu.prx" \
            or load.get("result") != "0" or load.get("init_result") != "0" \
            or load.get("exports") != "6" \
            or not 1 <= int(load.get("segments", "0"), 10) <= 4:
        fail("menu PRX load contract failed")
    for marker, fields in (("ready", ready), ("state", state)):
        if fields.get("module") != "menu.prx" or fields.get("state") != "1" \
                or fields.get("exports") != "2" \
                or fields.get("resolver") != "PRXDESC1":
            fail(f"menu PRX {marker} contract failed")
    expected_api = {
        "result": "1", "callbacks": "16", "expected": "16",
        "engine_mask": "63", "expected_mask": "63", "globals": "1", "pass": "1",
    }
    if any(api.get(key) != value for key, value in expected_api.items()):
        fail("menu PRX base API contract failed")
    expected_ext = {
        "version": "1", "result": "1", "callbacks": "12", "expected": "12",
        "engine_mask": "15", "expected_mask": "15", "pass": "1",
    }
    if any(ext_api.get(key) != value for key, value in expected_ext.items()):
        fail("menu PRX extended API contract failed")
    if optional_vgui.get("path") != "/app0/sce_module/libvgui_support.prx" \
            or optional_vgui.get("module") != "libvgui_support.prx" \
            or optional_vgui.get("fallback") != "client-probe" \
            or optional_vgui.get("rollback") != "complete":
        fail("menu gate did not bound the optional legacy VGUI fallback")
    if server_load.get("exports") != "257" or server_load.get("result") != "0" \
            or server_load.get("init_result") != "0" \
            or server_ready.get("state") != "1" \
            or server_ready.get("exports") != "251" \
            or server_ready.get("resolver") != "PRXDESC1" \
            or server_abi.get("engine_table_mask") != "7" \
            or server_abi.get("pass") != "1" or server_state.get("state") != "1" \
            or server_unload.get("result") != "0" \
            or server_unload.get("ownership") != "released" \
            or server_complete.get("stop_result") != "0" \
            or server_complete.get("active_modules") != ("3" if client_prx_gate else "2") \
            or server_complete.get("ownership") != "exact":
        fail("menu gate did not preserve the dynamic server checkpoint")
    server_smoke = [parse_fields(message) for message in messages
                    if message.startswith("XASH_SERVER_PRX_ABI_SMOKE ")]
    if len(server_smoke) != 4:
        fail("menu gate server callback smoke contains unexpected records")
    for step in (1, 2):
        phases = {entry.get("phase") for entry in server_smoke
                  if entry.get("step") == str(step)}
        completed = [entry for entry in server_smoke
                     if entry.get("step") == str(step)
                     and entry.get("phase") == "complete"]
        if phases != {"begin", "complete"} or len(completed) != 1 \
                or completed[0].get("result") != "1" \
                or completed[0].get("pass") != "1":
            fail(f"menu gate server callback smoke step {step} failed")

    init = [parse_fields(message) for message in messages
            if message.startswith("XASH_MENU_PRX_INIT ")]
    shutdown = [parse_fields(message) for message in messages
                if message.startswith("XASH_MENU_PRX_SHUTDOWN ")]
    if len(init) != 2 or {entry.get("phase") for entry in init} != {"begin", "complete"} \
            or any(entry.get("call") != "1" for entry in init) \
            or init[-1].get("active_modules") != "3":
        fail("menu PRX initialization lifecycle failed")
    redraw = [parse_fields(message) for message in messages
              if message.startswith("XASH_MENU_PRX_REDRAW ")]
    if not client_prx_gate and (not redraw or
            not any(entry.get("visible") == "1" for entry in redraw)):
        fail("menu PRX produced no visible redraw")
    active = [parse_fields(message) for message in messages
              if message.startswith("XASH_MENU_PRX_ACTIVE ")]
    if not any(entry.get("active") == "1" for entry in active):
        fail("menu PRX was never activated")
    if len(shutdown) != 2 \
            or {entry.get("phase") for entry in shutdown} != {"begin", "complete"} \
            or any(entry.get("call") != "1" for entry in shutdown):
        fail("menu PRX shutdown lifecycle failed")
    if unload.get("result") != "0" or unload.get("stop_result") != "0" \
            or unload.get("ownership") != "released":
        fail("menu PRX unload did not release ownership")
    expected_complete = {
        "module": "menu.prx", "stop_result": "0", "api_pass": "1",
        "ext_api_pass": "1", "init_calls": "1", "shutdown_calls": "1",
        "active_modules": "2" if client_prx_gate else "1",
        "ownership": "exact", "pass": "1",
    }
    if any(complete.get(key) != value for key, value in expected_complete.items()) \
            or (not client_prx_gate and
                int(complete.get("redraw_calls", "0"), 10) <= 0) \
            or int(complete.get("active_calls", "0"), 10) <= 0:
        fail("menu PRX completion contract failed")
    ordered = (
        "XASH_FS_PRX_READY", "XASH_SERVER_PRX_READY", "XASH_SERVER_PRX_ABI",
        "XASH_MENU_PRX_READY", "XASH_MENU_PRX_API",
        "XASH_MENU_PRX_EXT_API", "XASH_MENU_PRX_INIT", "XASH_MENU_PRX_ACTIVE",
        "XASH_PRX_OPTIONAL_MISS", "XASH_MENU_PRX_REDRAW", "XASH_SERVER_PRX_STATE",
        "XASH_SERVER_PRX_COMPLETE", "XASH_MENU_PRX_SHUTDOWN", "XASH_MENU_PRX_STATE",
        "XASH_MENU_PRX_COMPLETE", "XASH_FS_PRX_STATE",
    )
    positions = [next(i for i, message in enumerate(messages)
                      if message.startswith(marker + " ")) for marker in ordered]
    if positions != sorted(positions) or len(set(positions)) != len(positions):
        fail("menu/filesystem PRX lifecycle order is invalid")
    return complete


def validate_client_prx_gate(
    messages: list[str], raw: list[str]
) -> dict[str, str]:
    """Validate the GoldSrc callback ABI, live use and exact client unload."""
    load = one_where(messages, "XASH_PRX_LOAD", "module", "client.prx")
    ready = one(messages, "XASH_CLIENT_PRX_READY")
    api = one(messages, "XASH_CLIENT_PRX_API")
    state = one(messages, "XASH_CLIENT_PRX_STATE")
    unload = one_where(messages, "XASH_PRX_UNLOAD", "module", "client.prx")
    complete = one(messages, "XASH_CLIENT_PRX_COMPLETE")

    if load.get("path") != "/app0/sce_module/client.prx" \
            or load.get("result") != "0" or load.get("init_result") != "0" \
            or load.get("exports") != "48" \
            or not 1 <= int(load.get("segments", "0"), 10) <= 4:
        fail("client PRX load contract failed")
    for marker, fields in (("ready", ready), ("state", state)):
        if fields.get("module") != "client.prx" or fields.get("state") != "1" \
                or fields.get("exports") != "42" \
                or fields.get("resolver") != "PRXDESC1":
            fail(f"client PRX {marker} contract failed")
    expected_api = {
        "result": "1", "version": "7", "expected_version": "7",
        "engine_mask": "63", "expected_mask": "63", "module_mask": "15",
        "expected_module_mask": "15", "calls": "1", "pass": "1",
    }
    if any(api.get(key) != value for key, value in expected_api.items()):
        fail("client PRX API contract failed")
    smoke = [parse_fields(message) for message in messages
             if message.startswith("XASH_CLIENT_PRX_ABI_SMOKE ")]
    if len(smoke) != 2 or {entry.get("step") for entry in smoke} != {"1", "2"} \
            or any(entry.get("result") != "1" or entry.get("pass") != "1"
                   for entry in smoke):
        fail("client PRX engine callback smoke failed")
    init = [parse_fields(message) for message in messages
            if message.startswith("XASH_CLIENT_PRX_INIT ")]
    shutdown = [parse_fields(message) for message in messages
                if message.startswith("XASH_CLIENT_PRX_SHUTDOWN ")]
    if len(init) != 2 or {entry.get("phase") for entry in init} != {"begin", "complete"} \
            or any(entry.get("call") != "1" for entry in init) \
            or init[-1].get("active_modules") != "4":
        fail("client PRX initialization lifecycle failed")
    if len(shutdown) != 2 \
            or {entry.get("phase") for entry in shutdown} != {"begin", "complete"} \
            or any(entry.get("call") != "1" for entry in shutdown):
        fail("client PRX shutdown lifecycle failed")
    vid_init = one(messages, "XASH_CLIENT_PRX_VID_INIT")
    frame = one(messages, "XASH_CLIENT_PRX_FRAME")
    redraw = one(messages, "XASH_CLIENT_PRX_REDRAW")
    if vid_init.get("result") != "1" or vid_init.get("pass") != "1" \
            or int(vid_init.get("call", "0"), 10) <= 0:
        fail("client PRX video initialization failed")
    if frame.get("call") != "1" or frame.get("active_modules") != "4":
        fail("client PRX produced no live frame callback")
    if redraw.get("call") != "1" or redraw.get("result") != "1":
        fail("client PRX produced no successful HUD redraw")
    console = "\n".join(raw)
    if "Spawn Server: c1a0" not in console or \
            'Dll loaded for game "Half-Life"' not in console:
        fail("client PRX workload did not enter the Half-Life map")
    if unload.get("result") != "0" or unload.get("stop_result") != "0" \
            or unload.get("ownership") != "released":
        fail("client PRX unload did not release ownership")
    expected_complete = {
        "module": "client.prx", "stop_result": "0", "api_pass": "1",
        "abi_pass": "1", "initialize_calls": "1", "init_calls": "1",
        "shutdown_calls": "1", "active_modules": "1", "ownership": "exact",
        "pass": "1",
    }
    if any(complete.get(key) != value for key, value in expected_complete.items()) \
            or int(complete.get("vid_init_calls", "0"), 10) <= 0 \
            or int(complete.get("frame_calls", "0"), 10) <= 0 \
            or int(complete.get("redraw_calls", "0"), 10) <= 0:
        fail("client PRX completion contract failed")
    ordered = (
        "XASH_CLIENT_PRX_READY", "XASH_CLIENT_PRX_ABI_SMOKE",
        "XASH_CLIENT_PRX_API", "XASH_CLIENT_PRX_INIT", "XASH_CLIENT_PRX_FRAME",
        "XASH_CLIENT_PRX_SHUTDOWN", "XASH_CLIENT_PRX_STATE",
        "XASH_CLIENT_PRX_COMPLETE", "XASH_FS_PRX_STATE",
    )
    positions = [next(i for i, message in enumerate(messages)
                      if message.startswith(marker + " ")) for marker in ordered]
    if positions != sorted(positions) or len(set(positions)) != len(positions):
        fail("client/filesystem PRX lifecycle order is invalid")
    return complete


def validate_ref_agc_prx_gate(
    messages: list[str], raw: list[str]
) -> dict[str, str]:
    """Validate the Phase 6 boundary or the Phase 7 live consumer contract."""
    load = one_where(messages, "XASH_PRX_LOAD", "module", "ref_agc.prx")
    ready = one(messages, "XASH_REF_AGC_PRX_READY")
    state = one(messages, "XASH_REF_AGC_PRX_STATE")
    unload = one_where(messages, "XASH_PRX_UNLOAD", "module", "ref_agc.prx")
    complete = one(messages, "XASH_REF_AGC_PRX_COMPLETE")

    exports = int(load.get("exports", "0"), 10)
    if load.get("path") != "/app0/sce_module/ref_agc.prx" \
            or load.get("result") != "0" or load.get("init_result") != "0" \
            or exports not in (16, 26, 31, 40) \
            or not 1 <= int(load.get("segments", "0"), 10) <= 4:
        fail("ref_agc PRX load contract failed")
    expected_common = {
        "module": "ref_agc.prx", "api": "18",
        "backend": "phase7-live" if exports in (31, 40) else "phase4-native",
        "ownership": "fence+videoout+ack" if exports in (31, 40) else "fence+videoout",
        "pass": "1",
    }
    if any(ready.get(key) != value for key, value in expected_common.items()) \
            or ready.get("state") != "0" or ready.get("engine_mask") != "0" \
            or ready.get("expected_mask") != "63" or ready.get("frames") != "0":
        fail("ref_agc PRX ready contract failed")
    frame_count = int(state.get("frames", "0"), 10)
    if any(state.get(key) != value for key, value in expected_common.items()) \
            or state.get("state") != "5" \
            or state.get("runtime_result") != "0" \
            or state.get("teardown_result") != "0" \
            or state.get("engine_mask") != "63" \
            or state.get("expected_mask") != "63" \
            or (exports in (31, 40) and frame_count <= 0) \
            or (exports not in (31, 40) and frame_count != 600) \
            or state.get("frame_hash") in (None, "0000000000000000") \
            or int(state.get("bright_pixels", "0"), 10) <= 0:
        fail("ref_agc PRX runtime state did not pass")
    for field in ("begin_calls", "scene_calls", "end_calls", "newmap_calls"):
        if int(state.get(field, "0"), 10) <= 0:
            fail(f"ref_agc PRX recorded no {field}")
    if state.get("begin_calls") != state.get("end_calls"):
        fail("ref_agc begin/end callback counts differ")
    if exports in (26, 31, 40):
        live_positive = (
            "live_frames", "live_view_frames", "live_map_serial",
            "world_surfaces", "entity_peak", "draw2d_peak",
        )
        for field in live_positive:
            if int(state.get(field, "0"), 10) <= 0:
                fail(f"ref_agc live capture recorded no {field}")
        if state.get("live_frames") != state.get("end_calls") \
                or int(state.get("live_view_frames", "0"), 10) > int(
                    state.get("live_frames", "0"), 10) \
                or state.get("live_view_hash") in (None, "0000000000000000") \
                or state.get("dropped_entities") != "0" \
                or state.get("dropped_2d") != "0":
            fail("ref_agc live frame capture contract failed")
    if exports in (31, 40):
        consumed_positive = (
            "consumed_frames", "consumed_serial", "consumed_view_frames",
        )
        for field in consumed_positive:
            if int(state.get(field, "0"), 10) <= 0:
                fail(f"ref_agc live consumer recorded no {field}")
        if state.get("consumed_frames") != state.get("frames") \
                or state.get("consumed_frames") != state.get("live_frames") \
                or state.get("consumed_serial") != state.get("consumed_frames") \
                or state.get("consumed_view_frames") != state.get("live_view_frames") \
                or state.get("consumed_camera_hash") in (
                    None, "0000000000000000"):
            fail("ref_agc live consumer/ACK contract failed")
    if exports == 40:
        for field in (
            "texture_revision", "texture_creates", "texture_handles",
            "texture_peak_active", "texture_peak_bytes", "world_texture_refs",
        ):
            if int(state.get(field, "0"), 10) <= 0:
                fail(f"ref_agc live resource bridge recorded no {field}")
        if state.get("world_textures_resolved") != state.get(
                "world_texture_refs"):
            fail("ref_agc live resource bridge left world textures unresolved")
    if unload.get("result") != "0" or unload.get("stop_result") != "0" \
            or unload.get("ownership") != "released":
        fail("ref_agc PRX unload did not release ownership")
    expected_complete = {
        "module": "ref_agc.prx", "stop_result": "0", "active_modules": "1",
        "ownership": "exact", "pass": "1",
    }
    if any(complete.get(key) != value for key, value in expected_complete.items()):
        fail("ref_agc PRX completion contract failed")

    state["_descriptor_exports"] = str(exports)

    # The final gate keeps all four earlier module checkpoints and must unwind
    # server -> menu -> client -> renderer -> filesystem.
    active = {
        "XASH_SERVER_PRX_COMPLETE": "4",
        "XASH_MENU_PRX_COMPLETE": "3",
        "XASH_CLIENT_PRX_COMPLETE": "2",
        "XASH_REF_AGC_PRX_COMPLETE": "1",
        "XASH_FS_PRX_COMPLETE": "0",
    }
    positions: list[int] = []
    for marker, expected in active.items():
        fields = one(messages, marker)
        if fields.get("active_modules") != expected or fields.get("ownership") != "exact":
            fail(f"{marker} does not preserve the final unload chain")
        positions.append(next(i for i, message in enumerate(messages)
                              if message.startswith(marker + " ")))
    if positions != sorted(positions):
        fail("final PRX unload order is invalid")
    console = "\n".join(raw)
    for proof in ("Spawn Server: c1a0", 'Dll loaded for game "Half-Life"',
                  "Game started"):
        if proof not in console:
            fail(f"ref_agc workload lacks console proof: {proof}")
    return state


def validate(
    manifest_path: Path,
    *,
    engine_commit: str,
    hlsdk_commit: str,
    boot_map: str,
    mode: str = "dedicated",
    pad_gate: bool = False,
    audio_gate: bool = False,
    memory_gate: bool = False,
    thread_time_gate: bool = False,
    libc_shim_gate: bool = False,
    prx_gate: bool = False,
    filesystem_prx_gate: bool = False,
    server_prx_gate: bool = False,
    menu_prx_gate: bool = False,
    client_prx_gate: bool = False,
    ref_agc_prx_gate: bool = False,
) -> dict[str, object]:
    manifest_path = manifest_path.resolve()
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        fail(f"invalid manifest: {exc}")

    identity = manifest.get("identity")
    if not isinstance(identity, dict):
        fail("manifest identity missing")
    if identity.get("title") != TITLE or identity.get("app") != APP:
        fail("title/app identity mismatch")
    if manifest.get("protocol") != "ps5log/1" or manifest.get("transport") != "tcp":
        fail("protocol/transport mismatch")
    if not all(manifest.get(key) for key in ("hello", "bye", "clean")):
        fail("run lacks clean HELLO/BYE completion")
    if manifest.get("gaps") != []:
        fail("run has sequence gaps")
    if manifest.get("oversized_lines") != 0:
        fail("run has oversized records")

    log_name = manifest.get("log_path")
    if not isinstance(log_name, str) or Path(log_name).name != log_name:
        fail("unsafe transcript path")
    log_path = (manifest_path.parent / log_name).resolve()
    if log_path.parent != manifest_path.parent:
        fail("transcript escaped manifest directory")
    try:
        data = log_path.read_bytes()
    except OSError as exc:
        fail(f"missing transcript: {exc}")
    if len(data) != manifest.get("bytes") or sha256(data) != manifest.get("sha256"):
        fail("transcript size/hash mismatch")

    lines = data.decode("utf-8", errors="replace").splitlines()
    if not lines or not lines[0].startswith("HELLO ps5log/1 "):
        fail("HELLO line missing")
    expected_boot = str(identity.get("boot", ""))
    hello_fields = parse_fields("HELLO " + lines[0].split(" ", 2)[2])
    if hello_fields.get("title") != TITLE or hello_fields.get("app") != APP:
        fail("HELLO identity mismatch")
    if hello_fields.get("boot") != expected_boot:
        fail("HELLO boot mismatch")

    records, raw = split_transcript(lines[1:-1])
    if [record[0] for record in records] != list(range(1, len(records) + 1)):
        fail("transcript sequence is not contiguous")
    if not records:
        fail("transcript has no structured records")
    if len(records) != manifest.get("records") or records[-1][0] != manifest.get("last_seq"):
        fail("manifest record count/last sequence mismatch")
    if len(raw) != manifest.get("raw_lines"):
        fail("manifest raw line count mismatch")
    if any(level in ("ERR", "ERROR") for _, level, _ in records):
        fail("transcript contains ERROR records")
    messages = [message for _, _, message in records]

    if f"LOG_BOOT_MONOTONIC_NS={expected_boot}" not in messages:
        fail("structured boot token mismatch")
    bye_fields = manifest.get("bye_fields")
    if not isinstance(bye_fields, dict):
        fail("manifest BYE fields missing")
    expected_bye = f"BYE seq={records[-1][0]} reason={BYE_REASON}"
    if lines[-1] != expected_bye or bye_fields.get("reason") != BYE_REASON:
        fail("BYE reason/sequence mismatch")

    boot = one(messages, "XASH_BOOT")
    if boot.get("schema") != "1" or boot.get("slice") != "engine-boot" or boot.get("mode") != mode:
        fail("engine boot schema/slice/mode mismatch")
    if boot.get("engine") != engine_commit or boot.get("hlsdk") != hlsdk_commit:
        fail("engine/hlsdk commit mismatch")
    if boot.get("map") != boot_map:
        fail("boot map mismatch")
    if boot.get("basedir") in (None, "none"):
        fail("no writable base directory was selected")
    if boot.get("rodir_present") != "1":
        fail("read-only game data was not present under the title")
    gate_seconds = boot.get("gate_seconds")
    if gate_seconds is None or int(gate_seconds, 10) <= 0:
        fail("boot gate is not bounded")
    if pad_gate and boot.get("pad_gate") != "1":
        fail("ScePad gate was not enabled in the artifact")
    if audio_gate and boot.get("audio_gate") != "1":
        fail("SceAudioOut gate was not enabled in the artifact")
    if memory_gate and boot.get("memory_gate") != "1":
        fail("direct-memory gate was not enabled in the artifact")
    if thread_time_gate and boot.get("thread_time_gate") != "1":
        fail("thread/time gate was not enabled in the artifact")
    if libc_shim_gate and boot.get("libc_shim_gate") != "1":
        fail("libc shim gate was not enabled in the artifact")
    if prx_gate and boot.get("prx_gate") != "1":
        fail("PRX loader gate was not enabled in the artifact")
    if filesystem_prx_gate and boot.get("filesystem_prx") != "1":
        fail("filesystem PRX gate was not enabled in the artifact")
    if server_prx_gate and (boot.get("server_prx") != "1" or
                            boot.get("filesystem_prx") != "1"):
        fail("server PRX gate was not enabled on the filesystem checkpoint")
    if menu_prx_gate and (mode != "client" or boot.get("menu_prx") != "1" or
                          boot.get("server_prx") != "1" or
                          boot.get("filesystem_prx") != "1"):
        fail("menu PRX gate was not enabled on the client filesystem/server checkpoint")
    if client_prx_gate and (mode != "client" or boot.get("client_prx") != "1" or
                            boot.get("menu_prx") != "1" or
                            boot.get("server_prx") != "1" or
                            boot.get("filesystem_prx") != "1"):
        fail("client PRX gate was not enabled on the filesystem/server/menu checkpoint")
    if ref_agc_prx_gate and (mode != "client" or boot.get("ref") != "agc" or
                            boot.get("ref_agc_prx") != "1" or
                            any(boot.get(name) != "1" for name in
                                ("filesystem_prx", "server_prx", "menu_prx",
                                 "client_prx"))):
        fail("ref_agc PRX gate was not enabled on the complete module checkpoint")

    exit_fields = one(messages, "XASH_EXIT")
    if exit_fields.get("result") != "0":
        fail("engine returned a non-zero result")
    if memory_gate and (exit_fields.get("memory_gate") != "1" or
                        exit_fields.get("memory_pass") != "1"):
        fail("direct-memory result was not successful")
    if thread_time_gate and (exit_fields.get("thread_time_gate") != "1" or
                             exit_fields.get("thread_time_pass") != "1"):
        fail("thread/time result was not successful")
    if libc_shim_gate and (exit_fields.get("libc_shim_gate") != "1" or
                           exit_fields.get("libc_shim_pass") != "1"):
        fail("libc shim result was not successful")
    if prx_gate and (exit_fields.get("prx_gate") != "1" or
                     exit_fields.get("prx_pass") != "1"):
        fail("PRX loader result was not successful")
    if filesystem_prx_gate and exit_fields.get("filesystem_prx") != "1":
        fail("filesystem PRX result was not retained at exit")
    if server_prx_gate and exit_fields.get("server_prx") != "1":
        fail("server PRX result was not retained at exit")
    if menu_prx_gate and exit_fields.get("menu_prx") != "1":
        fail("menu PRX result was not retained at exit")
    if client_prx_gate and exit_fields.get("client_prx") != "1":
        fail("client PRX result was not retained at exit")
    if ref_agc_prx_gate and exit_fields.get("ref_agc_prx") != "1":
        fail("ref_agc PRX result was not retained at exit")

    console = "\n".join(raw)
    for needle in FATAL_CONSOLE:
        if needle in console:
            fail(f"console reports a fatal error: {needle}")
    proofs = {
        "filesystem": "filesystem_stdio successfully loaded",
        "bounded_quit": "XASH_PAD_GATE_PASS action=quit" if pad_gate
        else f"PS5_XASH_GATE_TIMEOUT seconds={gate_seconds} action=quit",
    }
    if not menu_prx_gate or client_prx_gate:
        proofs["spawn"] = f"Spawn Server: {boot_map}"
    frames: list[dict[str, str]] = []
    if mode == "client":
        ref = boot.get("ref", "")
        if not ref or ref == "none":
            fail("client boot names no renderer")
        proofs["renderer"] = f"Loading renderer: {ref} -> ref_{ref}"
        if ref == "soft":
            software_buffer = one(messages, "XASH_SW_BUFFER")
            if software_buffer.get("width") != "640" or \
                    software_buffer.get("height") != "480" or \
                    int(software_buffer.get("bytes", "0"), 10) <= 0:
                fail("software renderer buffer contract failed")
        elif not ref_agc_prx_gate:
            proofs["renderer_ready"] = f"Renderer ref_{ref} initialized"
        frames = [parse_fields(m) for m in messages if m.startswith("XASH_FRAME ")]
        if not frames and not ref_agc_prx_gate:
            fail("client run presented no frame")
        if ref == "soft" and not any(f.get("nonzero") == "1" for f in frames):
            fail("software renderer frames stayed black")
    for name, needle in proofs.items():
        if needle not in console:
            fail(f"console proof missing: {name}")

    audio_complete: dict[str, str] | None = None
    if audio_gate:
        audio_complete = validate_audio_gate(messages)

    memory_summary: dict[str, str] | None = None
    if memory_gate:
        memory_summary = validate_memory_gate(messages)

    thread_time_complete: dict[str, str] | None = None
    if thread_time_gate:
        thread_time_complete = validate_thread_time_gate(messages)

    libc_shim_complete: dict[str, str] | None = None
    if libc_shim_gate:
        libc_shim_complete = validate_libc_shim_gate(messages)

    prx_complete: dict[str, str] | None = None
    if prx_gate:
        prx_complete = validate_prx_gate(messages)

    filesystem_prx_complete: dict[str, str] | None = None
    if filesystem_prx_gate:
        filesystem_prx_complete = validate_filesystem_prx_gate(messages, raw)

    server_prx_complete: dict[str, str] | None = None
    if server_prx_gate:
        server_prx_complete = validate_server_prx_gate(messages, raw)

    menu_prx_complete: dict[str, str] | None = None
    if menu_prx_gate:
        menu_prx_complete = validate_menu_prx_gate(messages, raw, client_prx_gate)

    client_prx_complete: dict[str, str] | None = None
    if client_prx_gate:
        client_prx_complete = validate_client_prx_gate(messages, raw)

    ref_agc_state: dict[str, str] | None = None
    if ref_agc_prx_gate:
        ref_agc_state = validate_ref_agc_prx_gate(messages, raw)

    pad_summary: dict[str, str] | None = None
    if pad_gate:
        pad_init = one(messages, "XASH_PAD_INIT")
        pad_summary = one(messages, "XASH_PAD_SUMMARY")
        teardown = one(messages, "XASH_PAD_TEARDOWN")
        complete = one(messages, "XASH_PAD_COMPLETE")
        if pad_init.get("schema") != "1" or pad_init.get("read") != "scePadRead" \
                or pad_init.get("batch") != "64":
            fail("ScePad initialization contract mismatch")
        if int(pad_init.get("handle", "-1"), 10) < 0:
            fail("ScePad handle was not opened")
        if pad_init.get("pad_init_rc") != "0":
            fail("scePadInit did not succeed")
        for field in ("polls", "samples", "connected", "movement", "look"):
            if int(pad_summary.get(field, "0"), 10) <= 0:
                fail(f"ScePad summary lacks {field} evidence")
        maximum = int(pad_summary.get("max_batch", "0"), 10)
        if not 1 <= maximum <= 64:
            fail("ScePad batch size is outside the 1-64 contract")
        if pad_summary.get("read_errors") != "0":
            fail("ScePad run contains read errors")
        for action in ("jump", "crouch", "use", "fire"):
            try:
                presses, releases = (int(value, 10) for value in
                                     pad_summary.get(action, "0/0").split("/", 1))
            except ValueError as exc:
                fail(f"malformed {action} edge counters")
            if presses <= 0 or releases <= 0:
                fail(f"ScePad run lacks {action} press/release evidence")
        required_states = {
            "movement": {"active", "neutral"},
            "look": {"active", "neutral"},
            "jump": {"pressed", "released"},
            "crouch": {"pressed", "released"},
            "use": {"pressed", "released"},
            "fire": {"pressed", "released"},
        }
        observed: dict[str, set[str]] = {name: set() for name in required_states}
        for message in messages:
            if not message.startswith("XASH_PAD_ACTION "):
                continue
            action = parse_fields(message)
            name, state = action.get("name"), action.get("state")
            if name in observed and state is not None:
                observed[name].add(state)
        for action, states in required_states.items():
            if not states.issubset(observed[action]):
                fail(f"ScePad action transcript lacks {action} states")
        if teardown.get("schema") != "1" or teardown.get("close_rc") != "0" \
                or teardown.get("result") != "0":
            fail("ScePad handle teardown was not clean")
        if teardown.get("owned_user_service") == "1" and teardown.get("terminate_rc") != "0":
            fail("owned UserService was not terminated cleanly")
        required_complete = ("movement", "look", "jump", "crouch", "use", "fire", "pass")
        if complete.get("schema") != "1" or any(complete.get(field) != "1" for field in required_complete):
            fail("ScePad completion marker is incomplete")
        if complete.get("ownership") != "exact" or complete.get("errors") != "0":
            fail("ScePad completion ownership/error contract failed")

    return {
        "run_id": manifest.get("run_id"),
        "records": len(records),
        "raw_lines": len(raw),
        "log_sha256": sha256(data),
        "engine": boot["engine"],
        "hlsdk": boot["hlsdk"],
        "map": boot["map"],
        "basedir": boot["basedir"],
        "gate_seconds": int(gate_seconds, 10),
        "mode": mode,
        "frames_presented": int(frames[-1]["presented"], 10) if frames else 0,
        "last_frame_hash": frames[-1].get("hash") if frames else None,
        "pad_gate": pad_gate,
        "pad_samples": int(pad_summary["samples"], 10) if pad_summary else 0,
        "pad_max_batch": int(pad_summary["max_batch"], 10) if pad_summary else 0,
        "audio_gate": audio_gate,
        "audio_blocks": int(audio_complete["blocks"], 10) if audio_complete else 0,
        "audio_frames_sent": int(audio_complete["sent"], 10) if audio_complete else 0,
        "audio_source_hash": audio_complete["source_hash"] if audio_complete else None,
        "audio_output_hash": audio_complete["output_hash"] if audio_complete else None,
        "memory_gate": memory_gate,
        "memory_peak_bytes": int(memory_summary["peak_bytes"], 10)
        if memory_summary else 0,
        "memory_alloc_calls": int(memory_summary["alloc_calls"], 10)
        if memory_summary else 0,
        "thread_time_gate": thread_time_gate,
        "thread_time_workers": int(thread_time_complete["workers"], 10)
        if thread_time_complete else 0,
        "thread_time_counter": int(thread_time_complete["counter"], 10)
        if thread_time_complete else 0,
        "libc_shim_gate": libc_shim_gate,
        "libc_shim_pass": libc_shim_complete is not None,
        "prx_gate": prx_gate,
        "prx_pass": prx_complete is not None,
        "filesystem_prx_gate": filesystem_prx_gate,
        "filesystem_prx_large_bytes": int(filesystem_prx_complete["large_bytes"], 10)
        if filesystem_prx_complete else 0,
        "server_prx_gate": server_prx_gate,
        "server_prx_active_after_unload": int(server_prx_complete["active_modules"], 10)
        if server_prx_complete else 0,
        "menu_prx_gate": menu_prx_gate,
        "menu_prx_redraw_calls": int(menu_prx_complete["redraw_calls"], 10)
        if menu_prx_complete else 0,
        "client_prx_gate": client_prx_gate,
        "client_prx_frame_calls": int(client_prx_complete["frame_calls"], 10)
        if client_prx_complete else 0,
        "ref_agc_prx_gate": ref_agc_prx_gate,
        "ref_agc_exports": int(
            ref_agc_state.get("_descriptor_exports", "0"), 10)
        if ref_agc_state else 0,
        "ref_agc_frames": int(ref_agc_state["frames"], 10)
        if ref_agc_state else 0,
        "ref_agc_frame_hash": ref_agc_state["frame_hash"]
        if ref_agc_state else None,
        "ref_agc_bright_pixels": int(ref_agc_state["bright_pixels"], 10)
        if ref_agc_state else 0,
        "ref_agc_live_frames": int(ref_agc_state.get("live_frames", "0"), 10)
        if ref_agc_state else 0,
        "ref_agc_live_view_frames": int(
            ref_agc_state.get("live_view_frames", "0"), 10)
        if ref_agc_state else 0,
        "ref_agc_consumed_frames": int(
            ref_agc_state.get("consumed_frames", "0"), 10)
        if ref_agc_state else 0,
        "ref_agc_consumed_serial": int(
            ref_agc_state.get("consumed_serial", "0"), 10)
        if ref_agc_state else 0,
        "ref_agc_consumed_view_frames": int(
            ref_agc_state.get("consumed_view_frames", "0"), 10)
        if ref_agc_state else 0,
        "ref_agc_consumed_camera_hash": ref_agc_state.get(
            "consumed_camera_hash") if ref_agc_state else None,
        "ref_agc_texture_revision": int(
            ref_agc_state.get("texture_revision", "0"), 10)
        if ref_agc_state else 0,
        "ref_agc_texture_creates": int(
            ref_agc_state.get("texture_creates", "0"), 10)
        if ref_agc_state else 0,
        "ref_agc_texture_handles": int(
            ref_agc_state.get("texture_handles", "0"), 10)
        if ref_agc_state else 0,
        "ref_agc_texture_peak_active": int(
            ref_agc_state.get("texture_peak_active", "0"), 10)
        if ref_agc_state else 0,
        "ref_agc_texture_peak_bytes": int(
            ref_agc_state.get("texture_peak_bytes", "0"), 10)
        if ref_agc_state else 0,
        "ref_agc_world_texture_refs": int(
            ref_agc_state.get("world_texture_refs", "0"), 10)
        if ref_agc_state else 0,
        "ref_agc_world_textures_resolved": int(
            ref_agc_state.get("world_textures_resolved", "0"), 10)
        if ref_agc_state else 0,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--engine-commit", required=True)
    parser.add_argument("--hlsdk-commit", required=True)
    parser.add_argument("--map", default="c1a0")
    parser.add_argument("--mode", choices=("dedicated", "client"), default="dedicated")
    parser.add_argument("--pad-gate", action="store_true")
    parser.add_argument("--audio-gate", action="store_true")
    parser.add_argument("--memory-gate", action="store_true")
    parser.add_argument("--thread-time-gate", action="store_true")
    parser.add_argument("--libc-shim-gate", action="store_true")
    parser.add_argument("--prx-gate", action="store_true")
    parser.add_argument("--filesystem-prx-gate", action="store_true")
    parser.add_argument("--server-prx-gate", action="store_true")
    parser.add_argument("--menu-prx-gate", action="store_true")
    parser.add_argument("--client-prx-gate", action="store_true")
    parser.add_argument("--ref-agc-prx-gate", action="store_true")
    args = parser.parse_args()
    for value in (args.engine_commit, args.hlsdk_commit):
        if not HEX7.fullmatch(value):
            parser.error("commits must be 7 lowercase hex digits")
    try:
        summary = validate(
            args.manifest,
            engine_commit=args.engine_commit,
            hlsdk_commit=args.hlsdk_commit,
            boot_map=args.map,
            mode=args.mode,
            pad_gate=args.pad_gate,
            audio_gate=args.audio_gate,
            memory_gate=args.memory_gate,
            thread_time_gate=args.thread_time_gate,
            libc_shim_gate=args.libc_shim_gate,
            prx_gate=args.prx_gate,
            filesystem_prx_gate=args.filesystem_prx_gate,
            server_prx_gate=args.server_prx_gate,
            menu_prx_gate=args.menu_prx_gate,
            client_prx_gate=args.client_prx_gate,
            ref_agc_prx_gate=args.ref_agc_prx_gate,
        )
    except EvidenceError as exc:
        raise SystemExit(f"engine boot evidence validation failed: {exc}") from exc
    print(json.dumps(summary, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
