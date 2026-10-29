#!/usr/bin/env python3
"""Generate the transition overlay asset from the real loader.

The overlay records exactly which cell/object bytes the destination loader
writes, plus the small number of RNG/global-dependent writes that must be
replayed at runtime.  Transition loading preserves every byte not in the
overlay, matching the original loader's source-memory overlay behaviour.
"""
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

import emulate_load as E  # noqa: E402

CELL_LO = E.CELL_BASE
CELL_HI = CELL_LO + 40 * E.CELL_COL_STRIDE + 25 * E.CELL_ROW_STRIDE + 20
OBJ_LO = E.OBJ_BASE
OBJ_HI = OBJ_LO + E.OBJ_SLOTS * E.OBJ_STRIDE
CELL_CELLS = 40 * 25
TAG_LITERAL = 0
TAG_RNG = 1
TAG_GLOBAL = 2
TAG_MAP = {}


def load_tags():
    path = os.path.join(ROOT, "notes", "loader_tags.json")
    if os.path.exists(path):
        return json.load(open(path))
    return {}


def rng_step(lo, hi):
    ax = lo
    bx = hi
    cx = (ax << 3) & 0xFFFF
    prod = ax * 0x8405
    dx = (prod >> 16) & 0xFFFF
    ax = prod & 0xFFFF
    ch = ((cx >> 8) + (cx & 0xFF)) & 0xFF
    cx = ((ch << 8) | (cx & 0xFF)) & 0xFFFF
    dx = (dx + cx) & 0xFFFF
    dx = (dx + bx) & 0xFFFF
    bx = (bx << 2) & 0xFFFF
    dx = (dx + bx) & 0xFFFF
    dh = ((dx >> 8) + (bx & 0xFF)) & 0xFF
    dx = ((dh << 8) | (dx & 0xFF)) & 0xFFFF
    bx = (bx << 5) & 0xFFFF
    dh = ((dx >> 8) + (bx & 0xFF)) & 0xFF
    dx = ((dh << 8) | (dx & 0xFF)) & 0xFFFF
    ax = (ax + 1) & 0xFFFF
    if ax == 0:
        dx = (dx + 1) & 0xFFFF
    return ax, dx, dx


def rng_results(lo, hi, ns):
    out = []
    for n in ns:
        lo, hi, val = rng_step(lo, hi)
        out.append(val % n if n else 0)
    return out


def logical_cell(off):
    delta = off - CELL_LO
    # The original cell addressing is column-major with stride 500 bytes and
    # 20 bytes per row.  Because 25 rows * 20 bytes == 500, the last logical
    # row of one column aliases the top border of the next; resolve the address
    # against the logical 40x25 grid explicitly.
    for col in range(40):
        rem = delta - E.CELL_COL_STRIDE * (col + 1)
        if rem < 0:
            break
        row1, byte = divmod(rem, E.CELL_ROW_STRIDE)
        if 1 <= row1 <= 25:
            return (row1 - 1) * 40 + col, byte
    return None


def last_writes(records):
    """Collapse ordered write records to the final value of each byte."""
    final = {}
    meta = {}
    for rec in records:
        off, size, val, rc, retip, args = rec
        for i in range(size):
            final[off + i] = (val >> (8 * i)) & 0xFF
            meta[off + i] = (rc, retip, args)
    return final, meta


