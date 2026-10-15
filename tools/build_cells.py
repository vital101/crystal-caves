#!/usr/bin/env python3
"""Build the DS:0x508C map cell array from the decoded command tables.

For each level: read its 25 destination rows (= blob rows) from
notes/level_table.json, walk each of the 40 columns, apply the regular
(command table cs:0xFBF) or special (command table cs:0x24DC) handler,
writing DS:0x508C cells via the SetCell slot map.  Autotile shape deltas
are applied per [0x511E] mode.  Outputs an ASCII block/shape dump and a
JSON cell map for the GFX renderer.
"""
import json
import os
import re

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NOTE = os.path.join(HERE, "notes")

AE = lambda *a: int(a[0], a[1])

# --- per-level prologue constants (hex addr -> int), from setup_level -------
# index == game level number (0-based); level 0 prologue @load 0xB3AD.
PROL = {
0:  {'30b':0x0,'a0c8':0x14,'a0c2':0xf,'a0e4':0x14,'a0ca':0xf,'a0ba':0x28,'50dc':4,'50de':7,'50e0':0xe,'2b8':7,'511e':1,'5122':0x2d,'2f4':6,'30d':1,'a126':1,'a0cc':1},
1:  {'2b8':7,'511e':1,'5122':0x2d,'2f4':6,'30d':1,'a0cc':1},
2:  {'2b8':5,'511e':1,'5122':0x28,'2ce':0xa,'2cc':1,'2d6':0x14,'2d4':4,'2f4':6,'30d':1},
3:  {'511e':4,'2b8':1,'5122':0x15,'30d':0,'2fa':0x27},
4:  {'511e':4,'2b8':1,'5122':0x15,'30d':0,'2fa':0x27},
5:  {'511e':8,'2b8':0x13,'5122':9,'2ce':0x16,'2cc':0x21,'2d4':7,'2d8':1,'50dc':1,'50de':2,'50e0':0xa,'2fc':0x28,'2fa':0x27,'2fe':0x26},
6:  {'511e':4,'2b8':0x13,'5122':0x19,'2ce':0x16,'2cc':0x15,'2d4':4,'2d8':1,'2fa':0x28,'2fc':0x27,'2fe':0x26},
7:  {'511e':4,'2b8':0xb,'5122':0x1f,'a0c8':0xa,'2ce':0x14,'2cc':0x15,'2d4':4,'2d8':3,'50dc':1,'50de':2,'50e0':0xa},
8:  {'511e':4,'2b8':0x13,'5122':0x23,'2ce':0x16,'2cc':0x15,'2d8':1,'2fa':0x27},
9:  {'511e':4,'2b8':0x10,'5122':3,'2ce':0x16,'2cc':0x15,'2d4':7,'2d8':2,'30b':1,'2fa':0x28,'2fc':0x27,'2fe':0x26},
10: {'511e':6,'2b8':0x10,'5122':0x19,'5132':1,'2ce':0x16,'2cc':0x15,'2d4':4,'2d8':3},
11: {'511e':4,'2b8':0xb,'5122':0x1d,'2ce':0x14,'2cc':0x15,'2d4':0xa,'2d8':2},
12: {'2ce':0x17,'2cc':0xd,'2d2':0xd,'2d0':5,'2d4':0xa,'2d8':3,'2fa':0x28,'2fc':0x27,'2fe':0x26,'511e':4,'2b8':0x13,'5122':0x29},
13: {'511e':6,'2b8':0x10,'5122':0x29,'2ce':0x16,'2cc':0x21,'2d4':4,'2d8':1},
14: {'511e':8,'2b8':0x13,'5122':0x11,'2ce':0x16,'2cc':0xd,'2d8':1,'2fa':0x28,'2fc':0x27,'2fe':0x26},
15: {'511e':4,'2b8':0xb,'5122':9,'2ce':0x17,'2cc':0xd,'2d4':0xa,'2d8':3,'2fa':0x28,'2fc':0x27,'2fe':0x26},
16: {'2ce':0x17,'2cc':0xd,'2d2':0xd,'2d4':0xa,'2d8':3,'511e':8,'2b8':0x10,'5122':0x21,'2fa':0x27},
17: {'511e':4,'2b8':0x10,'5122':1,'2da':7,'2ce':0x16,'2cc':0xd,'2d2':0xd,'2d0':5,'2d8':2},
18: {'511e':8,'2b8':0xb,'5122':1,'2da':2,'2ce':0x14,'2cc':0x15,'2d4':4,'2d8':3,'2fc':0x28,'2fa':0x27,'2fe':0x26},
19: {'511e':4,'2b8':0x13,'5122':0x1b,'2ce':0x10,'2cc':9,'2d8':2,'2fa':0x28,'2fc':0x27,'2fe':0x26,'50dc':1,'50de':4,'50e0':0xa},
20: {'511e':4,'2b8':0x13,'5122':0x21,'2fa':0x28,'2fc':0x27,'2fe':0x26},
}

