#!/usr/bin/env python3
"""Level cell maps for the renderers.

The original version of this module re-interpreted the level command tables
by hand.  That interpreter had several wrong assumptions (prologue indexing
shift, (x,y)/(row,col) transposition, wrong SetCell argument mapping), and it
has been superseded: tools/emulate_load.py runs the *original* loader in
Unicorn and dumps the resulting DS:0x508C cell array into
notes/oracle_objects.json.  Those dumps are the cell source of truth.

Cell layout (confirmed against SetCell CS:0x59 and the draw helper CS:0x0000,
and validated bit-exactly by tools/emulate_render.py):

    20-byte cell = 10 little-endian words w0..w9
      layer1 = (block w4, shape w2)   drawn first,  opaque blitter 0x29C5
      layer2 = (block w5, shape w3)   drawn second, transparent 0x39B, if w3 > 0
      layer3 = (block w6, shape w7)   drawn third,  transparent 0x39B, if w7 > 0

build_cells() returns {(row, col): [b1, s1, b2, s2, b3, s3]}.

Block 0 never has shape data: the two-wide-object commands (0xFFD/0xFFF) pass
block [0x2d6], which the binary sets exactly once (level 2's prologue, value
20) and which persists across level loads.  A fresh boot would leave it 0 and
make the game read shape data through table[0] = 0:0 (undefined behaviour).
Sequential play always yields 20, so block 0 is substituted with 20 here
(BLOCK0_SUB); pass block0_sub=0 to keep the raw values.
"""
import json
import os
import re
import struct

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NOTE = os.path.join(HERE, "notes")
ORACLE = os.path.join(NOTE, "oracle_objects.json")

BLOCK0_SUB = 20

AE = lambda *a: int(a[0], a[1])

# --- per-level prologue constants (hex addr -> int), from setup_level -------
# Index == game level number (0-based); level 0 prologue @load 0xB3AD, level 1
# @0xB50B, level 2 @0xB664, level 3 @0xB803; levels 2..18 take arg (level-2).
#
# Verified against the oracle cells: the dominant layer1 (block, shape) of
# every level 2..18 equals this table's [0x2b8]/[0x5122] (the original table
# was indexed by loader argument, i.e. off by two for levels >= 2, and its
# level-2 entry mixed in superseded writes).  notes/oracle_objects.json stays
# the authoritative source for everything downstream.
PROL = {
0:  {'30b':0x0,'a0c8':0x14,'a0c2':0xf,'a0e4':0x14,'a0ca':0xf,'a0ba':0x28,'50dc':4,'50de':7,'50e0':0xe,'2b8':7,'511e':1,'5122':0x2d,'2f4':6,'30d':1,'a126':1,'a0cc':1},
1:  {'2b8':7,'511e':1,'5122':0x2d,'2f4':6,'30d':1,'a0cc':1},
2:  {'2b8':1,'511e':4,'5122':0x15,'2ce':0xa,'2cc':1,'2d6':0x14,'2d4':4,'30d':0,'2fa':0x27},
3:  {'511e':8,'2b8':0x13,'5122':9,'2ce':0x16,'2cc':0x21,'2d4':7,'2d8':1,'50dc':1,'50de':2,'50e0':0xa,'2fc':0x28,'2fa':0x27,'2fe':0x26},
4:  {'511e':4,'2b8':0x13,'5122':0x19,'2ce':0x16,'2cc':0x15,'2d4':4,'2d8':1,'2fa':0x28,'2fc':0x27,'2fe':0x26},
5:  {'511e':4,'2b8':0xb,'5122':0x1f,'a0c8':0xa,'2ce':0x14,'2cc':0x15,'2d4':4,'2d8':3,'50dc':1,'50de':2,'50e0':0xa},
6:  {'511e':4,'2b8':0x13,'5122':0x23,'2ce':0x16,'2cc':0x15,'2d8':1,'2fa':0x27},
7:  {'511e':4,'2b8':0x10,'5122':3,'2ce':0x16,'2cc':0x15,'2d4':7,'2d8':2,'30b':1,'2fa':0x28,'2fc':0x27,'2fe':0x26},
8:  {'511e':6,'2b8':0x10,'5122':0x19,'5132':1,'2ce':0x16,'2cc':0x15,'2d4':4,'2d8':3},
9:  {'511e':4,'2b8':0xb,'5122':0x1d,'2ce':0x14,'2cc':0x15,'2d4':0xa,'2d8':2},
10: {'2ce':0x17,'2cc':0xd,'2d2':0xd,'2d0':5,'2d4':0xa,'2d8':3,'2fa':0x28,'2fc':0x27,'2fe':0x26,'511e':4,'2b8':0x13,'5122':0x29},
11: {'511e':6,'2b8':0x10,'5122':0x29,'2ce':0x16,'2cc':0x21,'2d4':4,'2d8':1},
12: {'511e':8,'2b8':0x13,'5122':0x11,'2ce':0x16,'2cc':0xd,'2d8':1,'2fa':0x28,'2fc':0x27,'2fe':0x26},
13: {'511e':4,'2b8':0xb,'5122':9,'2ce':0x17,'2cc':0xd,'2d4':0xa,'2d8':3,'2fa':0x28,'2fc':0x27,'2fe':0x26},
14: {'2ce':0x17,'2cc':0xd,'2d2':0xd,'2d4':0xa,'2d8':3,'511e':8,'2b8':0x10,'5122':0x21,'2fa':0x27},
15: {'511e':4,'2b8':0x10,'5122':1,'2da':7,'2ce':0x16,'2cc':0xd,'2d2':0xd,'2d0':5,'2d8':2},
16: {'511e':8,'2b8':0xb,'5122':1,'2da':2,'2ce':0x14,'2cc':0x15,'2d4':4,'2d8':3,'2fc':0x28,'2fa':0x27,'2fe':0x26},
17: {'511e':4,'2b8':0x13,'5122':0x1b,'2ce':0x10,'2cc':9,'2d8':2,'2fa':0x28,'2fc':0x27,'2fe':0x26,'50dc':1,'50de':4,'50e0':0xa},
18: {'511e':4,'2b8':0x13,'5122':0x21,'2fa':0x28,'2fc':0x27,'2fe':0x26},
}

