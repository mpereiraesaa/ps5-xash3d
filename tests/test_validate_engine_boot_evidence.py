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


def make_evidence(directory: Path, *, spawn: bool = True, exit_result: int = 0,
                  mode: str = "dedicated", nonzero: int = 1,
                  pad_gate: bool = False) -> Path:
    ref = "soft" if mode == "client" else "none"
    structured = [
        ("INFO", "LOG_SCHEMA=3"),
        ("INFO", "LOG_BOOT_MONOTONIC_NS=0x1234"),
        ("MARK", f"XASH_BOOT schema=1 slice=engine-boot mode={mode} ref={ref} fw=12.02 "
                 f"engine={ENGINE} hlsdk={HLSDK} rodir=/app0/xash3d basedir=/download0/xash3d "
                 f"gamedir=valve map=c1a0 gate_seconds=90 pad_gate={int(pad_gate)} rodir_present=1"),
    ]
    if mode == "client":
        structured.append(("MARK", "XASH_FRAME source=software presented=300 width=640 height=480 "
                                   f"hash=0123456789abcdef nonzero={nonzero}"))
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
    structured.append(("MARK", f"XASH_EXIT result={exit_result}"))
    console = [
        "Xash3D FWGS 49/0.21 (freebsd-amd64 build 4900)",
        "FS_LoadProgs: filesystem_stdio successfully loaded",
        "Spawn Server: c1a0" if spawn else "map c1a0: map not found",
        "XASH_PAD_GATE_PASS action=quit" if pad_gate else
        "PS5_XASH_GATE_TIMEOUT seconds=90 action=quit",
    ]
    if mode == "client":
        console += ["Loading renderer: soft -> ref_soft", "Renderer ref_soft initialized"]
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
                  pad_gate: bool = False) -> subprocess.CompletedProcess[str]:
    command = ["python3", "-B", str(VALIDATOR), str(manifest),
               "--engine-commit", engine, "--hlsdk-commit", HLSDK,
               "--map", "c1a0", "--mode", mode]
    if pad_gate:
        command.append("--pad-gate")
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
