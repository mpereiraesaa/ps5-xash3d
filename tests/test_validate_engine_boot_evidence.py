#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import json
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
VALIDATOR = ROOT / "tools" / "validate_engine_boot_evidence.py"
ENGINE = "9aa39ad"
HLSDK = "e277ffa"
# The recorded pattern: 66150 source frames -> 71999 resampled + 193 padding
# = 282 whole 256-frame blocks. tests/test_ps5_audio_pattern.c pins the hash.
PATTERN_FRAMES = 66150
PATTERN_SILENT = 13230
PATTERN_HASH = "0x9fd6b8c32bb54595"


def make_evidence(directory: Path, *, spawn: bool = True, exit_result: int = 0,
                  mode: str = "dedicated", nonzero: int = 1,
                  pad_gate: bool = False, audio_gate: bool = False,
                  memory_gate: bool = False, memory_failures: int = 0,
                  thread_time_gate: bool = False,
                  libc_shim_gate: bool = False,
                  prx_gate: bool = False,
                  filesystem_prx_gate: bool = False,
                  server_prx_gate: bool = False,
                  menu_prx_gate: bool = False,
                  audio_underruns: int = 0, audio_sent: int = 72192,
                  audio_padding: int = 193, audio_source_hash: str = PATTERN_HASH,
                  audio_progress: int = 5, audio_drain_rc: int = 256) -> Path:
    ref = "soft" if mode == "client" else "none"
    structured = [
        ("INFO", "LOG_SCHEMA=3"),
        ("INFO", "LOG_BOOT_MONOTONIC_NS=0x1234"),
        ("MARK", f"XASH_BOOT schema=1 slice=engine-boot mode={mode} ref={ref} fw=12.02 "
                 f"engine={ENGINE} hlsdk={HLSDK} rodir=/app0/xash3d basedir=/download0/xash3d "
                 f"gamedir=valve map=c1a0 gate_seconds=90 pad_gate={int(pad_gate)} "
                 f"audio_gate={int(audio_gate)} memory_gate={int(memory_gate)} "
                 f"thread_time_gate={int(thread_time_gate)} "
                 f"libc_shim_gate={int(libc_shim_gate)} prx_gate={int(prx_gate)} "
                 f"filesystem_prx={int(filesystem_prx_gate or menu_prx_gate)} "
                 f"server_prx={int(server_prx_gate or menu_prx_gate)} "
                 f"menu_prx={int(menu_prx_gate)} "
                 "rodir_present=1"),
    ]
    if mode == "client":
        structured += [
            ("INFO", "XASH_SW_BUFFER width=640 height=480 bpp=4 bytes=1228800"),
            ("MARK", "XASH_FRAME source=software presented=300 width=640 height=480 "
                     f"hash=0123456789abcdef nonzero={nonzero}"),
        ]
    if pad_gate:
        structured += [
            ("MARK", "XASH_PAD_INIT schema=1 user_service_rc=0 owns_user_service=1 "
                     "user_id=1 pad_init_rc=0 handle=7 read=scePadRead batch=64"),
            ("MARK", "XASH_PAD_ACTION schema=1 name=movement state=active timestamp_us=1"),
            ("MARK", "XASH_PAD_ACTION schema=1 name=movement state=neutral timestamp_us=2"),
            ("MARK", "XASH_PAD_ACTION schema=1 name=look state=active timestamp_us=3"),
            ("MARK", "XASH_PAD_ACTION schema=1 name=look state=neutral timestamp_us=4"),
        ]
        for action in ("jump", "crouch", "use", "fire"):
            structured.append(("MARK", f"XASH_PAD_ACTION schema=1 name={action} state=pressed timestamp_us=5"))
            structured.append(("MARK", f"XASH_PAD_ACTION schema=1 name={action} state=released timestamp_us=6"))
        structured += [
            ("MARK", "XASH_PAD_SUMMARY schema=1 polls=10 samples=20 empty_reads=2 max_batch=4 "
                     "connected=20 disconnected=0 intercepted=0 generation_changes=1 axis_events=12 "
                     "button_events=8 movement=4 look=3 jump=1/1 crouch=1/1 use=1/1 fire=1/1 read_errors=0"),
            ("MARK", "XASH_PAD_TEARDOWN schema=1 handle=7 close_rc=0 owned_user_service=1 "
                     "terminate_rc=0 result=0"),
            ("MARK", "XASH_PAD_COMPLETE schema=1 movement=1 look=1 jump=1 crouch=1 use=1 "
                     "fire=1 chronological_batches=1 ownership=exact errors=0 pass=1"),
        ]
    if audio_gate:
        blocks = audio_sent // 256
        structured += [
            ("MARK", "XASH_AUDIO_USER schema=1 source=system user=0xff"),
            ("MARK", "XASH_AUDIO_INIT schema=1 user=0xff type=0 index=0 handle=5 "
                     "init_rc=0 open_rc=5 volume_rc=0 volume_flags=3 volume_value=0x8000 "
                     "input_rate=44100 output_rate=48000 format=1 channels=2 grain=256"),
            ("MARK", "XASH_AUDIO_RING_READY schema=1 capacity_frames=8192 prime_frames=1024 "
                     "stage_frames=1024 accum_frames=1536 ratio=147/160"),
            ("MARK", f"XASH_AUDIO_PATTERN schema=1 segments=3 frames={PATTERN_FRAMES} "
                     f"silent_frames={PATTERN_SILENT} rate=44100 channels=2 width=2 "
                     f"source_hash={audio_source_hash}"),
        ]
        for index in range(audio_progress):
            structured.append(("INFO", f"XASH_AUDIO_PROGRESS schema=1 produced=1 consumed=1 "
                                       f"sent={index} blocks={index} high_water=2048 wraps=1 "
                                       "underruns=0 silent=0"))
        if audio_underruns:
            structured.append(("WARN", "XASH_AUDIO_UNDERRUN schema=1 episode=1 produced=1 "
                                       "consumed=1 sent=1 blocks=1"))
        structured += [
            ("MARK", f"XASH_AUDIO_SUMMARY schema=1 produced={PATTERN_FRAMES} "
                     f"consumed={PATTERN_FRAMES} sent={audio_sent} blocks={blocks} "
                     f"silent={PATTERN_SILENT} underruns={audio_underruns} "
                     f"padding={audio_padding} discarded=0 wraps=8 rebases=0 high_water=2048 "
                     "capacity=8192 prime=1024 grain=256 input_rate=44100 output_rate=48000 "
                     f"format=1 channels=2 source_hash={audio_source_hash} "
                     "output_hash=0x1122334455667788 output_errors=0"),
            # drain_rc is the frame count FW 12.02 actually returns, not zero.
            ("MARK", f"XASH_AUDIO_TEARDOWN schema=1 handle=5 drain_rc={audio_drain_rc} "
                     "drain_calls=1 close_rc=0 close_calls=1 join_calls=1 owner=worker "
                     "state=3 result=0"),
            ("MARK", f"XASH_AUDIO_COMPLETE schema=1 shutdown_rc=0 produced={PATTERN_FRAMES} "
                     f"consumed={PATTERN_FRAMES} sent={audio_sent} blocks={blocks} "
                     f"expected_resampled=71999 padding={audio_padding} discarded=0 "
                     f"underruns={audio_underruns} output_errors=0 silent={PATTERN_SILENT} "
                     "wraps=8 high_water=2048 "
                     f"source_hash={audio_source_hash} expected_source_hash={PATTERN_HASH} "
                     "output_hash=0x1122334455667788 input_rate=44100 output_rate=48000 "
                     "ratio=147/160 ownership=exact pass=1"),
        ]
    if memory_gate:
        structured += [
            ("MARK", "XASH_MEMORY_BEGIN schema=1 arena=direct root_mib=128 "
                     "cpu=malloc+calloc+realloc+free gpu=command+buffer+texture+depth"),
            ("MARK", "XASH_MEMORY_RESOURCE kind=command bytes=2097152 alignment=256 "
                     "generation=4 hash=1111111111111111 owner=gpu-active"),
            ("MARK", "XASH_MEMORY_RESOURCE kind=buffer bytes=4194304 alignment=65536 "
                     "generation=5 hash=2222222222222222 owner=gpu-active"),
            ("MARK", "XASH_MEMORY_RESOURCE kind=texture bytes=8388608 alignment=65536 "
                     "generation=6 hash=3333333333333333 owner=gpu-active"),
            ("MARK", "XASH_MEMORY_RESOURCE kind=depth bytes=4194304 alignment=65536 "
                     "generation=7 hash=4444444444444444 owner=gpu-active"),
            ("MARK", "XASH_MEMORY_COMPLETE schema=1 result=0 resources=4 "
                     "resource_bytes=18874368 hash=5555555555555555 "
                     "retire_token=5048354d454d0001 completion=synthetic-contract "
                     "live_bytes=0 live_cpu=0 live_gpu=0 retiring_gpu=0 "
                     "peak_bytes=19927040 guards=intact alloc_failures=0 "
                     "root_calls=1/1/1 pass=1"),
            ("MARK", f"XASH_MEMORY_SUMMARY schema=1 arena_bytes=134217728 "
                     "live_bytes=22565 peak_bytes=19927040 alloc_calls=100 free_calls=88 "
                     f"realloc_calls=2 live_cpu=8 live_gpu=0 retiring_gpu=0 failures={memory_failures} "
                     "guard_failures=0 stale_errors=0 retire_calls=4 reclaim_calls=4 "
                     "process_lifetime_cpu=8 process_lifetime_bytes=22565 "
                     "foreign_calls=0 foreign_bytes=0 pass=1"),
            ("MARK", "XASH_MEMORY_TEARDOWN schema=1 result=0 reserve_calls=1 "
                     "allocate_calls=1 map_calls=1 unmap_calls=1 release_calls=1 "
                     "reserve_rc=0 allocate_rc=0 map_rc=0 unmap_rc=0 release_rc=0 "
                     "mapped=0 allocated=0 live_bytes=0 live_cpu=0 live_gpu=0 "
                     "retiring_gpu=0 lifetime_reclaims=8 lifetime_bytes=22565 "
                     "ownership=exact pass=1"),
        ]
    if thread_time_gate:
        structured += [
            ("MARK", "XASH_THREAD_TIME_BEGIN schema=1 workers=2 iterations=16384 "
                     "clock_samples=8192 sleep_samples=16 sleep_buckets=8"),
            ("MARK", "XASH_THREAD_RESULT schema=1 create_calls=2 create_join_rc=0 "
                     "create_detach_rc=0 join_calls=1 join_rc=0 detach_calls=1 "
                     "detach_rc=0 completions=2 detached_complete=1 distinct=2 "
                     "mutex_init_rc=0 mutex_destroy_rc=0 mutex_errors=0 "
                     "counter=32768 expected=32768 ownership=exact pass=1"),
            ("MARK", "XASH_CLOCK_RESULT schema=1 clock=monotonic reads=8192 "
                     "advances=8191 min_step_ns=20 span_ns=163820 errors=0 "
                     "regressions=0 pass=1"),
        ]
        for api in ("nanosleep", "usleep"):
            for requested in (1000, 2000, 5000, 10000):
                minimum = requested * 1000
                structured.append(("MARK", f"XASH_SLEEP_RESULT schema=1 api={api} "
                                           f"requested_us={requested} samples=16 "
                                           f"min_ns={minimum} average_ns={minimum + 50000} "
                                           f"p95_ns={minimum + 90000} max_ns={minimum + 100000} "
                                           "errors=0 early=0 pass=1"))
        structured.append(("MARK", "XASH_THREAD_TIME_COMPLETE schema=1 create=2 "
                                   "join=1 detach=1 workers=2 counter=32768 "
                                   "clock_regressions=0 sleep_errors=0 sleep_early=0 "
                                   "ownership=exact pass=1"))
    if libc_shim_gate:
        structured += [
            ("MARK", "XASH_LIBC_SHIM_BEGIN schema=1 symbols=__assert,getpwuid,dladdr"),
            ("MARK", "XASH_LIBC_SHIM_RESULT symbol=__assert implementation=project-owned "
                     "reporter=ps5log abort=noreturn format_pass=1"),
            ("MARK", "XASH_LIBC_SHIM_RESULT symbol=getpwuid implementation=project-owned "
                     "requested_uid=0xff returned_uid=0xff username=ps5 pass=1"),
            ("MARK", "XASH_LIBC_SHIM_RESULT symbol=dladdr implementation=project-owned "
                     "result=0 fallback=argv0 info=zeroed pass=1"),
            ("MARK", "XASH_LIBC_SHIM_END pass=1"),
        ]
    if prx_gate:
        structured += [
            ("MARK", "XASH_PRX_BEGIN schema=1 backend=COM_LoadLibrary "
                     "resolver=PRXDESC1 module=xash_prx_probe.prx"),
            ("MARK", "XASH_PRX_LOAD path=/app0/sce_module/xash_prx_probe.prx "
                     "module=xash_prx_probe.prx handle=0xd0 segments=4 exports=6 result=0"),
            ("MARK", "XASH_PRX_RESOLVE add=1 sleep_count=1 module_start=1 "
                     "version=1 started=1 missing=0 pass=1"),
            ("MARK", "XASH_PRX_CALL add=42 count=2 version=0x10000 auto_started=0 "
                     "manual_start_rc=0 started=1 kernel_import=sceKernelUsleep "
                     "name_roundtrip=1 pass=1"),
            ("MARK", "XASH_PRX_UNLOAD module=xash_prx_probe.prx result=0 "
                     "reason=ok ownership=released"),
            ("MARK", "XASH_PRX_COMPLETE pass=1 load=1 resolve=1 call=1 unload=1 "
                     "active=0 ownership=exact"),
        ]
    if filesystem_prx_gate or menu_prx_gate:
        structured += [
            ("MARK", "XASH_PRX_LOAD path=/app0/sce_module/filesystem_stdio.prx "
                     "module=filesystem_stdio.prx handle=0xd1 segments=4 exports=8 result=0"),
            ("MARK", "XASH_FS_PRX_READY module=filesystem_stdio.prx index_entries=4823 "
                     "allocator_contract=libc-shared allocator_result=0 "
                     "listing_refused=0 resolver=PRXDESC1"),
        ]
    if server_prx_gate or menu_prx_gate:
        structured += [
            ("MARK", "XASH_PRX_LOAD path=/app0/sce_module/server.prx "
                     "module=server.prx handle=0xd2 segments=4 exports=257 "
                     "init_result=0 result=0"),
            ("MARK", "XASH_SERVER_PRX_READY module=server.prx state=1 exports=251 "
                     "resolver=PRXDESC1"),
            ("MARK", "XASH_SERVER_PRX_ABI engine_table_mask=7 expected=7 pass=1"),
            ("MARK", "XASH_SERVER_PRX_ABI_SMOKE step=1 phase=begin"),
            ("MARK", "XASH_SERVER_PRX_ABI_SMOKE step=1 phase=complete result=1 pass=1"),
            ("MARK", "XASH_SERVER_PRX_ABI_SMOKE step=2 phase=begin"),
            ("MARK", "XASH_SERVER_PRX_ABI_SMOKE step=2 phase=complete result=1 pass=1"),
        ]
    if server_prx_gate:
        structured += [
            ("MARK", "XASH_SERVER_PRX_STATE module=server.prx state=1 exports=251 "
                     "resolver=PRXDESC1"),
            ("MARK", "XASH_PRX_UNLOAD module=server.prx result=0 reason=ok "
                     "stop_result=0 ownership=released"),
            ("MARK", "XASH_SERVER_PRX_COMPLETE module=server.prx stop_result=0 "
                     "active_modules=1 ownership=exact"),
        ]
    if menu_prx_gate:
        structured += [
            ("MARK", "XASH_PRX_LOAD path=/app0/sce_module/menu.prx module=menu.prx "
                     "handle=0xd3 segments=4 exports=6 init_result=0 result=0"),
            ("MARK", "XASH_MENU_PRX_READY module=menu.prx state=1 exports=2 "
                     "resolver=PRXDESC1"),
            ("MARK", "XASH_MENU_PRX_API result=1 callbacks=16 expected=16 "
                     "engine_mask=63 expected_mask=63 globals=1 pass=1"),
            ("MARK", "XASH_MENU_PRX_EXT_API version=1 result=1 callbacks=12 expected=12 "
                     "engine_mask=15 expected_mask=15 pass=1"),
            ("MARK", "XASH_MENU_PRX_INIT phase=begin call=1"),
            ("MARK", "XASH_MENU_PRX_INIT phase=complete call=1 active_modules=3"),
            ("MARK", "XASH_MENU_PRX_ACTIVE active=1 call=1 visible=0"),
            ("WARN", "XASH_PRX_OPTIONAL_MISS path=/app0/sce_module/libvgui_support.prx "
                     "module=libvgui_support.prx result=-2 reason=load/start-syscall-failed "
                     "loaded_handle=0x80020002 start_result=0 module_info_rc=0x0 "
                     "info_size=0x0 segment_count=0 load_error=0 rollback_rc=0x0 "
                     "rollback_stop_result=0 rollback=complete fallback=client-probe"),
            ("MARK", "XASH_MENU_PRX_REDRAW call=1 visible=1 realtime_ms=100 "
                     "active_modules=3"),
            ("MARK", "XASH_SERVER_PRX_STATE module=server.prx state=1 exports=251 "
                     "resolver=PRXDESC1"),
            ("MARK", "XASH_PRX_UNLOAD module=server.prx result=0 reason=ok "
                     "stop_result=0 ownership=released"),
            ("MARK", "XASH_SERVER_PRX_COMPLETE module=server.prx stop_result=0 "
                     "active_modules=2 ownership=exact"),
            ("MARK", "XASH_MENU_PRX_ACTIVE active=0 call=2 visible=0"),
            ("MARK", "XASH_MENU_PRX_SHUTDOWN phase=begin call=1 redraw_calls=300"),
            ("MARK", "XASH_MENU_PRX_SHUTDOWN phase=complete call=1 redraw_calls=300"),
            ("MARK", "XASH_MENU_PRX_STATE module=menu.prx state=1 exports=2 "
                     "resolver=PRXDESC1"),
            ("MARK", "XASH_PRX_UNLOAD module=menu.prx result=0 reason=ok "
                     "stop_result=0 ownership=released"),
            ("MARK", "XASH_MENU_PRX_COMPLETE module=menu.prx stop_result=0 "
                     "api_pass=1 ext_api_pass=1 init_calls=1 shutdown_calls=1 "
                     "redraw_calls=300 active_calls=2 active_modules=1 "
                     "ownership=exact pass=1"),
        ]
    if filesystem_prx_gate or menu_prx_gate:
        structured += [
            ("MARK", "XASH_FS_PRX_STATE module=filesystem_stdio.prx index_entries=4823 "
                     "allocator_contract=libc-shared allocator_result=0 "
                     "listing_refused=0 resolver=PRXDESC1"),
            ("MARK", "XASH_PRX_UNLOAD module=filesystem_stdio.prx result=0 reason=ok "
                     "stop_result=0 ownership=released"),
            ("MARK", "XASH_FS_PRX_COMPLETE module=filesystem_stdio.prx stop_result=0 "
                     "active_modules=0 ownership=exact"),
        ]
    structured.append(("MARK", f"XASH_EXIT result={exit_result} "
                               f"memory_gate={int(memory_gate)} memory_pass=1 "
                               f"thread_time_gate={int(thread_time_gate)} thread_time_pass=1 "
                               f"libc_shim_gate={int(libc_shim_gate)} libc_shim_pass=1 "
                               f"prx_gate={int(prx_gate)} prx_pass=1 "
                               f"filesystem_prx={int(filesystem_prx_gate or menu_prx_gate)} "
                               f"server_prx={int(server_prx_gate or menu_prx_gate)} "
                               f"menu_prx={int(menu_prx_gate)}"))
    console = [
        "Xash3D FWGS 49/0.21 (freebsd-amd64 build 4900)",
        "FS_LoadProgs: filesystem_stdio successfully loaded",
        "Spawn Server: c1a0" if spawn else "map c1a0: map not found",
        "XASH_PAD_GATE_PASS action=quit" if pad_gate else
        "PS5_XASH_GATE_TIMEOUT seconds=90 action=quit",
    ]
    if mode == "client":
        console += ["Loading renderer: soft -> ref_soft", "Renderer ref_soft initialized"]
    if filesystem_prx_gate or menu_prx_gate:
        console.append("XASH_FS_PRX_PROBE schema=1 index_entries=4823 "
                       "listing_pattern=gfx/* listing_matches=41 "
                       "case_path=GfX/PaLeTtE.LmP palette_bytes=768 "
                       "palette_hash=1111222233334444 large_path=maps/c1a0.bsp "
                       "large_bytes=2546336 large_hash=5555666677778888 pass=1")
    if server_prx_gate:
        console += ['Dll loaded for game "Half-Life"', "4 player server started"]
    if menu_prx_gate:
        console.append("UI_LoadProgs: extended Menu API initialized")
    lines = ["HELLO ps5log/1 title=PPSA99996 app=xash3d-engine boot=0x1234 tag=test"]
    seq = 0
    for index, (level, message) in enumerate(structured):
        seq += 1
        lines.append(f"{seq}\t{seq * 10}\t{level}\t{message}")
        if index == 2:
            lines.extend(console)
    lines.append(f"BYE seq={seq} reason=xash-engine-boot-complete")
    transcript = ("\n".join(lines) + "\n").encode()
    log_name = "synthetic.log"
    (directory / log_name).write_bytes(transcript)
    manifest = {
        "identity": {"title": "PPSA99996", "app": "xash3d-engine", "boot": "0x1234"},
        "protocol": "ps5log/1",
        "transport": "tcp",
        "hello": True,
        "bye": True,
        "clean": True,
        "gaps": [],
        "raw_lines": len(console),
        "oversized_lines": 0,
        "records": seq,
        "last_seq": seq,
        "bye_fields": {"seq": str(seq), "reason": "xash-engine-boot-complete"},
        "log_path": log_name,
        "bytes": len(transcript),
        "sha256": hashlib.sha256(transcript).hexdigest(),
        "run_id": "synthetic",
    }
    manifest_path = directory / "synthetic.json"
    manifest_path.write_text(json.dumps(manifest))
    return manifest_path


