#!/usr/bin/env python3
"""Writes fo4pool.esl: a light plugin with the 16 ball statics.

    python tools/make_esl.py [out.esl] [--stick]

Form IDs (what fo4pool.ini expects by default):
    xx000800        cue ball   Meshes\\fo4pool\\Ball_Cue.nif
    xx000801..80F   balls 1-15 Meshes\\fo4pool\\Ball_01.nif .. Ball_15.nif
    xx000810        cue stick  Meshes\\fo4pool\\CueStick.nif   (only with --stick)
    xx000811        idle fo4poolAim     Meshes\\Actors\\Character\\Animations\\fo4pool\\PoolAim.hkx
    xx000812        idle fo4poolStrike  Meshes\\Actors\\Character\\Animations\\fo4pool\\PoolStrike.hkx

The idles are "dynamic idles": the human behaviour graph's dyn_ActivationLoop event plays whatever HKX the idle
names, so no behaviour file is edited. The plugin plays them on the player with AIProcess::PlayIdle.

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


def stat(form_id: int, editor_id: str, mesh: str, bound: int = 4) -> bytes:
    return record(
        b"STAT",
        0,
        form_id,
        subrecord(b"EDID", zstring(editor_id))
        + subrecord(b"OBND", struct.pack("<6h", -bound, -bound, -bound, bound, bound, bound))
        + subrecord(b"MODL", zstring(mesh))
        # max angle, material (none), leaf amplitude, leaf frequency
        + subrecord(b"DNAM", struct.pack("<fIff", 90.0, 0, 1.0, 1.0)),
    )


BEHAVIOR = "Actors\\Character\\Behaviors\\RaiderRootBehavior.hkx"
ANIM_DIR = "Actors\\Character\\Animations\\fo4pool\\"


def idle(form_id: int, editor_id: str, anim_file: str, event: str = "dyn_ActivationLoop") -> bytes:
    return record(
        b"IDLE",
        0,
        form_id,
        subrecord(b"EDID", zstring(editor_id))
        + subrecord(b"DNAM", zstring(BEHAVIOR))
        + subrecord(b"ENAM", zstring(event))
        # related idles: parent, previous sibling (none: played directly, not picked from the idle tree)
        + subrecord(b"ANAM", struct.pack("<II", 0, 0))
        # looping min/max seconds, flags, animation group section, replay delay
        + subrecord(b"DATA", struct.pack("<BBBBH", 0, 0, 0, 0, 0))
        + subrecord(b"GNAM", zstring(anim_file)),
    )


def build(with_stick: bool) -> bytes:
    own = 0x01000000  # our records come after the one master
    stats = [stat(own | FIRST_ID, "fo4poolBallCue", "fo4pool\\Ball_Cue.nif")]
    for n in range(1, 16):
        stats.append(stat(own | (FIRST_ID + n), f"fo4poolBall{n:02}", f"fo4pool\\Ball_{n:02}.nif"))
    if with_stick:
        stats.append(stat(own | (FIRST_ID + 16), "fo4poolCueStick", "fo4pool\\CueStick.nif", bound=80))

    idles = [
        idle(own | (FIRST_ID + 17), "fo4poolAim", ANIM_DIR + "PoolAim.hkx"),
        idle(own | (FIRST_ID + 18), "fo4poolStrike", ANIM_DIR + "PoolStrike.hkx"),
    ]

    header = record(
        b"TES4",
        FLAG_MASTER | FLAG_LIGHT,
        0,
        subrecord(b"HEDR", struct.pack("<fiI", 1.0, len(stats) + len(idles), FIRST_ID + 19))
        + subrecord(b"CNAM", zstring("evodube"))
        + subrecord(b"SNAM", zstring("fo4pool: pool balls"))
        + subrecord(b"MAST", zstring("Fallout4.esm"))
        + subrecord(b"DATA", struct.pack("<Q", 0)),
    )
    return header + group(b"STAT", b"".join(stats)) + group(b"IDLE", b"".join(idles))


def main() -> int:
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    with_stick = "--stick" in sys.argv[1:]
    out = args[0] if args else "fo4pool.esl"
    data = build(with_stick)
    with open(out, "wb") as f:
        f.write(data)
    print(f"wrote {out} ({len(data)} bytes, {17 if with_stick else 16} statics, 2 idles)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
