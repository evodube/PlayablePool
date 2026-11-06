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
                                                 package, so it outranks settler sandboxes from other mods; alias 1
                                                 "Watchers", a collection, carries fo4poolWatchTable; alias 2
                                                 "Opponent2", the second NPC of an NPC-vs-NPC game, the walk again)
    xx000817        keyword fo4poolLinkPoolTable (a watcher's linked ref: the table they hang round)
    xx000819        idle fo4poolAim2    Meshes\\Actors\\Character\\Animations\\fo4pool\\PoolAim2.hkx  (another aim, picked at random)
    xx00081A        idle fo4poolWait2   Meshes\\Actors\\Character\\Animations\\fo4pool\\PoolWait2.hkx (another wait, picked at random)
    xx000818        package fo4poolWatchTable    (Travel to that linked ref, facing it: a spot the plugin picks, 100 out
                                                 from the felt, turned to face the table; they stand there watching)

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


def quest_vmad(quest_id: int, quest_script: str, alias_properties: list[tuple[str, int]], alias_scripts: list[tuple[int, str]]) -> bytes:
    """Scripts on the quest and on its aliases, laid out as Fallout4.esm's DialogueGlowingSeaAtom: version 6, object
    format 2; the quest script (its properties pointing at its aliases); the fragments block with no fragments (03,
    count 0, an empty fragment script name); then the aliases and their scripts."""
    aliases = b"".join(
        struct.pack("<HhI", 0, alias_id, quest_id) + struct.pack("<HHH", 6, 2, 1) + script_entry(script, [])
        for alias_id, script in alias_scripts
    )
    return (
        struct.pack("<HHH", 6, 2, 1)
        + script_entry(quest_script, [object_property(name, quest_id, alias_id) for name, alias_id in alias_properties])
        + b"\x03" + struct.pack("<H", 0) + wstring("")
        + struct.pack("<H", len(alias_scripts))
        + aliases
    )


def sandbox_package(form_id: int, editor_id: str, link_keyword: int, radius: int) -> bytes:
    """A Sandbox package round the actor's linked ref (with link_keyword): MagicSyringesMod's working settler sandbox
    (template 2CB1, its data inputs and event blocks) without its conditions, at the given radius."""
    no_event = subrecord(b"INAM", struct.pack("<I", 0)) + subrecord(b"PDTO", bytes(8))
    def boolean(v: int) -> bytes:
        return subrecord(b"ANAM", zstring("Bool")) + subrecord(b"CNAM", bytes([v]))
    body = (
        subrecord(b"EDID", zstring(editor_id))
        + subrecord(b"PKDT", bytes.fromhex("0000000012000261fffe0000"))
        + subrecord(b"PSDT", bytes.fromhex("00ff00ffff02000000000000"))
        + subrecord(b"PKCU", struct.pack("<III", 15, 0x00002CB1, 7))
        + subrecord(b"ANAM", zstring("Location")) + subrecord(b"PLDT", struct.pack("<IIiI", 6, link_keyword, radius, 0))
        + boolean(1) + boolean(1) + boolean(0) + boolean(1) + boolean(1) + boolean(1) + boolean(1) + boolean(1) + boolean(1)
        + subrecord(b"ANAM", zstring("Float")) + subrecord(b"CNAM", bytes.fromhex("00004842"))
        + boolean(0) + boolean(0)
        + subrecord(b"ANAM", zstring("TargetSelector")) + subrecord(b"PTDA", bytes.fromhex("020000000000000000000000"))
        + subrecord(b"ANAM", zstring("Float")) + subrecord(b"CNAM", bytes.fromhex("00001643"))
        + b"".join(subrecord(b"UNAM", bytes([i])) for i in (0x02, 0x0C, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0E, 0x0A, 0x10, 0x12, 0x14, 0x16, 0x18, 0x1A))
        + subrecord(b"XNAM", b"\x15")
        + subrecord(b"POBA", b"") + no_event
        + subrecord(b"POEA", b"") + no_event
        + subrecord(b"POCA", b"") + no_event
    )
    return record(b"PACK", 0, form_id, body)


def quest(form_id: int, editor_id: str, opponent_package: int, watch_package: int, priority: int = 100) -> bytes:
    """fo4poolOpponent: start game enabled, priority 100 (the highest the Creation Kit allows: alias packages go by
    quest priority). Alias 0 'Opponent' (the NPC playing you; Travel to their linked ref), alias 1 'Watchers' (a
    collection: settlers hanging round a table; Sandbox round it). Scripts: fo4poolOpponentQuest on the quest
    (properties Opponent and Watchers), fo4poolOpponentAlias and fo4poolWatchersAlias on the aliases (papyrus/)."""
    body = (
        subrecord(b"EDID", zstring(editor_id))
        + subrecord(b"VMAD", quest_vmad(form_id, "fo4poolOpponentQuest", [("Opponent", 0), ("Watchers", 1), ("Opponent2", 2), ("Players", 3)],
                                        [(0, "fo4poolOpponentAlias"), (1, "fo4poolWatchersAlias"), (2, "fo4poolOpponentAlias"),
                                         (3, "fo4poolWatchersAlias")]))
        # flags (0x10 start game enabled, 0x1 as saved by the CK for those), priority, unknown, delay 0, type 0 (none)
        + subrecord(b"DNAM", struct.pack("<HBBfI", 0x0011, priority, 0, 0.0, 0))
        + subrecord(b"NEXT", b"")
        + subrecord(b"ANAM", struct.pack("<I", 4))  # next alias id
        + subrecord(b"ALST", struct.pack("<I", 0))
        + subrecord(b"ALID", zstring("Opponent"))
        # optional, allow reuse in quest, allow reserved (settlers are often reserved by workshop quests)
        + subrecord(b"FNAM", struct.pack("<I", 0x0000020A))
        + subrecord(b"ALPC", struct.pack("<I", opponent_package))
        + subrecord(b"VTCK", struct.pack("<I", 0))
        + subrecord(b"ALED", b"")
        # a collection alias, as vanilla WorkshopPermanentActors: ALCS id, ALMI 0 (no initial fill), then the alias
        + subrecord(b"ALCS", struct.pack("<I", 1))
        + subrecord(b"ALMI", b"\x00")
        + subrecord(b"ALST", struct.pack("<I", 1))
        + subrecord(b"ALID", zstring("Watchers"))
        + subrecord(b"FNAM", struct.pack("<I", 0x0000020A))
        + subrecord(b"ALPC", struct.pack("<I", watch_package))
        + subrecord(b"VTCK", struct.pack("<I", 0))
        + subrecord(b"ALED", b"")
        # alias 2 'Opponent2': the second NPC when two NPCs play each other; the same walk package
        + subrecord(b"ALST", struct.pack("<I", 2))
        + subrecord(b"ALID", zstring("Opponent2"))
        + subrecord(b"FNAM", struct.pack("<I", 0x0000020A))
        + subrecord(b"ALPC", struct.pack("<I", opponent_package))
        + subrecord(b"VTCK", struct.pack("<I", 0))
        + subrecord(b"ALED", b"")
        # alias 3 'Players': a collection, every NPC playing at any table (up to four tables at once); the walk package
        + subrecord(b"ALCS", struct.pack("<I", 3))
        + subrecord(b"ALMI", b"\x00")
        + subrecord(b"ALST", struct.pack("<I", 3))
        + subrecord(b"ALID", zstring("Players"))
        + subrecord(b"FNAM", struct.pack("<I", 0x0000020A))
        + subrecord(b"ALPC", struct.pack("<I", opponent_package))
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
        idle(own | (FIRST_ID + 25), "fo4poolAim2", ANIM_DIR + "PoolAim2.hkx"),
        idle(own | (FIRST_ID + 26), "fo4poolWait2", ANIM_DIR + "PoolWait2.hkx"),
    ]

    link = own | (FIRST_ID + 20)
    walk = own | (FIRST_ID + 21)
    table_link = own | (FIRST_ID + 23)
    watch = own | (FIRST_ID + 24)
    keywords = [keyword(link, "fo4poolLinkPoolSpot"), keyword(table_link, "fo4poolLinkPoolTable")]
    packages = [travel_package(walk, "fo4poolWalkToSpot", link), travel_package(watch, "fo4poolWatchTable", table_link, 20)]
    quests = [quest(own | (FIRST_ID + 22), "fo4poolOpponent", walk, watch)]

    count = len(stats) + len(idles) + len(keywords) + len(packages) + len(quests)
    header = record(
        b"TES4",
        FLAG_MASTER | FLAG_LIGHT,
        0,
        subrecord(b"HEDR", struct.pack("<fiI", 1.0, count, FIRST_ID + 27))
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
