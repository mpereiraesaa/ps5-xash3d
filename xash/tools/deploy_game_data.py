#!/usr/bin/env python3
"""Transactionally replace PPSA99996's private Xash3D game-data tree."""

from __future__ import annotations

import argparse
import ftplib
import hashlib
import json
from datetime import datetime, timezone
from pathlib import Path, PurePosixPath


TITLE_ID = "PPSA99996"
REMOTE_TITLE = PurePosixPath("/data/homebrew") / TITLE_ID


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def manifest(local_root: Path) -> list[tuple[Path, str, int]]:
    return [
        (path, path.relative_to(local_root).as_posix(), path.stat().st_size)
        for path in sorted(local_root.rglob("*"))
        if path.is_file()
    ]


def exists_dir(ftp: ftplib.FTP, path: str) -> bool:
    current = ftp.pwd()
    try:
        ftp.cwd(path)
        return True
    except ftplib.error_perm as error:
        if str(error).startswith("550"):
            return False
        raise
    finally:
        ftp.cwd(current)


def exists_file(ftp: ftplib.FTP, path: str) -> bool:
    try:
        size = ftp.size(path)
        return size is not None and size != 0xFFFFFFFFFFFFFFFF
    except ftplib.error_perm as error:
        if str(error).startswith("550"):
            return False
        raise


def make_parents(ftp: ftplib.FTP, root: PurePosixPath, relative: str,
                 created: set[str]) -> None:
    current = root
    for part in PurePosixPath(relative).parent.parts:
        if part in ("", "."):
            continue
        current /= part
        remote = str(current)
        if remote not in created:
            ftp.mkd(remote)
            created.add(remote)


def verify_hash(ftp: ftplib.FTP, remote: str, expected: str) -> None:
    digest = hashlib.sha256()
    ftp.retrbinary(f"RETR {remote}", digest.update, blocksize=1024 * 1024)
    observed = digest.hexdigest()
    if observed != expected:
        raise RuntimeError(f"hash mismatch for {remote}: {observed} != {expected}")


def parse_list_line(line: str) -> tuple[str, bool]:
    fields = line.split(None, 8)
    if len(fields) != 9 or not fields[0]:
        raise RuntimeError(f"unrecognized FTP LIST record: {line!r}")
    return fields[8], fields[0][0] == "d"


def delete_file(ftp: ftplib.FTP, path: str) -> None:
    try:
        ftp.delete(path)
    except ftplib.error_reply as error:
        if str(error) != "226 File deleted":
            raise


def remove_tree(ftp: ftplib.FTP, root: PurePosixPath) -> tuple[int, int]:
    lines: list[str] = []
    ftp.retrlines(f"LIST {root}", lines.append)
    files = directories = 0
    for line in lines:
        name, is_directory = parse_list_line(line)
        if name in (".", ".."):
            continue
        child = root / name
        if is_directory:
            child_files, child_directories = remove_tree(ftp, child)
            files += child_files
            directories += child_directories
        else:
            delete_file(ftp, str(child))
            files += 1
    ftp.rmd(str(root))
    return files, directories + 1


def validated_backup_path(value: str) -> PurePosixPath:
    path = PurePosixPath(value)
    if path.parent != REMOTE_TITLE or not path.name.startswith(".xash3d.previous-"):
        raise ValueError("backup must be an exact PPSA99996 game-data rollback path")
    return path


def prune_backup(host: str, port: int, value: str, journal: Path) -> None:
    backup = validated_backup_path(value)
    with ftplib.FTP() as ftp:
        ftp.connect(host, port, 30)
        ftp.login()
        if not exists_dir(ftp, str(backup)):
            raise RuntimeError(f"backup does not exist: {backup}")
        files, directories = remove_tree(ftp, backup)
    record(journal, "game_data_backup_pruned", backup=str(backup),
           files=files, directories=directories)


def record(journal: Path, event: str, **fields: object) -> None:
    entry = {"at_utc": datetime.now(timezone.utc).isoformat(),
             "event": event, **fields}
    journal.parent.mkdir(parents=True, exist_ok=True)
    with journal.open("a", encoding="utf-8") as stream:
        stream.write(json.dumps(entry, sort_keys=True) + "\n")
    print(json.dumps(entry, sort_keys=True), flush=True)


def deploy(host: str, port: int, local_root: Path, journal: Path) -> str:
    files = manifest(local_root)
    index = local_root / ".dirindex"
    if not index.is_file() or not files:
        raise RuntimeError("local game-data tree or .dirindex is missing")
    suffix = sha256(index)[:12]
    staging = REMOTE_TITLE / f".xash3d.staging-{suffix}"
    backup = REMOTE_TITLE / f".xash3d.previous-{suffix}"
    live = REMOTE_TITLE / "xash3d"
    total_bytes = sum(item[2] for item in files)

    record(journal, "game_data_plan", title=TITLE_ID, files=len(files),
           bytes=total_bytes, staging=str(staging), live=str(live),
           backup=str(backup), apply=True)
    with ftplib.FTP() as ftp:
        ftp.connect(host, port, 30)
        ftp.login()
        if exists_dir(ftp, str(staging)) or exists_dir(ftp, str(backup)):
            raise RuntimeError("staging or backup collision; no write attempted")
        if not exists_dir(ftp, str(live)):
            raise RuntimeError("live xash3d tree missing; no write attempted")
        ftp.mkd(str(staging))
        created = {str(staging)}
        sent = 0
        for number, (local, relative, size) in enumerate(files, 1):
            make_parents(ftp, staging, relative, created)
            remote = str(staging / relative)
            with local.open("rb") as stream:
                ftp.storbinary(f"STOR {remote}", stream, blocksize=1024 * 1024)
            observed = ftp.size(remote)
            if observed != size:
                raise RuntimeError(f"size mismatch for {remote}: {observed} != {size}")
            sent += size
            if number % 100 == 0 or number == len(files):
                record(journal, "game_data_progress", files=number,
                       total_files=len(files), bytes=sent, total_bytes=total_bytes)

        for relative in (".dirindex", "valve/delta.lst", "valve/gfx/palette.lmp"):
            local = local_root / relative
            verify_hash(ftp, str(staging / relative), sha256(local))
            record(journal, "game_data_critical_verified", path=relative,
                   bytes=local.stat().st_size, sha256=sha256(local))

        promoted = False
        try:
            ftp.rename(str(live), str(backup))
            ftp.rename(str(staging), str(live))
            promoted = True
        finally:
            if not promoted and not exists_dir(ftp, str(live)) and exists_dir(ftp, str(backup)):
                ftp.rename(str(backup), str(live))

    record(journal, "game_data_promoted", title=TITLE_ID, files=len(files),
           bytes=total_bytes, live=str(live), backup=str(backup),
           backup_retained=True, index_sha256=sha256(index))
    return str(backup)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", required=True)
    parser.add_argument("--port", type=int, default=2121)
    parser.add_argument("--local-root", type=Path, required=True)
    parser.add_argument("--journal", type=Path, required=True)
    parser.add_argument("--apply", action="store_true")
    parser.add_argument("--prune-backup")
    args = parser.parse_args()
    if args.prune_backup:
        prune_backup(args.host, args.port, args.prune_backup, args.journal)
        return 0
    files = manifest(args.local_root)
    summary = {"title": TITLE_ID, "files": len(files),
               "bytes": sum(item[2] for item in files),
               "local_root": str(args.local_root)}
    if not args.apply:
        print(json.dumps(summary, sort_keys=True))
        return 0
    deploy(args.host, args.port, args.local_root, args.journal)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
