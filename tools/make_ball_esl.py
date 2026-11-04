#!/usr/bin/env python3
"""Writes fo4pool.esl: a light plugin with one static object (STAT) used as the pool ball.

    python tools/make_ball_esl.py "Path\\To\\Mesh.nif" [out.esl]

The mesh path is relative to Data\\Meshes, exactly as it would appear in the Creation Kit. The record gets
form ID xx000800, which is what fo4pool.ini expects by default (sPlugin=fo4pool.esl, iFormID=0x800).

Open the result in xEdit once to check it before relying on it.
"""

import struct
import sys

RECORD_VERSION = 131  # Fallout 4
FLAG_MASTER = 0x1
FLAG_LIGHT = 0x200


def subrecord(sig: bytes, data: bytes) -> bytes:
    if len(data) > 0xFFFF:
        raise ValueError(f"{sig!r} too long")
    return sig + struct.pack("<H", len(data)) + data


def zstring(text: str) -> bytes:
    return text.encode("cp1252") + b"\0"


def record(sig: bytes, flags: int, form_id: int, body: bytes) -> bytes:
    # type, data size, flags, form id, version control info, form version, unknown
    return sig + struct.pack("<IIIIHH", len(body), flags, form_id, 0, RECORD_VERSION, 0) + body


def group(label: bytes, contents: bytes) -> bytes:
    # type, group size (header included), label, group type 0 (top), timestamp, vc info, unknown
    return b"GRUP" + struct.pack("<I", 24 + len(contents)) + label + struct.pack("<iHHI", 0, 0, 0, 0) + contents


def build(mesh: str) -> bytes:
    mesh = mesh.replace("/", "\\")
    if mesh.lower().startswith("meshes\\"):
        mesh = mesh[len("meshes\\"):]

    ball_id = 0x01000800  # index 01: our own records come after the one master
    stat = record(
        b"STAT",
        0,
        ball_id,
        subrecord(b"EDID", zstring("fo4poolBall"))
        + subrecord(b"OBND", struct.pack("<6h", -3, -3, -3, 3, 3, 3))
        + subrecord(b"MODL", zstring(mesh))
        # max angle, material (none), leaf amplitude, leaf frequency
        + subrecord(b"DNAM", struct.pack("<fIff", 90.0, 0, 1.0, 1.0)),
    )

    header = record(
        b"TES4",
        FLAG_MASTER | FLAG_LIGHT,
        0,
        subrecord(b"HEDR", struct.pack("<fiI", 1.0, 1, 0x801))
        + subrecord(b"CNAM", zstring("evodube"))
        + subrecord(b"SNAM", zstring("fo4pool test ball"))
        + subrecord(b"MAST", zstring("Fallout4.esm"))
        + subrecord(b"DATA", struct.pack("<Q", 0)),
    )
    return header + group(b"STAT", stat)


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    out = sys.argv[2] if len(sys.argv) > 2 else "fo4pool.esl"
    data = build(sys.argv[1])
    with open(out, "wb") as f:
        f.write(data)
    print(f"wrote {out} ({len(data)} bytes), ball mesh Meshes\\{sys.argv[1]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
