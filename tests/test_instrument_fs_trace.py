#!/usr/bin/env python3
from __future__ import annotations

import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / "xash" / "tools"))

import instrument_fs_trace as trace  # noqa: E402


def main() -> int:
    upstream = ROOT / "third_party" / "xash3d-fwgs" / "filesystem"
    io_source = (upstream / "io.c").read_text()
    search_source = (upstream / "searchpath.c").read_text()
    imagelib = ROOT / "third_party" / "xash3d-fwgs" / "engine" / "common" / "imagelib"
    img_main_source = (imagelib / "img_main.c").read_text()
    img_wad_source = (imagelib / "img_wad.c").read_text()

    instrumented_io = trace.instrument_io(io_source)
    instrumented_search = trace.instrument_searchpath(search_source)
    instrumented_img_main = trace.instrument_img_main(img_main_source)
    instrumented_img_wad = trace.instrument_img_wad(img_wad_source)
    assert "guarded_netpath.guard" in instrumented_io
    assert "PS5_XASH_FS_TRACE_PATH" in instrumented_io
    assert '"gfx/palette.lmp"' not in instrumented_io
    assert "sizeof( guarded_netpath.bytes )" in instrumented_io
    assert "event=corrupt" in instrumented_io
    assert "XASH_FS_ARCHIVE event=enter" in instrumented_io
    assert "XASH_FS_ARCHIVE event=call_load" in instrumented_io
    assert "XASH_FS_ARCHIVE event=call_open" in instrumented_io
    assert "XASH_FS_ARCHIVE event=open_return" in instrumented_io
    assert "XASH_FS_ARCHIVE event=call_alloc" in instrumented_io
    assert "XASH_FS_ARCHIVE event=alloc_return" in instrumented_io
    assert "XASH_FS_ARCHIVE event=call_read" in instrumented_io
    assert "XASH_FS_ARCHIVE event=read_return" in instrumented_io
    assert "XASH_FS_ARCHIVE event=call_close" in instrumented_io
    assert "XASH_FS_ARCHIVE event=close_return" in instrumented_io
    assert "XASH_FS_ARCHIVE event=call_sizeptr" in instrumented_io
    assert "XASH_FS_ARCHIVE event=sizeptr_return" in instrumented_io
    assert "XASH_FS_ARCHIVE event=return" in instrumented_io
    assert "before_callback" in instrumented_search
    assert "terminated=%d" in instrumented_search
    assert not any(line.startswith("+") for line in instrumented_search.splitlines())
    assert instrumented_io.count("PS5_FSTraceGuardBegin") == 2  # definition + call
    assert instrumented_search.count("search->pfnFindFile( search, name") == 1
    assert "XASH_IMAGE event=call_loader" in instrumented_img_main
    assert "PS5_XASH_FS_TRACE_PATH" in instrumented_img_main
    assert "XASH_IMAGE event=loader_return" in instrumented_img_main
    assert "XASH_IMAGE event=pal_enter" in instrumented_img_wad
    assert "XASH_IMAGE event=call_get_palette" in instrumented_img_wad
    assert "XASH_IMAGE event=get_palette_return" in instrumented_img_wad
    assert "XASH_IMAGE event=call_copy_palette" in instrumented_img_wad
    assert "XASH_IMAGE event=copy_palette_return" in instrumented_img_wad

    with tempfile.TemporaryDirectory() as temporary:
        output = Path(temporary)
        trace.generate(upstream / "io.c", upstream / "searchpath.c",
                       imagelib / "img_main.c", imagelib / "img_wad.c", output)
        assert (output / "io.c").read_text() == instrumented_io
        assert (output / "searchpath.c").read_text() == instrumented_search
        assert (output / "img_main.c").read_text() == instrumented_img_main
        assert (output / "img_wad.c").read_text() == instrumented_img_wad

    try:
        trace.instrument_io(io_source.replace('static byte *FS_LoadFile_', 'static byte *moved_', 1))
    except ValueError as error:
        assert "FS_LoadFile_ guarded buffer" in str(error)
    else:
        raise AssertionError("changed upstream anchor did not fail closed")

    print("filesystem trace instrumentation tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
