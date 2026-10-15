"""Render the recovered Crystal Caves levels as ASCII art (validation).

The original maps were authored as literal ASCII-art strings in the Pascal
source, so tile codes are printable characters and this renders the source art.

Usage: python3 render_levels.py [level_index...]   (default: all)
"""

import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
EXE = os.path.join(HERE, "..", "extracted", "CC1-UNLZ.EXE")
TBL = os.path.join(HERE, "..", "notes", "level_table.json")
HDR = 0x1E00
BLOB = 0x6EE0
ROW = 41
WIDTH = 40

# how many levels consume each blob row (proves cross-level sharing)
def main():
    data = open(EXE, "rb").read()[HDR:]
    tbl = json.load(open(TBL))
    levels = tbl["levels"]
    want = [int(a) for a in sys.argv[1:]] or list(range(len(levels)))

    usecount = {}
    for lv in levels:
        for r in lv["rows"]:
            usecount[r["blob_row"]] = usecount.get(r["blob_row"], 0) + 1

    print("blob rows reused across levels: %d of %d"
          % (sum(1 for v in usecount.values() if v > 1),
             tbl["blob"]["rows"]))
    print()

    for i in want:
        lv = levels[i]
        dests = [r["dest"] for r in lv["rows"]]
        assert dests == list(range(25)), "dest rows not 0..24: %s" % dests
        grid = []
        for r in lv["rows"]:
            off = BLOB + r["blob_row"] * ROW
            ln = data[off]
            assert ln == WIDTH, "row len %d at blob row %d" % (ln, r["blob_row"])
            grid.append(data[off + 1:off + 1 + WIDTH])
        reuse = sum(1 for r in lv["rows"]
                    if usecount[r["blob_row"]] > 1)
        print("=" * 46)
        print("LEVEL %d   file %#x   %d dest rows  (%d reused src rows)"
              % (i, lv["file"], lv["height"], reuse))
        print("=" * 46)
        for d, row in enumerate(grid):
            print("%2d|%s|" % (d, "".join(chr(b) if 32 <= b < 127 else "?"
                                          for b in row)))
        print()


if __name__ == "__main__":
    main()
