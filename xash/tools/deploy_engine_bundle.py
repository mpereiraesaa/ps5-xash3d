#!/usr/bin/env python3
"""Transactionally promote an engine eboot and application-owned PRXs."""

from __future__ import annotations

import argparse
import ftplib
import hashlib
import json
import re
import socket
from datetime import datetime, timezone
from pathlib import Path, PurePosixPath


TITLE_ID = "PPSA99996"
REMOTE_ROOT = PurePosixPath("/data/homebrew") / TITLE_ID
SAFE_MODULE = re.compile(r"^[A-Za-z0-9_-]+\.prx$")


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def file_exists(ftp: ftplib.FTP, path: str) -> bool:
    try:
        size = ftp.size(path)
        return size is not None and size != 0xFFFFFFFFFFFFFFFF
    except ftplib.error_perm as exc:
        if str(exc).startswith("550"):
            return False
        raise


def delete_file(ftp: ftplib.FTP, path: str) -> None:
    try:
        ftp.delete(path)
    except ftplib.error_reply as exc:
        if str(exc) != "226 File deleted":
            raise


def shsrv_size(host: str, path: str) -> int:
    data = bytearray()
    with socket.create_connection((host, 2323), 8) as sock:
        sock.settimeout(8)
        while b"$ " not in data and len(data) < 16384:
            data.extend(sock.recv(4096))
        start = len(data)
        sock.sendall(f"stat {path}\n".encode("ascii"))
        while len(data) < 32768:
            chunk = sock.recv(4096)
            if not chunk:
                break
            data.extend(chunk)
            if b"$ " in data[start:]:
                break
    match = re.search(rb"(?:^|\n)size: (\d+)(?:\r?$|\n)", data[start:], re.MULTILINE)
    if not match:
        raise RuntimeError(f"shsrv stat was not parseable: {path}")
    return int(match.group(1))


def verify_transformed_self(ftp: ftplib.FTP, remote: str, elf: Path) -> int:
    expected = elf.read_bytes()
    actual = bytearray()
    ftp.retrbinary(f"RETR {remote}", actual.extend)
    stable = max(0, len(expected) - 512)
    if (not expected.startswith(b"\x7fELF") or len(actual) != len(expected)
            or bytes(actual[:stable]) != expected[:stable]):
        raise RuntimeError(f"FTP-transformed SELF verification failed: {remote}")
    return stable


def bundle(local_root: Path, modules: list[str]) -> list[tuple[Path, PurePosixPath]]:
    if len(modules) != len(set(modules)):
        raise ValueError("duplicate module name")
    items = [(local_root / "eboot.bin", REMOTE_ROOT / "eboot.bin")]
    for name in modules:
        if not SAFE_MODULE.fullmatch(name):
            raise ValueError(f"unsafe module name: {name}")
        items.append((local_root / "sce_module" / name,
                      REMOTE_ROOT / "sce_module" / name))
    for local, _ in items:
        if not local.is_file() or local.stat().st_size == 0:
            raise FileNotFoundError(f"missing bundle artifact: {local}")
        if local.read_bytes()[:4] not in (bytes.fromhex("4f153d1d"),
                                          bytes.fromhex("5414f5ee")):
            raise ValueError(f"artifact is not a SELF container: {local}")
    return items


def promote(host: str, local_root: Path, modules: list[str], journal: Path) -> None:
    local_root = local_root.resolve()
    items = bundle(local_root, modules)
    tag = digest(items[0][0])[:12]
    staged: list[tuple[str, str, bool]] = []
    promoted: list[tuple[str, str, bool]] = []
    committed = False

    def record(event: str, **fields: object) -> None:
        journal.parent.mkdir(parents=True, exist_ok=True)
        entry = {"at_utc": datetime.now(timezone.utc).isoformat(),
                 "event": event, **fields}
        with journal.open("a", encoding="utf-8") as stream:
            stream.write(json.dumps(entry, sort_keys=True) + "\n")
        print(json.dumps(entry, sort_keys=True), flush=True)

    with ftplib.FTP() as ftp:
        ftp.connect(host, 2121, 8)
        ftp.login()
        try:
            for local, remote_path in items:
                live = str(remote_path)
                stage = str(remote_path.parent / f".{remote_path.name}.new-{tag}")
                backup = str(remote_path.parent / f".{remote_path.name}.previous-{tag}")
                if file_exists(ftp, stage) or file_exists(ftp, backup):
                    raise RuntimeError(f"staging collision; no promotion attempted: {live}")
                had_live = file_exists(ftp, live)
                # Register ownership before the first write so a failed upload
                # or verification still removes the exact staging path.
                staged.append((stage, backup, had_live))
                with local.open("rb") as stream:
                    ftp.storbinary(f"STOR {stage}", stream)
                verification = "shsrv-size"
                stable_bytes = 0
                try:
                    size = shsrv_size(host, stage)
                    if size != local.stat().st_size:
                        raise RuntimeError(f"stored size mismatch: {stage}: {size}")
                except (OSError, TimeoutError):
                    project = local_root.parents[2]
                    if local.name == "eboot.bin":
                        elf = project / "build/engine-boot/eboot.elf"
                    else:
                        elf = project / "build/engine-boot/prx" / local.name.replace(".prx", ".elf")
                    stable_bytes = verify_transformed_self(ftp, stage, elf)
                    size = local.stat().st_size
                    verification = "ftp-transformed-elf"
                record("engine_bundle_staged", live=live, staged=stage,
                       bytes=size, fself_sha256=digest(local),
                       verification=verification, stable_verified_bytes=stable_bytes)
            for (_, remote_path), (stage, backup, had_live) in zip(items, staged):
                live = str(remote_path)
                if had_live:
                    ftp.rename(live, backup)
                try:
                    ftp.rename(stage, live)
                except Exception:
                    if had_live and not file_exists(ftp, live):
                        ftp.rename(backup, live)
                    raise
                promoted.append((live, backup, had_live))
            committed = True
            for _, backup, had_live in promoted:
                if had_live:
                    delete_file(ftp, backup)
            record("engine_bundle_promoted", title_id=TITLE_ID, tag=tag,
                   items=[{"remote": str(remote), "fself_sha256": digest(local),
                           "bytes": local.stat().st_size}
                          for local, remote in items], rollback_retained=False)
        except Exception:
            if committed:
                record("engine_bundle_cleanup_failed", title_id=TITLE_ID,
                       tag=tag, live_bundle_retained=True,
                       backup_cleanup_incomplete=True)
                raise
            for live, backup, had_live in reversed(promoted):
                try:
                    if file_exists(ftp, live):
                        delete_file(ftp, live)
                    if had_live and file_exists(ftp, backup):
                        ftp.rename(backup, live)
                except ftplib.all_errors:
                    pass
            for stage, _, _ in staged:
                try:
                    if file_exists(ftp, stage):
                        delete_file(ftp, stage)
                except ftplib.all_errors:
                    pass
            raise


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", required=True)
    parser.add_argument("--local-root", required=True, type=Path)
    parser.add_argument("--module", action="append", default=[])
    parser.add_argument("--journal", required=True, type=Path)
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    items = bundle(args.local_root, args.module)
    plan = {"title_id": TITLE_ID,
            "items": [{"local": str(local), "remote": str(remote),
                       "bytes": local.stat().st_size, "fself_sha256": digest(local)}
                      for local, remote in items]}
    if not args.apply:
        print(json.dumps(plan, indent=2, sort_keys=True))
        return 0
    promote(args.host, args.local_root, args.module, args.journal)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
