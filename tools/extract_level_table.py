"""Recover the per-level map-row table from the unpacked CC1.EXE.

Level heights are not stored as data: they are encoded as unrolled calls to
LoadMapRow (seg000:25f0) emitted into setup_level's body.

Instruction shape (Turbo Pascal pascal convention, args pushed left->right):
    <mov ax, dest | xor ax,ax>  then push ax (50)
    mov di, <src_addr>          (bf xx xx)
    push cs ; push di           (0e 57)
    call 0x25f0                 (e8 xx xx)

A new level begins whenever the dest row resets backwards (dest <= last dest).

Usage: python3 extract_level_table.py [out.json]
"""

import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
EXE = os.path.join(HERE, "..", "extracted", "CC1-UNLZ.EXE")
HDR = 0x1E00
BLOB = 0x6EE0
ROW = 41
NBLOB = 417
LOAD_MAP_ROW = 0x25F0


def scan():
    data = open(EXE, "rb").read()[HDR:]
    hits = []
    n = len(data)
    i = 0
    while i < n - 8:
        if data[i] == 0xBF and data[i + 3] == 0x0E and data[i + 4] == 0x57 \
                and data[i + 5] == 0xE8:
            src = data[i + 1] | (data[i + 2] << 8)
            e8 = i + 5
            rel = data[e8 + 1] | (data[e8 + 2] << 8)
            if rel >= 0x8000:
                rel -= 0x10000
            if ((e8 + 3 + rel) & 0xFFFF) == LOAD_MAP_ROW:
                dest = None
                if data[i - 1] == 0x50:                       # push ax
                    if data[i - 4] == 0xB8:                   # mov ax, imm16
                        dest = data[i - 3] | (data[i - 2] << 8)
                    elif data[i - 3] in (0x31, 0x33) and data[i - 2] == 0xC0:
                        dest = 0                              # xor ax,ax
                hits.append({"load": e8 + 3, "dest": dest, "src": src})
            i = e8 + 3
            continue
        i += 1
    return hits


def src_row(src):
    if not (BLOB <= src < BLOB + NBLOB * ROW):
        return None
    row, rem = divmod(src - BLOB, ROW)
    return row if rem == 0 else None


def split_levels(hits):
    levels = []
    last_dest = None
    for h in hits:
        d = h["dest"]
        prev = levels[-1][-1] if levels else None
        gap = h["load"] - prev["load"] if prev else 0
        new = (prev is None
               or (d is not None and last_dest is not None and d <= last_dest)
               or (d is None and gap > 40 and last_dest is not None))
        if new:
            levels.append([])
            last_dest = None
        levels[-1].append(h)
        if d is not None:
            last_dest = d
    return levels


def main():
    hits = scan()
    print("LoadMapRow (seg000:%04x) call sites: %d" % (LOAD_MAP_ROW, len(hits)))

    levels = split_levels(hits)
    print("levels detected (dest-row resets): %d\n" % len(levels))
    print("  idx  file        calls  distinct | src row range")
    print("  " + "-" * 66)

    out = []
    bad = 0
    unknown_dest = 0
    for idx, lv in enumerate(levels):
        rows = [src_row(c["src"]) for c in lv]
        bad += sum(1 for r in rows if r is None)
        unknown_dest += sum(1 for c in lv if c["dest"] is None)
        uniq = sorted({r for r in rows if r is not None})
        contiguous = bool(uniq) and uniq == list(range(uniq[0], uniq[-1] + 1))
        dests = [c["dest"] for c in lv]
        span = ("%d..%d" % (uniq[0], uniq[-1])) if uniq else "?"
        print("  %4d  %#09x  %5d  %7d | %s%s"
              % (idx, lv[0]["load"] + HDR, len(lv), len(uniq), span,
                 "" if contiguous else "  (reused across levels)"))
        out.append({
            "index": idx, "file": lv[0]["load"] + HDR, "load": lv[0]["load"],
            "height": len(lv),
            "dests": dests,
            "rows": [{"dest": c["dest"], "blob_row": src_row(c["src"]),
                      "src": c["src"]} for c in lv],
            "distinct_blob_rows": uniq,
        })

    print("\ntotal calls %d | distinct blob rows used %d (blob has %d)"
          % (len(hits), len({src_row(h["src"]) for h in hits}), NBLOB))
    print("heights   : %s" % [o["height"] for o in out])
    print("sum(height): %d  (vs %d blob rows -> %d reused row loads)"
          % (sum(o["height"] for o in out), NBLOB,
             sum(o["height"] for o in out) - NBLOB))
    print("unknown dest: %d | src outside blob/misaligned: %d"
          % (unknown_dest, bad))

    path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        HERE, "..", "notes", "level_table.json")
    json.dump({"load_map_row": LOAD_MAP_ROW,
               "blob": {"file": 0x8CE0, "seg000": BLOB,
                        "rows": NBLOB, "row_len": ROW},
               "levels": out}, open(path, "w"), indent=1)
    print("wrote %s" % path)


if __name__ == "__main__":
    main()
