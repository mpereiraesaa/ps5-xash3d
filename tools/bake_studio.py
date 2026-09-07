#!/usr/bin/env python3
"""Bake one owned GoldSrc Studio v10 model into a private PS5 runtime bundle."""

from __future__ import annotations

import argparse
import math
import struct
import zlib
from dataclasses import dataclass
from pathlib import Path


MAGIC = b"PS5MDL\0\0"
VERSION = 1
HEADER = struct.Struct("<8sIIIIQQQIIfIIIII6f8I6I")
BONE = struct.Struct("<32sii6i6f6f")
SEQUENCE = struct.Struct("<32sf10i3f2i6f4i4f6i")
TEXTURE = struct.Struct("<64sHHiii")
BODYPART = struct.Struct("<64siii")
MODEL = struct.Struct("<64sif10i")
MESH = struct.Struct("<5i")
SOURCE_VERTEX = struct.Struct("<8fHH")
POSE = struct.Struct("<7f")
DRAW = struct.Struct("<4I")
OUTPUT_TEXTURE = struct.Struct("<6IQ")

STUDIO_LOOPING = 1
STUDIO_NF_CHROME = 2
STUDIO_NF_ADDITIVE = 0x20
STUDIO_NF_MASKED = 0x40
MAX_BONES = 128
MAX_FRAMES = 4096
MAX_VERTICES = 65535
MAX_TEXTURE_DIMENSION = 2048


class BakeError(ValueError):
    pass


@dataclass(frozen=True)
class SourceVertex:
    position: tuple[float, float, float]
    normal: tuple[float, float, float]
    uv: tuple[float, float]
    bone: int
    normal_bone: int


@dataclass(frozen=True)
class DrawInfo:
    first_index: int
    index_count: int
    texture: int
    flags: int


def _range(data: bytes, offset: int, size: int, label: str) -> memoryview:
    if offset < 0 or size < 0 or offset > len(data) or size > len(data) - offset:
        raise BakeError(f"{label} is outside the MDL")
    return memoryview(data)[offset:offset + size]


def _c_string(value: bytes) -> str:
    return value.split(b"\0", 1)[0].decode("latin-1")


