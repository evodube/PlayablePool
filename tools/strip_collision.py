#!/usr/bin/env python3
"""Makes collision-free copies of Fallout 4 meshes for fo4pool's balls.

    python tools/strip_collision.py <input.nif> <output.nif>

The vanilla pool balls are Havok physics objects. fo4pool moves the balls itself, so its copies must not have
physics: this detaches the root node's collision object and clears the Havok bits in BSXFlags. Everything else
(geometry, textures, materials) is untouched, and the file stays the same size.

Bethesda's meshes aren't in this repository; run this on the ones extracted from your own game.
"""

import struct
import sys

BSX_HAVOK_BITS = 0x2 | 0x4 | 0x8 | 0x40 | 0x80  # havok, ragdoll, complex, dynamic, articulated


def read_header(data: bytes):
    p = data.index(b"\n") + 1
    p += 4 + 1 + 4  # version, endian, user version
    nblocks = struct.unpack_from("<I", data, p)[0]
    p += 4
    bsver = struct.unpack_from("<I", data, p)[0]
    p += 4

    def sstr():
        nonlocal p
        n = data[p]
        p += 1 + n

    sstr()  # author
    if bsver > 130:
        p += 4
    sstr()  # process script
    sstr()  # export script
    if bsver >= 103:
        sstr()  # max filepath
    ntypes = struct.unpack_from("<H", data, p)[0]
    p += 2
    types = []
    for _ in range(ntypes):
        n = struct.unpack_from("<I", data, p)[0]
        types.append(data[p + 4:p + 4 + n].decode("latin1"))
        p += 4 + n
    tindex = list(struct.unpack_from(f"<{nblocks}H", data, p))
    p += 2 * nblocks
    sizes = list(struct.unpack_from(f"<{nblocks}I", data, p))
    p += 4 * nblocks
    nstrings = struct.unpack_from("<I", data, p)[0]
    p += 8
    for _ in range(nstrings):
        n = struct.unpack_from("<I", data, p)[0]
        p += 4 + n
    ngroups = struct.unpack_from("<I", data, p)[0]
    p += 4 + 4 * ngroups

    blocks = []
    for i in range(nblocks):
        blocks.append((types[tindex[i] & 0x7FFF], p))
        p += sizes[i]
    return blocks


def strip(data: bytes) -> tuple[bytes, list[str]]:
    out = bytearray(data)
    blocks = read_header(data)
    done = []

    root_type, root = blocks[0]
    if not root_type.endswith("Node"):
        raise ValueError(f"root block is {root_type}, expected a node")
    # NiAVObject: name, extra data list, controller, flags, translation, rotation, scale, collision
    nextra = struct.unpack_from("<I", data, root + 4)[0]
    coll = root + 4 + 4 + 4 * nextra + 4 + 4 + 12 + 36 + 4
    if struct.unpack_from("<i", data, coll)[0] >= 0:
        struct.pack_into("<i", out, coll, -1)
        done.append("collision detached")

    for t, off in blocks:
        if t == "BSXFlags":
            value = struct.unpack_from("<I", data, off + 4)[0]
            new = value & ~BSX_HAVOK_BITS
            if new != value:
                struct.pack_into("<I", out, off + 4, new)
                done.append(f"BSXFlags 0x{value:X} -> 0x{new:X}")
    return bytes(out), done


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 1
    data = open(sys.argv[1], "rb").read()
    out, done = strip(data)
    with open(sys.argv[2], "wb") as f:
        f.write(out)
    print(f"{sys.argv[2]}: {', '.join(done) if done else 'nothing to change'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
