#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import json
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
VALIDATOR = ROOT / "tools" / "validate_ref_agc_prx_evidence.py"
ENGINE = "9aa39ad"
HLSDK = "e277ffa"
BUNDLE = "1" * 64
STUDIO = "2" * 64


def write_run(directory: Path, name: str, app: str, messages: list[str], *,
              raw: list[str] | None = None, started: str) -> Path:
    raw = raw or []
    boot = "0x1234" if app == "xash3d-engine" else "0x5678"
    reason = "xash-engine-boot-complete" if app == "xash3d-engine" \
        else "ref-agc-runtime-complete"
    lines = [f"HELLO ps5log/1 title=PPSA99996 app={app} boot={boot} tag=test"]
    for seq, message in enumerate(messages, 1):
        lines.append(f"{seq}\t{seq * 10}\tMARK\t{message}")
        if seq == 2:
            lines.extend(raw)
    lines.append(f"BYE seq={len(messages)} reason={reason}")
    data = ("\n".join(lines) + "\n").encode()
    log_name = name + ".log"
    (directory / log_name).write_bytes(data)
    manifest = {
        "identity": {"title": "PPSA99996", "app": app, "boot": boot},
        "protocol": "ps5log/1", "transport": "tcp", "hello": True,
        "bye": True, "clean": True, "gaps": [], "oversized_lines": 0,
        "raw_lines": len(raw), "records": len(messages),
        "last_seq": len(messages), "log_path": log_name,
        "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest(),
        "run_id": name, "started_utc": started,
        "bye_fields": {"seq": str(len(messages)), "reason": reason},
    }
    path = directory / (name + ".json")
    path.write_text(json.dumps(manifest), encoding="utf-8")
    return path


def engine_messages(frame_hash: str = "d8c9aadab3c82cdb") -> list[str]:
    complete = [
        "XASH_SERVER_PRX_COMPLETE module=server.prx stop_result=0 active_modules=4 ownership=exact",
        "XASH_MENU_PRX_COMPLETE module=menu.prx stop_result=0 active_modules=3 ownership=exact pass=1",
        "XASH_CLIENT_PRX_COMPLETE module=client.prx stop_result=0 active_modules=2 ownership=exact pass=1",
    ]
    return [
        "LOG_BOOT_MONOTONIC_NS=0x1234",
        f"XASH_BOOT schema=1 slice=engine-boot mode=client ref=agc fw=12.02 engine={ENGINE} "
        f"hlsdk={HLSDK} rodir=/app0/xash3d basedir=/download0/xash3d gamedir=valve "
        "map=c1a0 gate_seconds=20 pad_gate=0 audio_gate=0 memory_gate=0 "
        "thread_time_gate=0 libc_shim_gate=0 prx_gate=0 filesystem_prx=1 "
        "server_prx=1 menu_prx=1 client_prx=1 ref_agc_prx=1 rodir_present=1",
        "XASH_PRX_LOAD path=/app0/sce_module/ref_agc.prx module=ref_agc.prx handle=0xd3 "
        "segments=4 exports=16 init_result=0 result=0",
        "XASH_REF_AGC_PRX_READY module=ref_agc.prx api=18 state=0 runtime_result=0 "
        "teardown_result=0 engine_mask=0 expected_mask=63 frames=0 "
        "frame_hash=0000000000000000 bright_pixels=0 begin_calls=0 scene_calls=0 "
        "end_calls=0 newmap_calls=0 backend=phase4-native ownership=fence+videoout pass=1",
        *complete,
        "XASH_REF_AGC_PRX_STATE module=ref_agc.prx api=18 state=5 runtime_result=0 "
        f"teardown_result=0 engine_mask=63 expected_mask=63 frames=600 frame_hash={frame_hash} "
        "bright_pixels=820521 begin_calls=100 scene_calls=99 end_calls=100 newmap_calls=1 "
        "backend=phase4-native ownership=fence+videoout pass=1",
        "XASH_PRX_UNLOAD module=ref_agc.prx result=0 stop_result=0 ownership=released",
        "XASH_REF_AGC_PRX_COMPLETE module=ref_agc.prx stop_result=0 active_modules=1 "
        "ownership=exact pass=1",
        "XASH_FS_PRX_COMPLETE module=filesystem_stdio.prx stop_result=0 active_modules=0 "
        "ownership=exact",
        "XASH_EXIT result=0 ref_agc_prx=1",
    ]


