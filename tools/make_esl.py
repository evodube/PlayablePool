#!/usr/bin/env python3
"""Writes fo4pool.esl: a light plugin with the 16 ball statics.

    python tools/make_esl.py [out.esl] [--stick]

Form IDs (what fo4pool.ini expects by default):
    xx000800        cue ball   Meshes\\fo4pool\\Ball_Cue.nif
    xx000801..80F   balls 1-15 Meshes\\fo4pool\\Ball_01.nif .. Ball_15.nif
    xx000810        cue stick  Meshes\\fo4pool\\CueStick.nif   (only with --stick)
    xx000811        idle fo4poolAim     Meshes\\Actors\\Character\\Animations\\fo4pool\\PoolAim.hkx
    xx000812        idle fo4poolStrike  Meshes\\Actors\\Character\\Animations\\fo4pool\\PoolStrike.hkx
    xx000813        idle fo4poolWait    Meshes\\Actors\\Character\\Animations\\fo4pool\\PoolWait.hkx
    xx000814        keyword fo4poolLinkPoolSpot  (the opponent's linked ref: where they're to walk)
    xx000815        package fo4poolWalkToSpot    (Travel to that linked ref, walking, facing the marker at the end)
    xx000816        quest fo4poolOpponent        (priority 100, start game enabled; alias 0 "Opponent" carries the
                                                 package, so it outranks settler sandboxes from other mods)

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


def keyword(form_id: int, editor_id: str) -> bytes:
    # colour (white), type 0 (none): as vanilla link keywords (RELinkTravel)
    return record(
        b"KYWD", 0, form_id,
        subrecord(b"EDID", zstring(editor_id)) + subrecord(b"CNAM", bytes.fromhex("ffffff00")) + subrecord(b"TNAM", struct.pack("<I", 0)),
    )


TRAVEL_TEMPLATE = 0x00002CB0  # Fallout4.esm "Travel"


def travel_package(form_id: int, editor_id: str, link_keyword: int, radius: int = 10) -> bytes:
    """A Travel package to the actor's linked ref (with link_keyword), like vanilla
    VRWorkshopShared_WorkshopSummonedByBellPackage: same template, data inputs and event blocks."""
    no_event = subrecord(b"INAM", struct.pack("<I", 0)) + subrecord(b"PDTO", bytes(8))
    body = (
        subrecord(b"EDID", zstring(editor_id))
        # general flags (0x2000: use the preferred speed), type 18 (package), interrupt override 0, preferred speed
        # 0 (walk), unknown 0x4D, interrupt flags 0xFD, unknown: as the vanilla bell package, but walking
        + subrecord(b"PKDT", struct.pack("<IBBBBHH", 0x00002000, 18, 0, 0, 0x4D, 0x00FD, 0))
        # schedule: any time
        + subrecord(b"PSDT", bytes.fromhex("00ff00ffff000000") + struct.pack("<i", 0))
        # data inputs: 4, from the Travel template, version 1
        + subrecord(b"PKCU", struct.pack("<III", 4, TRAVEL_TEMPLATE, 1))
        # Place to Travel: near linked reference (type 6) with the keyword, radius
        + subrecord(b"ANAM", zstring("Location")) + subrecord(b"PLDT", struct.pack("<IIiI", 6, link_keyword, radius, 0))
        + subrecord(b"ANAM", zstring("Bool")) + subrecord(b"CNAM", b"\x00")  # Prefer Preferred Path?
        + subrecord(b"ANAM", zstring("Bool")) + subrecord(b"CNAM", b"\x00")  # Use "soft radius"?
        + subrecord(b"ANAM", zstring("Bool")) + subrecord(b"CNAM", b"\x00")  # Ignore marker orientation? (no: face it)
        + subrecord(b"UNAM", b"\x01") + subrecord(b"UNAM", b"\x03") + subrecord(b"UNAM", b"\x05") + subrecord(b"UNAM", b"\x07")
        + subrecord(b"XNAM", b"\x08")
        + subrecord(b"POBA", b"") + no_event
        + subrecord(b"POEA", b"") + no_event
        + subrecord(b"POCA", b"") + no_event
    )
    return record(b"PACK", 0, form_id, body)


def wstring(text: str) -> bytes:
    data = text.encode("cp1252")
    return struct.pack("<H", len(data)) + data


def script_entry(name: str, properties: list[bytes]) -> bytes:
    # name, flags 0 (local), property count, properties
    return wstring(name) + b"\x00" + struct.pack("<H", len(properties)) + b"".join(properties)


def object_property(name: str, form_id: int, alias: int = -1) -> bytes:
    # type 1 (object), flags 1 (edited), object v2: unused, alias id (-1 none), form id
    return wstring(name) + bytes([1, 1]) + struct.pack("<HhI", 0, alias, form_id)


def quest_vmad(quest_id: int, quest_script: str, alias_property: str, alias_script: str) -> bytes:
    """Scripts on the quest and on its alias 0, laid out as Fallout4.esm's DialogueGlowingSeaAtom: version 6, object
    format 2; the quest script; the fragments block with no fragments (03, count 0, an empty fragment script name);
    then the aliases."""
    return (
        struct.pack("<HHH", 6, 2, 1)
        + script_entry(quest_script, [object_property(alias_property, quest_id, 0)])
        + b"\x03" + struct.pack("<H", 0) + wstring("")
        + struct.pack("<H", 1)
        + struct.pack("<HhI", 0, 0, quest_id)  # alias 0 of this quest
        + struct.pack("<HHH", 6, 2, 1)
        + script_entry(alias_script, [])
    )


def quest(form_id: int, editor_id: str, alias_name: str, package: int, priority: int = 100) -> bytes:
    """A start-game-enabled quest with one optional reference alias that carries `package`. Alias packages go by
    quest priority: 100 is the highest the Creation Kit allows. Scripts: fo4poolOpponentQuest (Join / Leave, its
    Opponent property is the alias) and fo4poolOpponentAlias on the alias (papyrus/)."""
    body = (
        subrecord(b"EDID", zstring(editor_id))
        + subrecord(b"VMAD", quest_vmad(form_id, "fo4poolOpponentQuest", "Opponent", "fo4poolOpponentAlias"))
        # flags (0x10 start game enabled, 0x1 as saved by the CK for those), priority, unknown, delay 0, type 0 (none)
        + subrecord(b"DNAM", struct.pack("<HBBfI", 0x0011, priority, 0, 0.0, 0))
        + subrecord(b"NEXT", b"")
        + subrecord(b"ANAM", struct.pack("<I", 1))  # next alias id
        + subrecord(b"ALST", struct.pack("<I", 0))
        + subrecord(b"ALID", zstring(alias_name))
        # optional, allow reuse in quest, allow reserved (settlers are often reserved by workshop quests)
        + subrecord(b"FNAM", struct.pack("<I", 0x0000020A))
        + subrecord(b"ALPC", struct.pack("<I", package))
        + subrecord(b"VTCK", struct.pack("<I", 0))
        + subrecord(b"ALED", b"")
    )
    return record(b"QUST", 0, form_id, body)


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
        idle(own | (FIRST_ID + 19), "fo4poolWait", ANIM_DIR + "PoolWait.hkx"),
    ]

    link = own | (FIRST_ID + 20)
    walk = own | (FIRST_ID + 21)
    keywords = [keyword(link, "fo4poolLinkPoolSpot")]
    packages = [travel_package(walk, "fo4poolWalkToSpot", link)]
    quests = [quest(own | (FIRST_ID + 22), "fo4poolOpponent", "Opponent", walk)]

    count = len(stats) + len(idles) + len(keywords) + len(packages) + len(quests)
    header = record(
        b"TES4",
        FLAG_MASTER | FLAG_LIGHT,
        0,
        subrecord(b"HEDR", struct.pack("<fiI", 1.0, count, FIRST_ID + 23))
        + subrecord(b"CNAM", zstring("evodube"))
        + subrecord(b"SNAM", zstring("fo4pool: pool balls"))
        + subrecord(b"MAST", zstring("Fallout4.esm"))
        + subrecord(b"DATA", struct.pack("<Q", 0)),
    )
    # groups in the order the game's own master has them
    return (
        header
        + group(b"KYWD", b"".join(keywords))
        + group(b"STAT", b"".join(stats))
        + group(b"QUST", b"".join(quests))
        + group(b"IDLE", b"".join(idles))
        + group(b"PACK", b"".join(packages))
    )


def main() -> int:
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    with_stick = "--stick" in sys.argv[1:]
    out = args[0] if args else "fo4pool.esl"
    data = build(with_stick)
    with open(out, "wb") as f:
        f.write(data)
    print(f"wrote {out} ({len(data)} bytes, {17 if with_stick else 16} statics, 3 idles, a keyword, a package and a quest)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