def _hash64(data: bytes) -> int:
    value = 14695981039346656037
    for item in data:
        value ^= item
        value = (value * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return value


def _align(value: int, alignment: int = 16) -> int:
    return (value + alignment - 1) & -alignment


def _read_i32(data: bytes, offset: int, label: str) -> int:
    _range(data, offset, 4, label)
    return struct.unpack_from("<i", data, offset)[0]


def _header(data: bytes) -> dict[str, int | str]:
    _range(data, 0, 244, "studio header")
    if data[:4] != b"IDST" or _read_i32(data, 4, "version") != 10:
        raise BakeError("input is not a GoldSrc Studio v10 model")
    declared = _read_i32(data, 72, "declared length")
    if declared != len(data):
        raise BakeError("studio header length does not match the file")
    values = struct.unpack_from("<27i", data, 136)
    names = (
        "flags", "bone_count", "bone_offset", "controller_count",
        "controller_offset", "hitbox_count", "hitbox_offset",
        "sequence_count", "sequence_offset", "sequence_group_count",
        "sequence_group_offset", "texture_count", "texture_offset",
        "texture_data_offset", "skinref_count", "skin_family_count",
        "skin_offset", "bodypart_count", "bodypart_offset",
        "attachment_count", "attachment_offset", "sound_table",
        "sound_offset", "sound_group_count", "sound_group_offset",
        "transition_count", "transition_offset",
    )
    result: dict[str, int | str] = dict(zip(names, values))
    result["name"] = _c_string(data[8:72])
    if not 1 <= int(result["bone_count"]) <= MAX_BONES:
        raise BakeError("unsupported bone count")
    if int(result["texture_count"]) <= 0:
        raise BakeError("model has no embedded textures")
    if int(result["bodypart_count"]) <= 0:
        raise BakeError("model has no bodyparts")
    return result


def _sequences(data: bytes, header: dict[str, int | str]) -> list[dict[str, object]]:
    count = int(header["sequence_count"])
    offset = int(header["sequence_offset"])
    _range(data, offset, count * SEQUENCE.size, "sequence table")
    result = []
    for index in range(count):
        row = SEQUENCE.unpack_from(data, offset + index * SEQUENCE.size)
        # Classic v10 sequence descriptors are 176 bytes.  Only fields used by
        # this baker are named here; the remaining values stay format-checked.
        result.append({
            "index": index,
            "name": _c_string(row[0]),
            "fps": row[1],
            "flags": row[2],
            "frames": row[7],
            "motion_type": row[10],
            "motion_bone": row[11],
            "blends": row[23],
            "anim_offset": row[24],
            "group": row[32],
        })
    return result


def _select_sequence(sequences: list[dict[str, object]], name: str | None
                     ) -> dict[str, object]:
    if name:
        matches = [item for item in sequences if item["name"] == name]
        if len(matches) != 1:
            raise BakeError(f"sequence {name!r} was not found exactly once")
        selected = matches[0]
    else:
        candidates = [item for item in sequences
                      if int(item["group"]) == 0 and int(item["frames"]) > 1]
        if not candidates:
            raise BakeError("model has no embedded animated sequence")
        selected = candidates[0]
    if int(selected["group"]) != 0:
        raise BakeError("selected sequence uses an external sequence group")
    if not 1 < int(selected["frames"]) <= MAX_FRAMES:
        raise BakeError("selected sequence has an unsupported frame count")
    if int(selected["blends"]) <= 0:
        raise BakeError("selected sequence has no animation blend")
    if not math.isfinite(float(selected["fps"])) or float(selected["fps"]) <= 0:
        raise BakeError("selected sequence has invalid fps")
    return selected


def _animation_value(data: bytes, anim: int, relative: int, frame: int) -> int:
    if relative == 0:
        return 0
    cursor = anim + relative
    remaining = frame
    for _ in range(MAX_FRAMES + 1):
        span = _range(data, cursor, 2, "animation span")
        valid, total = span[0], span[1]
        if total == 0 or valid > total:
            raise BakeError("invalid compressed animation span")
        if remaining < total:
            if valid == 0:
                raise BakeError("animation span has no retained value")
            sample = min(remaining, valid - 1)
            value_at = cursor + 2 + sample * 2
            _range(data, value_at, 2, "animation value")
            return struct.unpack_from("<h", data, value_at)[0]
        remaining -= total
        cursor += 2 + valid * 2
    raise BakeError("animation span chain does not terminate")


def _quaternion(angles: tuple[float, float, float]) -> tuple[float, float, float, float]:
    sr, cr = math.sin(angles[0] * 0.5), math.cos(angles[0] * 0.5)
    sp, cp = math.sin(angles[1] * 0.5), math.cos(angles[1] * 0.5)
    sy, cy = math.sin(angles[2] * 0.5), math.cos(angles[2] * 0.5)
    return (
        sr * cp * cy - cr * sp * sy,
        cr * sp * cy + sr * cp * sy,
        cr * cp * sy - sr * sp * cy,
        cr * cp * cy + sr * sp * sy,
    )


def _bones_and_poses(data: bytes, header: dict[str, int | str],
                     sequence: dict[str, object]) -> tuple[bytes, bytes]:
    count = int(header["bone_count"])
    bone_offset = int(header["bone_offset"])
    _range(data, bone_offset, count * BONE.size, "bone table")
    bones = [BONE.unpack_from(data, bone_offset + i * BONE.size)
             for i in range(count)]
    parents = bytearray()
    for index, bone in enumerate(bones):
        parent = bone[1]
        if parent < -1 or parent >= index:
            raise BakeError("bones are not in parent-before-child order")
        parents += struct.pack("<h", parent)
    anim_offset = int(sequence["anim_offset"])
    _range(data, anim_offset, count * 12, "animation table")
    poses = bytearray()
    motion_type = int(sequence["motion_type"])
    motion_bone = int(sequence["motion_bone"])
    for frame in range(int(sequence["frames"])):
        for index, bone in enumerate(bones):
            anim = anim_offset + index * 12
            offsets = struct.unpack_from("<6H", data, anim)
            defaults = bone[9:15]
            scales = bone[15:21]
            values = [defaults[channel] +
                      _animation_value(data, anim, offsets[channel], frame) *
                      scales[channel] for channel in range(6)]
            if index == motion_bone:
                for axis, flag in enumerate((1, 2, 4)):
                    if motion_type & flag:
                        values[axis] = 0.0
            if not all(math.isfinite(value) for value in values):
                raise BakeError("animation produced a non-finite pose")
            poses += POSE.pack(values[0], values[1], values[2],
                               *_quaternion(tuple(values[3:6])))
    return bytes(parents), bytes(poses)


def _textures(data: bytes, header: dict[str, int | str]
              ) -> tuple[list[tuple[int, int, int, int, int, int, int]], bytes]:
    count = int(header["texture_count"])
    offset = int(header["texture_offset"])
    _range(data, offset, count * TEXTURE.size, "texture table")
    metadata = []
    pixels = bytearray()
    for index in range(count):
        name, flags, _unused, width, height, source_offset = TEXTURE.unpack_from(
            data, offset + index * TEXTURE.size)
        if not 0 < width <= MAX_TEXTURE_DIMENSION or \
                not 0 < height <= MAX_TEXTURE_DIMENSION:
            raise BakeError("texture dimensions exceed the runtime contract")
        pixel_count = width * height
        indices = _range(data, source_offset, pixel_count, "texture indices")
        palette = _range(data, source_offset + pixel_count, 256 * 3,
                         "texture palette")
        row_pitch = _align(width * 4, 256)
        destination = _align(len(pixels), 256)
        pixels += bytes(destination - len(pixels))
        rgba = bytearray(row_pitch * height)
        for y in range(height):
            for x in range(width):
                palette_index = indices[y * width + x]
                source = palette_index * 3
                target = y * row_pitch + x * 4
                rgba[target:target + 3] = palette[source:source + 3]
                rgba[target + 3] = 0 if flags & STUDIO_NF_MASKED and \
                    palette_index == 255 else 255
        pixels += rgba
        metadata.append((destination, len(rgba), width, height, row_pitch,
                         flags, _hash64(bytes(name).split(b"\0", 1)[0])))
    return metadata, bytes(pixels)


def _geometry(data: bytes, header: dict[str, int | str]
              ) -> tuple[list[SourceVertex], list[int], list[DrawInfo],
                         tuple[float, float, float], tuple[float, float, float]]:
    skin_count = int(header["skinref_count"]) * int(header["skin_family_count"])
    if skin_count <= 0:
        raise BakeError("model has no skin family")
    skin_offset = int(header["skin_offset"])
    _range(data, skin_offset, skin_count * 2, "skin table")
    skin = struct.unpack_from(f"<{skin_count}h", data, skin_offset)
    body_offset = int(header["bodypart_offset"])
    _range(data, body_offset, BODYPART.size, "first bodypart")
    _name, model_count, _base, model_offset = BODYPART.unpack_from(data, body_offset)
    if model_count <= 0:
        raise BakeError("first bodypart has no model")
    _range(data, model_offset, MODEL.size, "first submodel")
    model = MODEL.unpack_from(data, model_offset)
    mesh_count, mesh_offset = model[3], model[4]
    vertex_count, vertex_bones_offset, positions_offset = model[5:8]
    normal_count, normal_bones_offset, normals_offset = model[8:11]
    if vertex_count <= 0 or normal_count <= 0 or mesh_count <= 0:
        raise BakeError("first submodel has no renderable geometry")
    _range(data, vertex_bones_offset, vertex_count, "vertex bone table")
    _range(data, normal_bones_offset, normal_count, "normal bone table")
    _range(data, positions_offset, vertex_count * 12, "vertex positions")
    _range(data, normals_offset, normal_count * 12, "vertex normals")
    _range(data, mesh_offset, mesh_count * MESH.size, "mesh table")
    positions = [struct.unpack_from("<3f", data, positions_offset + i * 12)
                 for i in range(vertex_count)]
    normals = [struct.unpack_from("<3f", data, normals_offset + i * 12)
               for i in range(normal_count)]
    vertices: list[SourceVertex] = []
    indices: list[int] = []
    draws: list[DrawInfo] = []
    texture_count = int(header["texture_count"])
    bone_count = int(header["bone_count"])
    for mesh_index in range(mesh_count):
        tri_count, cursor, skinref, _normal_count, _normal_index = MESH.unpack_from(
            data, mesh_offset + mesh_index * MESH.size)
        if skinref < 0 or skinref >= int(header["skinref_count"]):
            raise BakeError("mesh skin reference is invalid")
        texture = skin[skinref]
        if texture < 0 or texture >= texture_count:
            raise BakeError("mesh texture reference is invalid")
        texture_row = TEXTURE.unpack_from(
            data, int(header["texture_offset"]) + texture * TEXTURE.size)
        width, height = texture_row[3], texture_row[4]
        first_index = len(indices)
        emitted_triangles = 0
        while True:
            _range(data, cursor, 2, "triangle command")
            command = struct.unpack_from("<h", data, cursor)[0]
            cursor += 2
            if command == 0:
                break
            count = abs(command)
            if count < 3:
                raise BakeError("triangle command has fewer than three vertices")
            _range(data, cursor, count * 8, "triangle command vertices")
            command_vertices = []
            for item in range(count):
                vertex, normal, s, t = struct.unpack_from("<4h", data,
                                                           cursor + item * 8)
                if not 0 <= vertex < vertex_count or not 0 <= normal < normal_count:
                    raise BakeError("triangle command references invalid geometry")
                bone = data[vertex_bones_offset + vertex]
                normal_bone = data[normal_bones_offset + normal]
                if bone >= bone_count or normal_bone >= bone_count:
                    raise BakeError("geometry references an invalid bone")
                command_vertices.append(len(vertices))
                vertices.append(SourceVertex(
                    positions[vertex], normals[normal],
                    (s / width, t / height), bone, normal_bone))
            cursor += count * 8
            for item in range(2, count):
                if command > 0:
                    triangle = ((item - 2, item - 1, item) if item % 2 == 0
                                else (item - 1, item - 2, item))
                else:
                    triangle = (0, item - 1, item)
                indices.extend(command_vertices[value] for value in triangle)
                emitted_triangles += 1
        if emitted_triangles != tri_count:
            raise BakeError("mesh triangle count does not match its commands")
        draws.append(DrawInfo(first_index, len(indices) - first_index,
                              texture, texture_row[1]))
    if not vertices or len(vertices) > MAX_VERTICES or len(indices) > MAX_VERTICES:
        raise BakeError("expanded geometry exceeds the 16-bit runtime contract")
    flat = [component for vertex in positions for component in vertex]
    if not all(math.isfinite(value) for value in flat):
        raise BakeError("model contains non-finite positions")
    minimum = tuple(min(value[axis] for value in positions) for axis in range(3))
    maximum = tuple(max(value[axis] for value in positions) for axis in range(3))
    return vertices, indices, draws, minimum, maximum


def bake(data: bytes, sequence_name: str | None = None) -> bytes:
    header = _header(data)
    sequence = _select_sequence(_sequences(data, header), sequence_name)
    parents, poses = _bones_and_poses(data, header, sequence)
    vertices, indices, draws, minimum, maximum = _geometry(data, header)
    textures, texture_pixels = _textures(data, header)

    sections: list[tuple[str, bytes, int]] = [
        ("parents", parents, 2),
        ("poses", poses, 16),
        ("vertices", b"".join(SOURCE_VERTEX.pack(
            *vertex.position, *vertex.normal, *vertex.uv,
            vertex.bone, vertex.normal_bone) for vertex in vertices), 16),
        ("indices", struct.pack(f"<{len(indices)}H", *indices), 2),
        ("draws", b"".join(DRAW.pack(draw.first_index, draw.index_count,
                                     draw.texture, draw.flags)
                           for draw in draws), 16),
        ("textures", b"".join(OUTPUT_TEXTURE.pack(*texture)
                              for texture in textures), 16),
        ("pixels", texture_pixels, 256),
    ]
    output = bytearray(HEADER.size)
    offsets = []
    for _name, payload, alignment in sections:
        offset = _align(len(output), alignment)
        output += bytes(offset - len(output))
        offsets.append(offset)
        output += payload
    payload_crc = zlib.crc32(output[HEADER.size:]) & 0xFFFFFFFF
    flags = STUDIO_LOOPING if int(sequence["flags"]) & STUDIO_LOOPING else 0
    packed = HEADER.pack(
        MAGIC, VERSION, HEADER.size, len(output), payload_crc,
        _hash64(data), _hash64(str(header["name"]).encode("latin-1")),
        _hash64(str(sequence["name"]).encode("latin-1")),
        int(header["bone_count"]), int(sequence["frames"]),
        float(sequence["fps"]), flags, len(vertices), len(indices),
        len(draws), len(textures), *minimum, *maximum, *offsets,
        len(texture_pixels), 0, 0, 0, 0, 0, 0)
    output[:HEADER.size] = packed
    return bytes(output)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--sequence")
    args = parser.parse_args()
    try:
        result = bake(args.input.read_bytes(), args.sequence)
    except (OSError, BakeError) as exc:
        raise SystemExit(f"studio bake failed: {exc}") from exc
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(result)
    header = HEADER.unpack_from(result)
    print(f"studio bundle: bytes={len(result)} bones={header[8]} "
          f"frames={header[9]} fps={header[10]:.3f} "
          f"vertices={header[12]} indices={header[13]} "
          f"draws={header[14]} textures={header[15]} "
          f"source_fnv64={header[5]:016x}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