BLOB_FILE = 0x8CE0
ROW_LEN = 41


def load_tables():
    reg = json.load(open(os.path.join(NOTE, "regular_cmds.json")))
    spc = json.load(open(os.path.join(NOTE, "special_cmds.json")))
    return reg, spc


def autotile_delta(mode, row, col):
    d = 0
    if mode == 4:
        d += 4 * (row & 1) + 1 * (col & 1)
    elif mode == 6:
        d += 4 * (row & 1)
        d += 2 if col % 3 == 0 else (1 if col % 3 == 2 else 0)
    elif mode == 8:
        d += 4 * (row & 1)
        d += 3 if col % 4 == 0 else (2 if col % 4 == 3 else (1 if col % 4 == 2 else 0))
    return d


def ev(expr, C, row, col, table):
    """Evaluate an extracted SetCell argument to an int."""
    if isinstance(expr, int):
        return expr
    s = expr.replace("word ptr ", "").strip()
    # table-specific frame variables
    if table == "reg":
        s = s.replace("[bp + 4]", str(row)).replace("[bp + 6]", str(col))
    else:
        s = s.replace("[bp + 8]", str(row)).replace("[bp - 0x102]", str(col))
    # any remaining frame variable (e.g. animation frame [bp-0x104]) -> 0
    s = re.sub(r"\[bp[^\]]*\]", "0", s)
    # memory-const terms [0xADDR]
    def repl(m):
        return str(C.get(m.group(1).lstrip("0") or "0", 0))
    s = re.sub(r"\[0x([0-9a-fA-F]+)\]", repl, s)
    if set(s) <= set("0123456789abcdefABCDEFxX+- "):
        try:
            return int(eval(s))  # noqa: S307 - digits/hex/+- only
        except Exception:
            pass
    raise ValueError("cannot eval %r -> %r" % (expr, s))


def build_cells(level):
    reg, spc = load_tables()
    C = dict(PROL.get(level, {}))
    C.setdefault("5120", 4)
    C.setdefault("50dc", 4)
    C.setdefault("50de", 7)
    C.setdefault("50e0", 0xe)
    mode = C.get("511e", 1)
    lt = json.load(open(os.path.join(NOTE, "level_table.json")))
    blob = open(os.path.join(HERE, "extracted", "CC1-UNLZ.EXE"), "rb").read()
    L = lt["levels"][level]
    base = C.get("5122", 0)
    cells = {}          # (row,col) -> [blkA,sA,blkB,sB,blkC,sC] (insertion order preserved)
    unknown = {}
    for rw in L["rows"]:
        dest = rw["dest"]
        src = BLOB_FILE + rw["blob_row"] * ROW_LEN
        line = blob[src + 1: src + 1 + 40]
        for col in range(40):
            ch = line[col]
            key = chr(ch)
            if key in reg:
                cmd = reg[key]; table = "reg"
            elif key in spc:
                cmd = spc[key]; table = "spc"
            else:
                unknown.setdefault(key, []).append((dest, col))
                continue
            for kind, args in cmd:
                if kind != "SetCell":
                    continue
                a = [ev(p, C, dest, col, table) for p in args]
                blkB, blkA, blkC, y, x, sB, sA, sC = a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]
                # autotile only when shapeB == [0x5122] (no frame offset added)
                if sB == base and mode in (4, 6, 8):
                    sB = base + autotile_delta(mode, x, y)
                cells[(x, y)] = [blkA, sA, blkB, sB, blkC, sC]
    return cells, unknown


def ascii_dump(cells, level):
    print("=== level %d  cells=%d ===" % (level, len(cells)))
    ys = sorted({y for (_, y) in cells})
    print("col range", ys[0], "..", ys[-1])
    for r in range(25):
        line = []
        for c in range(40):
            cell = cells.get((r, c))
            if not cell:
                line.append("....")
                continue
            blkA, sA, blkB, sB, blkC, sC = cell
            s = "%x/%x" % (blkB & 0xff, sB & 0xff)
            if sA:
                s += "+%x/%x" % (blkA & 0xff, sA & 0xff)
            if sC:
                s += "*%x/%x" % (blkC & 0xff, sC & 0xff)
            line.append(s)
        print("%2d %s" % (r, " ".join(line)))


if __name__ == "__main__":
    import sys
    lvl = int(sys.argv[1]) if len(sys.argv) > 1 else 0
    cells, unknown = build_cells(lvl)
    ascii_dump(cells, lvl)
    print("unknown chars:", {repr(k): len(v) for k, v in unknown.items()})
    json.dump({"level": lvl, "cells": {"%d,%d" % k: v for k, v in cells.items()}},
              open(os.path.join(NOTE, "cellmap_L%02d.json" % lvl), "w"))
