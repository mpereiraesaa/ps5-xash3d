#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
import struct
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("bake_studio",
                                              ROOT / "tools/bake_studio.py")
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


def synthetic_mdl() -> bytes:
    size = 1732
    data = bytearray(size)
    data[:4] = b"IDST"
    struct.pack_into("<i", data, 4, 10)
    data[8:17] = b"unit.mdl\0"
    struct.pack_into("<i", data, 72, size)
    values = [0] * 27
    values[1:3] = [1, 244]       # bones
    values[7:9] = [1, 356]       # sequences
    values[9:11] = [1, 0]        # sequence groups
    values[11:14] = [1, 532, 960]  # textures
    values[14:17] = [1, 1, 612]  # skins
    values[17:19] = [1, 616]     # bodyparts
    struct.pack_into("<27i", data, 136, *values)

    MODULE.BONE.pack_into(
        data, 244, b"root", -1, 0, *([-1] * 6),
        *(0.0 for _ in range(6)), *(1.0 for _ in range(6)))
    data[356:361] = b"move\0"
    struct.pack_into("<f", data, 388, 1.0)
    struct.pack_into("<i", data, 392, 1)   # looping
    struct.pack_into("<i", data, 412, 2)   # frames
    struct.pack_into("<i", data, 476, 1)   # blends
    struct.pack_into("<i", data, 480, 932) # animation offset
    struct.pack_into("<i", data, 512, 0)   # sequence group

    MODULE.TEXTURE.pack_into(data, 532, b"unit.bmp", 0, 0, 2, 2, 960)
    struct.pack_into("<h", data, 612, 0)
    MODULE.BODYPART.pack_into(data, 616, b"studio", 1, 1, 692)
    MODULE.MODEL.pack_into(data, 692, b"triangle", 0, 0.0,
                           1, 804, 3, 824, 832, 3, 827, 868, 0, 0)
    MODULE.MESH.pack_into(data, 804, 1, 904, 0, 3, 0)
    data[824:827] = b"\0\0\0"
    data[827:830] = b"\0\0\0"
    struct.pack_into("<9f", data, 832,
                     -1.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0)
    struct.pack_into("<9f", data, 868,
                     0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0)
    struct.pack_into("<h", data, 904, -3)
    struct.pack_into("<4h", data, 906, 0, 0, 0, 0)
    struct.pack_into("<4h", data, 914, 1, 1, 2, 0)
    struct.pack_into("<4h", data, 922, 2, 2, 0, 2)
    struct.pack_into("<h", data, 930, 0)
    struct.pack_into("<6H", data, 932, 12, 0, 0, 0, 0, 0)
    data[944:946] = bytes((2, 2))
    struct.pack_into("<2h", data, 946, 0, 10)
    data[960:964] = bytes((0, 1, 2, 3))
    for index in range(256):
        data[964 + index * 3:967 + index * 3] = bytes(
            (index, 255 - index, index // 2))
    return bytes(data)


def main() -> None:
    source = synthetic_mdl()
    output = MODULE.bake(source, "move")
    again = MODULE.bake(source, "move")
    assert output == again
    header = MODULE.HEADER.unpack_from(output)
    assert header[0] == MODULE.MAGIC and header[1] == 1
    assert header[2] == MODULE.HEADER.size and header[3] == len(output)
    assert header[8:10] == (1, 2)
    assert header[10] == 1.0 and header[11] == 1
    assert header[12:16] == (3, 3, 1, 1)
    assert header[16:22] == (-1.0, 0.0, 0.0, 1.0, 1.0, 0.0)
    pose_offset = header[23]
    first = MODULE.POSE.unpack_from(output, pose_offset)
    second = MODULE.POSE.unpack_from(output, pose_offset + MODULE.POSE.size)
    assert first[:3] == (0.0, 0.0, 0.0)
    assert second[:3] == (10.0, 0.0, 0.0)
    assert first[3:] == second[3:] == (0.0, 0.0, 0.0, 1.0)
    texture_offset = header[27]
    texture = MODULE.OUTPUT_TEXTURE.unpack_from(output, texture_offset)
    assert texture[2:5] == (2, 2, 256)
    pixel_offset = header[28]
    assert output[pixel_offset:pixel_offset + 4] == bytes((0, 255, 0, 255))
    assert output[pixel_offset + 4:pixel_offset + 8] == bytes((1, 254, 0, 255))
    try:
        MODULE.bake(source, "missing")
    except MODULE.BakeError:
        pass
    else:
        raise AssertionError("missing sequence was accepted")
    corrupt = bytearray(source)
    corrupt[:4] = b"BAD!"
    try:
        MODULE.bake(bytes(corrupt), "move")
    except MODULE.BakeError:
        pass
    else:
        raise AssertionError("bad Studio magic was accepted")


if __name__ == "__main__":
    main()