def renderer_messages(errors: str = "0") -> list[str]:
    return [
        f"BSP_TEXTURE_PATH_BOOT schema=1 slice=goldsrc-phase4-final target=gfx1013 fw=12.02 "
        f"ownership=fence+videoout bundle_sha256={BUNDLE} bundle_bytes=100 studio_sha256={STUDIO} "
        "studio_bytes=200 soak_frames=600 input_gate=not-required",
        "BSP_LOOP_BEGIN mode=ref-agc-integration frames=600 combined_window=0..599 "
        "geometry=phase4-baked-c1a0e retirement=fence+videoout input_dependency=none",
        "REF_AGC_RUNTIME_READY backend=phase4-native api=18 videoout=owned "
        "direct_memory=owned agc=initialized scene=planned",
        f"BSP_RESOURCE_READBACK buffer0=a9e62c5188ca6bf5 buffer1=0044418de19349d8 "
        f"bright_pixels0=1 bright_pixels1=2 guards=intact frames=600 errors={errors}",
        "GOLDSRC_PHASE4_FINAL_COMPLETE schema=1 frames=600 combined_frames=600 readbacks=2 "
        "both_slots=true tokens=exact guards=intact errors=0 ownership=fence+videoout "
        "sprites=true particles=true cpu_skinning=true brush_entities=true water=true "
        "glass=true pvs=true frustum=true animation_changes=true",
        "RESOURCE_POOL_RETIRED token=1 reclaimed=6 completion=fence+videoout",
        "REF_AGC_TEARDOWN videoout=closed direct_memory=released agc=unloaded "
        "result=0 ownership=exact",
    ]


def run(engine: Path, renderer: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run([
        "python3", "-B", str(VALIDATOR), str(engine), str(renderer),
        "--engine-commit", ENGINE, "--hlsdk-commit", HLSDK,
        "--bundle-sha256", BUNDLE, "--bundle-bytes", "100",
        "--studio-sha256", STUDIO, "--studio-bytes", "200",
    ], text=True, capture_output=True, check=False)


def main() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        directory = Path(temporary)
        raw = ["filesystem_stdio successfully loaded", "Spawn Server: c1a0",
               'Dll loaded for game "Half-Life"', "Game started",
               "Loading renderer: agc -> ref_agc",
               "PS5_XASH_GATE_TIMEOUT seconds=20 action=quit"]
        engine = write_run(directory, "engine", "xash3d-engine", engine_messages(),
                           raw=raw, started="2026-09-08T19:13:27.933+00:00")
        renderer = write_run(directory, "renderer", "ps5-xash3d", renderer_messages(),
                             started="2026-09-08T19:13:27.984+00:00")
        valid = run(engine, renderer)
        assert valid.returncode == 0, valid.stderr
        summary = json.loads(valid.stdout)
        assert summary["pass"] and summary["frames"] == 600
        assert summary["start_skew_ms"] == 51

        bad_engine = write_run(directory, "bad-engine", "xash3d-engine",
                               engine_messages("0000000000000000"), raw=raw,
                               started="2026-09-08T19:13:27.933+00:00")
        rejected = run(bad_engine, renderer)
        assert rejected.returncode != 0 and "runtime state" in rejected.stderr

        bad_renderer = write_run(directory, "bad-renderer", "ps5-xash3d",
                                 renderer_messages("1"),
                                 started="2026-09-08T19:13:27.984+00:00")
        rejected = run(engine, bad_renderer)
        assert rejected.returncode != 0 and "GPU readback" in rejected.stderr
    print("ref_agc paired evidence validator tests passed")


if __name__ == "__main__":
    main()