BLOB_FILE = 0x8CE0
ROW_LEN = 41


def load_tables():
    reg = json.load(open(os.path.join(NOTE, "regular_cmds.json")))
    spc = json.load(open(os.path.join(NOTE, "special_cmds.json")))
    return reg, spc


def ev(expr, C, row, col, table):
    """Evaluate an extracted SetCell argument to an int."""
    if isinstance(expr, int):
        return expr
    s = expr
    # strip known prefixes / markers
    s = s.replace("mem:", "").replace("word ptr ", "").replace("?", "")
    # table-specific frame variables
    s = s.replace("[bp + 4]", str(row)).replace("[bp + 6]", str(col))
    s = s.replace("[bp + 8]", str(row)).replace("[bp - 0x102]", str(col))
    # any remaining frame variable (e.g. animation frame [bp-0x104]) -> 0
    s = re.sub(r"\[bp[^\]]*\]", "0", s)
    # literal column / row coordinates used by the special command table
    s = s.replace("col", str(col)).replace("row", str(row))
    # memory-const terms [0xADDR]
    def repl(m):
        return str(C.get(m.group(1).lstrip("0") or "0", 0))
    s = re.sub(r"\[0x([0-9a-fA-F]+)\]", repl, s)
    # 'flag' local variable
    s = s.replace("flag", str(C.get("flag", 0)))
    if set(s) <= set("0123456789abcdefABCDEFxX+- "):
        try:
            return int(eval(s))  # noqa: S307 - digits/hex/+- only
        except Exception:
            pass
    raise ValueError("cannot eval %r -> %r" % (expr, s))


def build_cells(level, block0_sub=BLOCK0_SUB):
    """Load one level's cell map from the golden-oracle emulator dump.

    Returns {(row, col): [b1, s1, b2, s2, b3, s3]} for cells with any
    non-zero layer, with block 0 replaced by block0_sub (see module docstring).
    """
    oracle = json.load(open(ORACLE))
    grid = oracle[str(level)]["cells"]
    cells = {}
    for r, row in enumerate(grid):
        for c, hexcell in enumerate(row):
            w = struct.unpack("<10H", bytes.fromhex(hexcell))
            cell = [w[4], w[2], w[5], w[3], w[6], w[7]]  # [b1,s1,b2,s2,b3,s3]
            if block0_sub:
                for i in (0, 2, 4):
                    if cell[i] == 0 and cell[i + 1]:
                        cell[i] = block0_sub
            if any(cell):
                cells[(r, c)] = cell
    return cells


def ascii_dump(cells, level):
    print("=== level %d  cells=%d ===" % (level, len(cells)))
    for r in range(25):
        line = []
        for c in range(40):
            cell = cells.get((r, c))
            if not cell:
                line.append("....")
                continue
            b1, s1, b2, s2, b3, s3 = cell
            s = "%x/%x" % (b1 & 0xff, s1 & 0xff)
            if s2:
                s += "+%x/%x" % (b2 & 0xff, s2 & 0xff)
            if s3:
                s += "*%x/%x" % (b3 & 0xff, s3 & 0xff)
            line.append(s)
        print("%2d %s" % (r, " ".join(line)))


if __name__ == "__main__":
    import sys
    lvl = int(sys.argv[1]) if len(sys.argv) > 1 else 0
    ascii_dump(build_cells(lvl), lvl)
