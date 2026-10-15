"""Crystal Caves asset reconnaissance.

Validates the documented on-disk formats against the extracted Episode 1 data:
  PCX full-screen images (.APG/.CDT/.END/.TTL)
  ProGraphx EGA tilesets (.GFX 16x16, .MNI 8x8)
  Embedded level map blob inside the unpacked CC1.EXE (Crystal Caves Map Format)

Usage: python3 cc_assets.py <extracted_dir> <unpacked_exe>
"""

import os
import struct
import sys

LEVEL_OFFSET = 0x8CE0
LEVEL_WIDTH = 40
TILE16_BYTES = 5 * 16 * 2      # 5 planes * 16 rows * 2 bytes
TILE8_BYTES = 5 * 8 * 2        # 5 planes * 8 rows  * 2 bytes


def describe_pcx(path, data):
    mfr, ver, enc, bpp = data[0], data[1], data[2], data[3]
    xmin, ymin, xmax, ymax = struct.unpack("<4H", data[4:12])
    nplanes = data[65]
    bytes_per_line = struct.unpack("<H", data[66:68])[0]
    w = xmax - xmin + 1
    h = ymax - ymin + 1
    colors = 1 << (nplanes * bpp)
    print("  %-14s PCX v%d enc=%d bpp=%d %dx%d planes=%d bpl=%d colors=%d  (%d bytes)"
          % (path, ver, enc, bpp, w, h, nplanes, bytes_per_line, colors, len(data)))


def describe_tileset(path, data, tile_bytes, kind):
    tiles = len(data) // tile_bytes
    rem = len(data) % tile_bytes
    print("  %-14s %s tileset: %d tiles of %d bytes (%d trailing bytes)"
          % (path, kind, tiles, tile_bytes, rem))
    return tiles, rem


def parse_levels(exe):
    print("level map blob at %#x:" % LEVEL_OFFSET)
    pos = LEVEL_OFFSET
    levels = []
    while pos + 1 < len(exe):
        if exe[pos] != LEVEL_WIDTH:
            break
        rows = []
        while pos < len(exe) and exe[pos] == LEVEL_WIDTH:
            row = exe[pos + 1:pos + 1 + LEVEL_WIDTH]
            if len(row) < LEVEL_WIDTH:
                break
            rows.append(row)
            pos += 1 + LEVEL_WIDTH
        levels.append(rows)
    print("  parsed %d level(s), ended at %#x" % (len(levels), pos))
    for i, rows in enumerate(levels[:6]):
        codes = sorted({b for r in rows for b in r})
        print("    level %2d: %2d rows, tile codes %#x..%#x (%d distinct)"
              % (i, len(rows), codes[0], codes[-1], len(codes)))
    if len(levels) > 6:
        print("    ... and %d more levels" % (len(levels) - 6))
    return levels


def main(extracted, exe_path):
    print("== full-screen images ==")
    for name in ("CC1.APG", "CC1.CDT", "CC1.END", "CC1.TTL"):
        p = os.path.join(extracted, name)
        if os.path.exists(p):
            describe_pcx(name, open(p, "rb").read())

    print("== tilesets ==")
    describe_tileset("CC1.GFX", open(os.path.join(extracted, "CC1.GFX"), "rb").read(), TILE16_BYTES, "16x16")
    for name in ("CC1-F1.MNI", "CC1-F2.MNI", "CC1-SPL.MNI"):
        describe_tileset(name, open(os.path.join(extracted, name), "rb").read(), TILE8_BYTES, "8x8")

    print("== levels ==")
    exe = open(exe_path, "rb").read()
    parse_levels(exe)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    main(sys.argv[1], sys.argv[2])
