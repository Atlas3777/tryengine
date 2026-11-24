#!/usr/bin/env python3
"""Regenerate game/data/meshes/unit_box.mesh (the ProBuilder box primitive).

The file is a committed MeshBinary v4 artifact: 24 vertices, 36 indices, positions in
[-0.5, 0.5] with per-face normals and 0..1 UVs. Faces are wound so the cross product of a
triangle points along its outward normal, the same convention the glTF importer produces
and the renderer's back-face culling expects.

Run from the repository root:

    python3 tools/agent/make-unit-box.py

The header layout mirrors engine_source/resources/include/engine/resources/binary/
MeshBinary.hpp. If MeshVersion changes there, update VERSION below and regenerate.
"""

import math
import struct
import sys

OUT_PATH = "game/data/meshes/unit_box.mesh"

MAGIC = 0x4853454D  # 'M','E','S','H'
VERSION = 4
ATTR_NORMAL = 1 << 0
ATTR_TEXCOORD0 = 1 << 2
INDEX_FORMAT_UINT16 = 2  # resources::IndexFormat::UInt16

# (outward normal, four corners counter-clockwise seen from outside)
FACES = [
    ((1, 0, 0), [(0.5, -0.5, -0.5), (0.5, 0.5, -0.5), (0.5, 0.5, 0.5), (0.5, -0.5, 0.5)]),
    ((-1, 0, 0), [(-0.5, -0.5, 0.5), (-0.5, 0.5, 0.5), (-0.5, 0.5, -0.5), (-0.5, -0.5, -0.5)]),
    ((0, 1, 0), [(-0.5, 0.5, -0.5), (-0.5, 0.5, 0.5), (0.5, 0.5, 0.5), (0.5, 0.5, -0.5)]),
    ((0, -1, 0), [(0.5, -0.5, -0.5), (0.5, -0.5, 0.5), (-0.5, -0.5, 0.5), (-0.5, -0.5, -0.5)]),
    ((0, 0, 1), [(-0.5, -0.5, 0.5), (0.5, -0.5, 0.5), (0.5, 0.5, 0.5), (-0.5, 0.5, 0.5)]),
    ((0, 0, -1), [(0.5, -0.5, -0.5), (-0.5, -0.5, -0.5), (-0.5, 0.5, -0.5), (0.5, 0.5, -0.5)]),
]

UVS = [(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)]


def snorm8(value):
    return max(-127, min(127, int(round(value * 127.0))))


def build():
    positions, normals, texcoords, indices = [], [], [], []
    for normal, corners in FACES:
        base = len(positions)
        for corner, uv in zip(corners, UVS):
            positions.append(corner)
            normals.append(normal)
            texcoords.append(uv)
        indices += [base, base + 1, base + 2, base, base + 2, base + 3]

    position_bytes = b"".join(struct.pack("<3f", *p) for p in positions)
    attribute_bytes = b""
    for normal, uv in zip(normals, texcoords):
        attribute_bytes += struct.pack("<4b", snorm8(normal[0]), snorm8(normal[1]), snorm8(normal[2]), 0)
        attribute_bytes += struct.pack("<2e", uv[0], uv[1])
    index_bytes = b"".join(struct.pack("<H", i) for i in indices)

    header = struct.pack(
        "<IIIIIHBB3f3ffIQ",
        MAGIC,
        VERSION,
        len(positions),
        len(indices),
        ATTR_NORMAL | ATTR_TEXCOORD0,
        8,  # normal (4 bytes) + texcoord0 (4 bytes)
        INDEX_FORMAT_UINT16,
        0,  # padding
        -0.5,
        -0.5,
        -0.5,
        0.5,
        0.5,
        0.5,
        math.sqrt(3.0) * 0.5,  # bounding sphere radius
        0,  # padding2
        0,  # default_material: ProBuilder assigns the grid material explicitly
    )
    assert len(header) == 64, len(header)
    return header + position_bytes + attribute_bytes + index_bytes


def verify(data):
    fields = struct.unpack_from("<IIIIIHBB3f3ffIQ", data, 0)
    assert fields[0] == MAGIC and fields[1] == VERSION
    vertex_count, index_count, stride = fields[2], fields[3], fields[5]
    assert vertex_count == 24 and index_count == 36
    assert len(data) == 64 + vertex_count * 12 + vertex_count * stride + index_count * 2

    positions = [struct.unpack_from("<3f", data, 64 + i * 12) for i in range(vertex_count)]
    normals = []
    for i in range(vertex_count):
        x, y, z, _ = struct.unpack_from("<4b", data, 64 + vertex_count * 12 + i * stride)
        normals.append((x / 127.0, y / 127.0, z / 127.0))

    def sub(a, b):
        return tuple(x - y for x, y in zip(a, b))

    def cross(a, b):
        return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])

    def dot(a, b):
        return sum(x * y for x, y in zip(a, b))

    for t in range(index_count // 3):
        i0, i1, i2 = struct.unpack_from("<3H", data, 64 + vertex_count * 12 + vertex_count * stride + t * 6)
        assert dot(cross(sub(positions[i1], positions[i0]), sub(positions[i2], positions[i0])), normals[i0]) > 0


def main():
    data = build()
    verify(data)
    with open(OUT_PATH, "wb") as f:
        f.write(data)
    print(f"wrote {OUT_PATH}: {len(data)} bytes")


if __name__ == "__main__":
    sys.exit(main())