def run_validator(manifest: Path, engine: str = ENGINE, mode: str = "dedicated",
                  pad_gate: bool = False,
                  audio_gate: bool = False,
                  memory_gate: bool = False,
                  thread_time_gate: bool = False,
                  libc_shim_gate: bool = False,
                  prx_gate: bool = False,
                  filesystem_prx_gate: bool = False,
                  server_prx_gate: bool = False,
                  menu_prx_gate: bool = False) -> subprocess.CompletedProcess[str]:
    command = ["python3", "-B", str(VALIDATOR), str(manifest),
               "--engine-commit", engine, "--hlsdk-commit", HLSDK,
               "--map", "c1a0", "--mode", mode]
    if pad_gate:
        command.append("--pad-gate")
    if audio_gate:
        command.append("--audio-gate")
    if memory_gate:
        command.append("--memory-gate")
    if thread_time_gate:
        command.append("--thread-time-gate")
    if libc_shim_gate:
        command.append("--libc-shim-gate")
    if prx_gate:
        command.append("--prx-gate")
    if filesystem_prx_gate:
        command.append("--filesystem-prx-gate")
    if server_prx_gate:
        command.append("--server-prx-gate")
    if menu_prx_gate:
        command.append("--menu-prx-gate")
    return subprocess.run(
        command,
        text=True, capture_output=True, check=False)


