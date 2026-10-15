#!/usr/bin/env python3
"""Render a Crystal Caves level to PNG from the decoded cell map + CC1.GFX.

Rebuilds DS:0x508C (via build_cells), then composites each cell's three
layers (B, A, C) using the block-wise GFX shape offsets.  Shapes are
16x16, 5 bitplanes (32 bytes/plane).  No third-party deps: PNG is written
by hand with zlib.
"""
import os
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_cells  # noqa: E402

BLK = 8003
SHAPE_BYTES = 160

EGA = [(0,0,0),(0,0,170),(0,170,0),(0,170,170),(170,0,0),(170,0,170),
       (170,85,0),(170,170,170),(85,85,85),(85,85,255),(85,255,85),
       (85,255,255),(255,85,85),(255,85,255),(255,255,85),(255,255,255)]


def palette32():
    pal = list(EGA)
    for r, g, b in EGA:
        pal.append((min(r+85,255), min(g+85,255), min(b+85,255)))
    return pal


def load_gfx():
    return open(os.path.join(HERE, "extracted", "CC1.GFX"), "rb").read()


def decode_shape(gfx, block, idx, nplanes=5, h=16, w=16):
    """block/shape are 1-based (0 = none). Return 16x16 palette indices."""
    if block < 1 or idx < 1:
        return None
    base = (block - 1) * BLK + 3 + (idx - 1) * SHAPE_BYTES
    if base + SHAPE_BYTES > len(gfx):
        return None
    d = gfx[base:base + SHAPE_BYTES]
    bytes_per_plane = (w // 8) * h
    px = [[0] * w for _ in range(h)]
    for p in range(nplanes):
        plane = d[p * bytes_per_plane:(p + 1) * bytes_per_plane]
        if len(plane) < bytes_per_plane:
            break
        for y in range(h):
            row = plane[y * (w // 8):(y + 1) * (w // 8)]
            for bx, byte in enumerate(row):
                for bit in range(8):
                    if byte & (0x80 >> bit):
                        px[y][bx * 8 + bit] |= (1 << p)
    return px


def write_png(path, w, h, rgb):
    def chunk(typ, data):
        c = struct.pack(">I", len(data)) + typ + data
        return c + struct.pack(">I", zlib.crc32(typ + data) & 0xffffffff)
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        raw += rgb[y * w * 3:(y + 1) * w * 3]
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    open(path, "wb").write(png)


def render(level, nplanes=5):
    gfx = load_gfx()
    pal = palette32()
    cells, unknown = build_cells.build_cells(level)
    W, H = 640, 400
    buf = bytearray(W * H * 3)
    drawn = 0
    for (r, c), (blkA, sA, blkB, sB, blkC, sC) in sorted(cells.items()):
        if c < 0 or c > 39 or r < 0 or r > 24:
            continue
        X, Y = c * 16, r * 16
        for blk, sh in ((blkB, sB), (blkA, sA), (blkC, sC)):
            if sh == 0:
                continue
            px = decode_shape(gfx, blk & 0xff, sh & 0xff, nplanes)
            if px is None:
                continue
            drawn += 1
            for yy in range(16):
                ro = ((Y + yy) * W + X) * 3
                for xx in range(16):
                    ci = px[yy][xx]
                    R, G, B = pal[ci]
                    k = ro + xx * 3
                    buf[k] = R; buf[k + 1] = G; buf[k + 2] = B
    out = os.path.join(HERE, "out", "level_%02d.png" % level)
    write_png(out, W, H, buf)
    nz = sum(1 for i in range(0, len(buf), 3) if buf[i] or buf[i+1] or buf[i+2])
    print("level %d -> %s  cells=%d layers_drawn=%d nonblack=%.1f%% unknown=%s"
          % (level, out, len(cells), drawn, 100.0 * nz / (W * H),
             {repr(k): len(v) for k, v in unknown.items()}))
    return out


if __name__ == "__main__":
    lvl = int(sys.argv[1]) if len(sys.argv) > 1 else 0
    render(lvl)
