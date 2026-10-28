#!/usr/bin/env python3
"""Compare two PNGs written by our renderers (8-bit RGB, filter 0) and
report the differences, bucketed per 16x16 map cell.

Both renderers emit plain PNGs with one filter byte (0) per row, so a
minimal decoder is enough.
"""

import struct
import sys
import zlib


def read_png(path):
    data = open(path, "rb").read()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", path
    pos = 8
    w = h = None
    idat = b""
    while pos < len(data):
        (n,) = struct.unpack(">I", data[pos:pos + 4])
        typ = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if typ == b"IHDR":
            w, h, depth, ctype = struct.unpack(">IIBB", body[:10])
            assert depth == 8 and ctype == 2, (depth, ctype)
        elif typ == b"IDAT":
            idat += body
        elif typ == b"IEND":
            break
    raw = zlib.decompress(idat)
    stride = w * 3
    rgb = bytearray(w * h * 3)
    for y in range(h):
        row = raw[y * (stride + 1):(y + 1) * (stride + 1)]
        assert row[0] == 0, "unexpected filter byte"
        rgb[y * stride:(y + 1) * stride] = row[1:]
    return w, h, bytes(rgb)


def main():
    a_path, b_path = sys.argv[1], sys.argv[2]
    w1, h1, a = read_png(a_path)
    w2, h2, b = read_png(b_path)
    if (w1, h1) != (w2, h2):
        print("size mismatch: %dx%d vs %dx%d" % (w1, h1, w2, h2))
        return
    w, h = w1, h1
    diffs = {}
    total = 0
    for y in range(h):
        ra = a[y * w * 3:(y + 1) * w * 3]
        rb = b[y * w * 3:(y + 1) * w * 3]
        if ra == rb:
            continue
        for x in range(w):
            k = x * 3
            if ra[k:k + 3] != rb[k:k + 3]:
                total += 1
                cell = (x // 16, y // 16)
                diffs[cell] = diffs.get(cell, 0) + 1
    print("%s vs %s: %d differing pixels in %d cells" % (a_path, b_path, total, len(diffs)))
    for (cx, cy), n in sorted(diffs.items()):
        print("  cell col=%2d row=%2d  (%4d,%4d px)  %d px differ" % (cx, cy, cx * 16, cy * 16, n))


if __name__ == "__main__":
    main()
