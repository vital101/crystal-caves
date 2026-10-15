#!/usr/bin/env python3
"""Export a portable, engine-ready intermediate of the Crystal Caves assets.

Produces out/portable/:
  manifest.json   format metadata (sizes, counts, palette model)
  palette.json    32 RGB entries (5-plane EGA + bright half)
  gfx.bin         raw CC1.GFX (23 blocks x 8003 B; 1-based block/shape)
  mni_f1.bin      50 glyphs x 40 B (8x8 5-plane), chars 23..72
  mni_f2.bin      50 glyphs x 40 B, chars 73..122
  levels.json     baked per-level tile maps (40x25 cells of [bB,sB,bA,sA,bC,sC])
  commands.json   decoded regular + special command tables (for faithful replay)
  objects.json    object-record template + index (from setup_level init)

All multi-byte integers are little-endian to match the DOS original.
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_cells  # noqa: E402
import render_png   # noqa: E402

OUT = os.path.join(HERE, "out", "portable")
EXTRACT = os.path.join(HERE, "extracted")
N = lambda name: os.path.join(HERE, "notes", name)
W, H = 40, 25


def main():
    os.makedirs(OUT, exist_ok=True)

    # palette
    json.dump({"colors": [list(c) for c in render_png.palette32()],
               "model": "5-bitplane (bit i = plane i); low 16 = EGA, high 16 = bright"},
              open(os.path.join(OUT, "palette.json"), "w"), indent=0)

    # gfx
    gfx = open(os.path.join(EXTRACT, "CC1.GFX"), "rb").read()
    open(os.path.join(OUT, "gfx.bin"), "wb").write(gfx)

    # fonts
    for src, dst in (("CC1-F1.MNI", "mni_f1.bin"), ("CC1-F2.MNI", "mni_f2.bin")):
        raw = open(os.path.join(EXTRACT, src), "rb").read()
        open(os.path.join(OUT, dst), "wb").write(raw[3:])  # strip 3-byte header

    # levels: bake tile maps
    levels = []
    for lvl in range(19):
        cells, unknown = build_cells.build_cells(lvl)
        grid = []
        for r in range(H):
            row = []
            for c in range(W):
                row.append(list(cells.get((r, c), [0, 0, 0, 0, 0, 0])))
            grid.append(row)
        levels.append({"index": lvl, "width": W, "height": H,
                       "cells": grid,
                       "unknown_chars": sorted(unknown)})
    json.dump(levels, open(os.path.join(OUT, "levels.json"), "w"))

    # compact binary level maps for the C/WASM side:
    # per level: H*W cells x 6 bytes [bB,sB,bA,sA,bC,sC]
    binmaps = bytearray()
    for L in levels:
        for row in L["cells"]:
            for cell in row:
                binmaps += bytes(v & 0xFF for v in cell)
    open(os.path.join(OUT, "levels.bin"), "wb").write(binmaps)

    # command tables (faithful replay)
    json.dump({"regular": json.load(open(N("regular_cmds.json"))),
               "special": json.load(open(N("special_cmds.json")))},
              open(os.path.join(OUT, "commands.json"), "w"))

    # object system (see notes/objects.md)
    template = {
        "record_base": 0x45DE,
        "record_size": 45,
        "record_count": 61,          # indices 4..59 plus 0 are initialised
        "spawn_cursor": 0x5120,
        "spawn_fn": 0x47D,           # far, retf 0x1C (14 word params)
        "state": {"FIELD": 0x00, "free": 0x63, "active": 2},
        "fields": {
            "+0x00": "state", "+0x02": "flag", "+0x04": "tile_ref",
            "+0x06": "x", "+0x08": "y", "+0x0A": "x_prev", "+0x0C": "y_prev",
            "+0x0E": "block", "+0x1C": "shape",
            "+0x29": "handler_off", "+0x2B": "handler_seg",
        },
        "init_defaults": {"+0x00": 2, "+0x06": 0xFFD7, "+0x08": 0, "+0x0A": 0xFFD7,
                          "+0x0C": 0, "+0x10": 1, "+0x1C": 1, "+0x1E": 1,
                          "+0x20": 0, "+0x22": 2, "+0x28": 1},
        "handler_segment": 0x1F9A,
        "handler_table": [
            0x1FB9, 0x1FC2, 0x2176, 0x2850, 0x2877, 0x298A, 0x2ACF, 0x2BD4,
            0x2D37, 0x2E25, 0x2F83, 0x323C, 0x32F6, 0x3416, 0x36B0, 0x3904,
            0x3A2E, 0x3BE8, 0x3D23, 0x3E70, 0x3F13, 0x3F6C, 0x403A, 0x4145,
            0x47B9, 0x48FE, 0x4A4B, 0x4A82, 0x4AD3, 0x4C92, 0x4FD4, 0x52E7,
            0x55EC, 0x5855, 0x5BBE, 0x5C8F, 0x6232, 0x62F2, 0x6458, 0x6681,
            0x672B, 0x69D3, 0x6AFA, 0x6D31, 0x6FF6, 0x70E4,
        ],
        "note": "handler behaviour + update loop traced in Phase C3",
    }
    json.dump(template, open(os.path.join(OUT, "objects.json"), "w"), indent=2)

    # manifest
    json.dump({
        "game": "Crystal Caves v1.0a shareware (Episode 1)",
        "gfx": {"file": "gfx.bin", "block_size": 8003, "block_count": 23,
                "shapes_per_block": 50, "shape_size": 160, "shape_w": 16,
                "shape_h": 16, "planes": 5, "index_base": 1},
        "fonts": {"f1": "mni_f1.bin", "f2": "mni_f2.bin", "glyph_size": 40,
                  "glyph_w": 8, "glyph_h": 8, "planes": 5,
                  "f1_range": [23, 72], "f2_range": [73, 122]},
        "levels": {"file": "levels.json", "bin": "levels.bin", "count": 19,
                   "width": W, "height": H, "bin_cell_bytes": 6,
                   "cell": "[blockA,shapeA,blockB,shapeB,blockC,shapeC]"},
        "palette": {"file": "palette.json"},
        "commands": {"file": "commands.json"},
        "tile_space": "640x400 (40x25 cells of 16x16)",
    }, open(os.path.join(OUT, "manifest.json"), "w"), indent=2)

    print("exported ->", OUT)
    for f in sorted(os.listdir(OUT)):
        print("  %-14s %d bytes" % (f, os.path.getsize(os.path.join(OUT, f))))


if __name__ == "__main__":
    main()