def build_level(dest, source_level=2, source_ds=None, source_rng=None):
    r = E.run_transition(source_level, dest, prefill=False, trace_writes=True)
    if r["stats"]["stopped"] not in ("ret", "load_end"):
        raise RuntimeError("level %d transition stopped: %s" %
                           (dest, r["stats"]["stopped"]))
    finals, meta = last_writes(r["writes"])
    ns = r["stats"]["rng_calls"]
    raw = rng_results(source_rng[0], source_rng[1], ns)

    cell_literal = {}
    cell_tags = []
    obj_literal = {}
    obj_tags = []
    add_for_draw = {}
    tag_by_off = {}
    for ent in TAG_MAP.get(str(dest), []):
        if ent[1] == "g":
            goff = int(ent[2], 16) if isinstance(ent[2], str) else ent[2]
            tag_by_off[ent[0]] = ("g", goff, ent[3])
        else:
            tag_by_off[ent[0]] = ("rng", ent[2], ent[3], ent[4])

    for abs_off, value in finals.items():
        rc, retip, args = meta[abs_off]
        tag = tag_by_off.get(abs_off)
        if OBJ_LO <= abs_off < OBJ_HI:
            slot = (abs_off - OBJ_LO) // E.OBJ_STRIDE
            boff = (abs_off - OBJ_LO) % E.OBJ_STRIDE
            if tag is not None and tag[0] == "rng":
                di, n, add = tag[1], tag[2], tag[3]
                add_for_draw[di] = add
                obj_tags.append((slot, boff, TAG_RNG, di))
                continue
            if tag is not None and tag[0] == "g":
                obj_tags.append((slot, boff, TAG_GLOBAL, tag[1]))
                continue
            # Every 0x68AC spawn p18 is RNG-selected.  Tag any that the
            # two-source diff happened to miss (coincident values).
            if retip == 0x68AC and boff == 0x18 and rc > 0 and rc - 1 < len(ns):
                di = rc - 1
                add_for_draw[di] = (value - raw[di]) & 0xFF
                obj_tags.append((slot, boff, TAG_RNG, di))
                continue
            obj_literal[(slot, boff)] = value
        else:
            loc = logical_cell(abs_off)
            if loc is None:
                continue
            cell_index, boff = loc
            if tag is not None and tag[0] == "g":
                cell_tags.append((cell_index, boff, TAG_GLOBAL, tag[1]))
            elif tag is not None and tag[0] == "rng":
                di, n, add = tag[1], tag[2], tag[3]
                add_for_draw[di] = add
                cell_tags.append((cell_index, boff, TAG_RNG, di))
            else:
                cell_literal[(cell_index, boff)] = value

    # Global writes: keep the last value for each (offset, size).
    gw = {}
    for off, size, value in r["stats"]["global_writes"]:
        if off == 0x02A6 or off == 0x02A8:
            continue  # live RNG is replayed through the draw list
        gw[(off, size)] = value
    globals_out = [(off, size, value) for (off, size), value in
                   sorted(gw.items(), key=lambda kv: (kv[0][0], kv[0][1]))]

    draws = []
    for i, n in enumerate(ns):
        draws.append((n, add_for_draw.get(i, 0)))
    return {
        "level": dest,
        "cell_literal": cell_literal,
        "cell_tags": cell_tags,
        "obj_literal": obj_literal,
        "obj_tags": obj_tags,
        "globals": globals_out,
        "draws": draws,
        "cursor": r["cursor"],
    }


def encode(levels):
    level_count = 19
    header_len = 8 + 4 * level_count
    offsets = []
    body = bytearray()
    for lv in range(level_count):
        offsets.append(header_len + len(body))
        rec = levels.get(lv)
        if rec is None:
            body += struct.pack("<HH", 0, 0)  # cell tags / obj tags placeholder
            # Empty record: cell bitset + no values, no tags, no objs, no globals,
            # no draws.
            body = body[:-4]
            body += bytes(2500) + struct.pack("<HHHHH", 0, 0, 0, 0, 0)
            continue
        # Cells: bitset over 1000*20 byte slots, literal values in bit order.
        bits = bytearray(2500)
        values = bytearray()
        for bit in range(CELL_CELLS * 20):
            cell_index, boff = divmod(bit, 20)
            if (cell_index, boff) in rec["cell_literal"]:
                bits[bit >> 3] |= 1 << (bit & 7)
                values.append(rec["cell_literal"][(cell_index, boff)])
        body += bits + values
        body += struct.pack("<H", len(rec["cell_tags"]))
        for cell_index, boff, tag, payload in rec["cell_tags"]:
            body += struct.pack("<HBH", cell_index * 20 + boff, tag, payload)
        body += struct.pack("<H", len(rec["obj_literal"]))
        for (slot, boff), value in sorted(rec["obj_literal"].items()):
            body += struct.pack("<BBB", slot, boff, value)
        body += struct.pack("<H", len(rec["obj_tags"]))
        for slot, boff, tag, payload in rec["obj_tags"]:
            body += struct.pack("<BBBH", slot, boff, tag, payload)
        body += struct.pack("<H", len(rec["globals"]))
        for off, size, value in rec["globals"]:
            body += struct.pack("<HBH", off, size, value)
        body += struct.pack("<H", len(rec["draws"]))
        for n, add in rec["draws"]:
            body += struct.pack("<HH", n, add)
    out = bytearray(b"CCLO")
    out += struct.pack("<HH", 1, level_count)
    out += b"".join(struct.pack("<I", o) for o in offsets)
    out += body
    return bytes(out)


def main():
    global TAG_MAP
    TAG_MAP = load_tags()
    source_level = 2
    module = E.load_module()
    src = E.make_machine(module, True)
    E.run_loader(src, module, E.level_arg(source_level))
    source_ds = bytes(src.mem_read(E.DATA_SEG << 4, 0x10000))
    source_rng = (int.from_bytes(source_ds[0x2A6:0x2A8], "little"),
                  int.from_bytes(source_ds[0x2A8:0x2AA], "little"))
    levels = {}
    for dest in range(3, 19):
        print("building overlay for level", dest, flush=True)
        levels[dest] = build_level(dest, source_level, source_ds, source_rng)
    data = encode(levels)
    out = os.path.join(ROOT, "out", "portable", "level_overlay.bin")
    with open(out, "wb") as f:
        f.write(data)
    print("wrote", out, len(data), "bytes")


if __name__ == "__main__":
    main()
