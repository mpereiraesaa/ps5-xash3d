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
    if any(level == "ERROR" for _, level, _ in records):
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

    console = "\n".join(raw)
    for needle in FATAL_CONSOLE:
        if needle in console:
            fail(f"console reports a fatal error: {needle}")
    proofs = {
        "filesystem": "filesystem_stdio successfully loaded",
        "spawn": f"Spawn Server: {boot_map}",
        "bounded_quit": "XASH_PAD_GATE_PASS action=quit" if pad_gate
        else f"PS5_XASH_GATE_TIMEOUT seconds={gate_seconds} action=quit",
    }
    frames: list[dict[str, str]] = []
    if mode == "client":
        ref = boot.get("ref", "")
        if not ref or ref == "none":
            fail("client boot names no renderer")
        proofs["renderer"] = f"Loading renderer: {ref} -> ref_{ref}"
        proofs["renderer_ready"] = f"Renderer ref_{ref} initialized"
        frames = [parse_fields(m) for m in messages if m.startswith("XASH_FRAME ")]
        if not frames:
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
        )
    except EvidenceError as exc:
        raise SystemExit(f"engine boot evidence validation failed: {exc}") from exc
    print(json.dumps(summary, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
