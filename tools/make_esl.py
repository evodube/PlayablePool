#!/usr/bin/env python3
"""Writes fo4pool.esl: a light plugin with the 16 ball statics.

    python tools/make_esl.py [out.esl]

Form IDs (what fo4pool.ini expects by default):
    xx000800        cue ball   Meshes\\fo4pool\\Ball_Cue.nif
    xx000801..80F   balls 1-15 Meshes\\fo4pool\\Ball_01.nif .. Ball_15.nif

The meshes are collision-free copies of the vanilla pool balls (tools/strip_collision.py).
"""

import struct
import sys

RECORD_VERSION = 131  # Fallout 4
FLAG_MASTER = 0x1
FLAG_LIGHT = 0x200
FIRST_ID = 0x800


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


def stat(form_id: int, editor_id: str, mesh: str) -> bytes:
    return record(
        b"STAT",
        0,
        form_id,
        subrecord(b"EDID", zstring(editor_id))
        + subrecord(b"OBND", struct.pack("<6h", -4, -4, -4, 4, 4, 4))
        + subrecord(b"MODL", zstring(mesh))
        # max angle, material (none), leaf amplitude, leaf frequency
        + subrecord(b"DNAM", struct.pack("<fIff", 90.0, 0, 1.0, 1.0)),
    )


def build() -> bytes:
    own = 0x01000000  # our records come after the one master
    stats = [stat(own | FIRST_ID, "fo4poolBallCue", "fo4pool\\Ball_Cue.nif")]
    for n in range(1, 16):
        stats.append(stat(own | (FIRST_ID + n), f"fo4poolBall{n:02}", f"fo4pool\\Ball_{n:02}.nif"))

    header = record(
        b"TES4",
        FLAG_MASTER | FLAG_LIGHT,
        0,
        subrecord(b"HEDR", struct.pack("<fiI", 1.0, len(stats), FIRST_ID + len(stats)))
        + subrecord(b"CNAM", zstring("evodube"))
        + subrecord(b"SNAM", zstring("fo4pool: pool balls"))
        + subrecord(b"MAST", zstring("Fallout4.esm"))
        + subrecord(b"DATA", struct.pack("<Q", 0)),
    )
    return header + group(b"STAT", b"".join(stats))


def main() -> int:
    out = sys.argv[1] if len(sys.argv) > 1 else "fo4pool.esl"
    data = build()
    with open(out, "wb") as f:
        f.write(data)
    print(f"wrote {out} ({len(data)} bytes, 16 statics)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