def main() -> int:
    with tempfile.TemporaryDirectory() as temporary:
        directory = Path(temporary)
        manifest = make_evidence(directory)
        valid = run_validator(manifest)
        assert valid.returncode == 0, valid.stderr
        summary = json.loads(valid.stdout)
        assert summary["raw_lines"] == 4 and summary["records"] == 4
        assert summary["basedir"] == "/download0/xash3d"

        wrong_engine = run_validator(manifest, engine="0000000")
        assert wrong_engine.returncode != 0 and "commit mismatch" in wrong_engine.stderr

        no_spawn = run_validator(make_evidence(directory, spawn=False))
        assert no_spawn.returncode != 0 and "console proof missing: spawn" in no_spawn.stderr

        bad_exit = run_validator(make_evidence(directory, exit_result=1))
        assert bad_exit.returncode != 0 and "non-zero result" in bad_exit.stderr

        client = run_validator(make_evidence(directory, mode="client"), mode="client")
        assert client.returncode == 0, client.stderr
        assert json.loads(client.stdout)["frames_presented"] == 300
        black = run_validator(make_evidence(directory, mode="client", nonzero=0), mode="client")
        assert black.returncode != 0 and "stayed black" in black.stderr
        wrong_mode = run_validator(make_evidence(directory, mode="client"))
        assert wrong_mode.returncode != 0 and "mode mismatch" in wrong_mode.stderr

        pad = run_validator(make_evidence(directory, pad_gate=True), pad_gate=True)
        assert pad.returncode == 0, pad.stderr
        pad_summary = json.loads(pad.stdout)
        assert pad_summary["pad_gate"] and pad_summary["pad_samples"] == 20
        missing_pad = run_validator(make_evidence(directory), pad_gate=True)
        assert missing_pad.returncode != 0 and "not enabled" in missing_pad.stderr

        audio = run_validator(make_evidence(directory, audio_gate=True), audio_gate=True)
        assert audio.returncode == 0, audio.stderr
        audio_summary = json.loads(audio.stdout)
        assert audio_summary["audio_gate"]
        assert audio_summary["audio_blocks"] == 282
        assert audio_summary["audio_frames_sent"] == 72192
        assert audio_summary["audio_source_hash"] == PATTERN_HASH

        missing_audio = run_validator(make_evidence(directory), audio_gate=True)
        assert missing_audio.returncode != 0 and "not enabled" in missing_audio.stderr

        memory = run_validator(make_evidence(directory, memory_gate=True),
                               memory_gate=True)
        assert memory.returncode == 0, memory.stderr
        memory_summary = json.loads(memory.stdout)
        assert memory_summary["memory_gate"]
        assert memory_summary["memory_peak_bytes"] == 19927040
        assert memory_summary["memory_alloc_calls"] == 100
        missing_memory = run_validator(make_evidence(directory), memory_gate=True)
        assert missing_memory.returncode != 0 and "not enabled" in missing_memory.stderr
        memory_failure = run_validator(
            make_evidence(directory, memory_gate=True, memory_failures=1),
            memory_gate=True)
        assert memory_failure.returncode != 0 and "failures" in memory_failure.stderr

        thread_time = run_validator(
            make_evidence(directory, thread_time_gate=True),
            thread_time_gate=True)
        assert thread_time.returncode == 0, thread_time.stderr
        thread_summary = json.loads(thread_time.stdout)
        assert thread_summary["thread_time_gate"]
        assert thread_summary["thread_time_workers"] == 2
        assert thread_summary["thread_time_counter"] == 32768
        missing_thread_time = run_validator(
            make_evidence(directory), thread_time_gate=True)
        assert missing_thread_time.returncode != 0 \
            and "not enabled" in missing_thread_time.stderr

        libc_shim = run_validator(
            make_evidence(directory, libc_shim_gate=True),
            libc_shim_gate=True)
        assert libc_shim.returncode == 0, libc_shim.stderr
        libc_shim_summary = json.loads(libc_shim.stdout)
        assert libc_shim_summary["libc_shim_gate"]
        assert libc_shim_summary["libc_shim_pass"]
        missing_libc_shim = run_validator(
            make_evidence(directory), libc_shim_gate=True)
        assert missing_libc_shim.returncode != 0 \
            and "not enabled" in missing_libc_shim.stderr

        prx = run_validator(make_evidence(directory, prx_gate=True), prx_gate=True)
        assert prx.returncode == 0, prx.stderr
        prx_summary = json.loads(prx.stdout)
        assert prx_summary["prx_gate"] and prx_summary["prx_pass"]
        missing_prx = run_validator(make_evidence(directory), prx_gate=True)
        assert missing_prx.returncode != 0 and "not enabled" in missing_prx.stderr

        filesystem_prx = run_validator(
            make_evidence(directory, filesystem_prx_gate=True),
            filesystem_prx_gate=True)
        assert filesystem_prx.returncode == 0, filesystem_prx.stderr
        filesystem_summary = json.loads(filesystem_prx.stdout)
        assert filesystem_summary["filesystem_prx_gate"]
        assert filesystem_summary["filesystem_prx_large_bytes"] == 2546336
        missing_filesystem_prx = run_validator(
            make_evidence(directory), filesystem_prx_gate=True)
        assert missing_filesystem_prx.returncode != 0 \
            and "not enabled" in missing_filesystem_prx.stderr

        bad_filesystem_prx = make_evidence(directory, filesystem_prx_gate=True)
        bad_filesystem_log = directory / json.loads(
            bad_filesystem_prx.read_text())["log_path"]
        bad_filesystem_text = bad_filesystem_log.read_text().replace(
            "allocator_contract=libc-shared", "allocator_contract=private-arena", 1)
        bad_filesystem_log.write_text(bad_filesystem_text)
        bad_filesystem_data = bad_filesystem_log.read_bytes()
        bad_filesystem_manifest = json.loads(bad_filesystem_prx.read_text())
        bad_filesystem_manifest["bytes"] = len(bad_filesystem_data)
        bad_filesystem_manifest["sha256"] = hashlib.sha256(
            bad_filesystem_data).hexdigest()
        bad_filesystem_prx.write_text(json.dumps(bad_filesystem_manifest))
        rejected_filesystem_prx = run_validator(
            bad_filesystem_prx, filesystem_prx_gate=True)
        assert rejected_filesystem_prx.returncode != 0 \
            and "ready contract" in rejected_filesystem_prx.stderr

        server_prx = run_validator(
            make_evidence(directory, filesystem_prx_gate=True, server_prx_gate=True),
            filesystem_prx_gate=True, server_prx_gate=True)
        assert server_prx.returncode == 0, server_prx.stderr
        server_summary = json.loads(server_prx.stdout)
        assert server_summary["server_prx_gate"]
        assert server_summary["server_prx_active_after_unload"] == 1
        missing_server_prx = run_validator(
            make_evidence(directory, filesystem_prx_gate=True), server_prx_gate=True)
        assert missing_server_prx.returncode != 0 \
            and "not enabled" in missing_server_prx.stderr

        menu_prx = run_validator(
            make_evidence(directory, spawn=False, mode="client", menu_prx_gate=True),
            mode="client", filesystem_prx_gate=True, menu_prx_gate=True)
        assert menu_prx.returncode == 0, menu_prx.stderr
        menu_summary = json.loads(menu_prx.stdout)
        assert menu_summary["menu_prx_gate"]
        assert menu_summary["menu_prx_redraw_calls"] == 300
        missing_menu_prx = run_validator(
            make_evidence(directory, spawn=False, mode="client",
                          filesystem_prx_gate=True),
            mode="client", filesystem_prx_gate=True, menu_prx_gate=True)
        assert missing_menu_prx.returncode != 0 \
            and "not enabled" in missing_menu_prx.stderr
        bad_menu_prx = make_evidence(
            directory, spawn=False, mode="client", menu_prx_gate=True)
        bad_menu_log = directory / json.loads(bad_menu_prx.read_text())["log_path"]
        bad_menu_text = bad_menu_log.read_text().replace(
            "XASH_MENU_PRX_API result=1", "XASH_MENU_PRX_API result=0", 1)
        bad_menu_log.write_text(bad_menu_text)
        bad_menu_data = bad_menu_log.read_bytes()
        bad_menu_manifest = json.loads(bad_menu_prx.read_text())
        bad_menu_manifest["bytes"] = len(bad_menu_data)
        bad_menu_manifest["sha256"] = hashlib.sha256(bad_menu_data).hexdigest()
        bad_menu_prx.write_text(json.dumps(bad_menu_manifest))
        rejected_menu_prx = run_validator(
            bad_menu_prx, mode="client", filesystem_prx_gate=True,
            menu_prx_gate=True)
        assert rejected_menu_prx.returncode != 0 \
            and "base API contract" in rejected_menu_prx.stderr

        bad_prx = make_evidence(directory, prx_gate=True)
        bad_prx_log = directory / json.loads(bad_prx.read_text())["log_path"]
        bad_prx_text = bad_prx_log.read_text().replace(
            "XASH_PRX_COMPLETE pass=1", "XASH_PRX_COMPLETE pass=0", 1)
        bad_prx_log.write_text(bad_prx_text)
        bad_prx_data = bad_prx_log.read_bytes()
        bad_prx_manifest = json.loads(bad_prx.read_text())
        bad_prx_manifest["bytes"] = len(bad_prx_data)
        bad_prx_manifest["sha256"] = hashlib.sha256(bad_prx_data).hexdigest()
        bad_prx.write_text(json.dumps(bad_prx_manifest))
        rejected_prx = run_validator(bad_prx, prx_gate=True)
        assert rejected_prx.returncode != 0 and "completion contract" in rejected_prx.stderr

        underrun = run_validator(
            make_evidence(directory, audio_gate=True, audio_underruns=1), audio_gate=True)
        assert underrun.returncode != 0 and "underrun" in underrun.stderr

        # A short final block is refused: 72191 is not a whole number of blocks.
        short_block = run_validator(
            make_evidence(directory, audio_gate=True, audio_sent=72191), audio_gate=True)
        assert short_block.returncode != 0 and "whole 256-frame blocks" in short_block.stderr

        # Wrong conversion count is refused even when the blocks are whole.
        wrong_ratio = run_validator(
            make_evidence(directory, audio_gate=True, audio_sent=71936, audio_padding=193),
            audio_gate=True)
        assert wrong_ratio.returncode != 0 and "ratio mismatch" in wrong_ratio.stderr

        # A consumed-PCM hash that does not match the generated pattern is refused.
        wrong_hash = run_validator(
            make_evidence(directory, audio_gate=True, audio_source_hash="0xdeadbeefdeadbeef"),
            audio_gate=True)
        assert wrong_hash.returncode != 0 and "hash" in wrong_hash.stderr

        # A negative drain result is refused; a positive frame count is not.
        bad_drain = run_validator(
            make_evidence(directory, audio_gate=True, audio_drain_rc=-22), audio_gate=True)
        assert bad_drain.returncode != 0 and "drain returned an error" in bad_drain.stderr

        # One telemetry line per block is refused.
        chatty = run_validator(
            make_evidence(directory, audio_gate=True, audio_progress=300), audio_gate=True)
        assert chatty.returncode != 0 and "line per block" in chatty.stderr

        manifest = make_evidence(directory)
        log_path = directory / json.loads(manifest.read_text())["log_path"]
        with log_path.open("ab") as handle:
            handle.write(b"tamper\n")
        integrity = run_validator(manifest)
        assert integrity.returncode != 0 and "size/hash" in integrity.stderr
    print("engine boot evidence validator tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
