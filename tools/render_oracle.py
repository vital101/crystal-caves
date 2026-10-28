#!/usr/bin/env python3
"""Render a level straight from the golden-oracle cell dump (ground truth).

Uses the formats confirmed from the binary's own draw code:
  * shape  = 16 rows x 2 groups x 5 bytes [mask][plane0..plane3];
    mask bit set = opaque pixel, colour index = sum of plane bits
    (plane0=blue .. plane3=intensity)  [0x28B5:0x39B lodsb sequence]
  * cell   = 20 bytes: w2=shape1 w3=shape2 w4=block1 w5=block2 w6=block3
    w7=shape3; draw order layer1(w4,w2), layer2(w5,w3 if>0), layer3(w6,w7 if>0)
    [SetCell CS:0x59 stores + immediate draws via CS:0x0000 helper]
  * map    = 640x400 row-major, cell (row,col) at (col*16, row*16)
    [SetBytesPerRow(0x50) before loader; blit A=row*16, B=col*2]

PNG written by hand (zlib), no third-party deps.
"""
import json
import os
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GFX = os.path.join(HERE, "extracted", "CC1.GFX")
ORACLE = os.path.join(HERE, "notes", "oracle_objects.json")

BLK = 8003

# BIOS default EGA palette (mode 0Dh/0Eh/10h); the game never reprograms it.
EGA = [(0x00, 0x00, 0x00), (0x00, 0x00, 0xAA), (0x00, 0xAA, 0x00),
       (0x00, 0xAA, 0xAA), (0xAA, 0x00, 0x00), (0xAA, 0x00, 0xAA),
       (0xAA, 0x55, 0x00), (0xAA, 0xAA, 0xAA), (0x55, 0x55, 0x55),
       (0x55, 0x55, 0xFF), (0x55, 0xFF, 0x55), (0x55, 0xFF, 0xFF),
       (0xFF, 0x55, 0x55), (0xFF, 0x55, 0xFF), (0xFF, 0xFF, 0x55),
       (0xFF, 0xFF, 0xFF)]


def decode_shape(gfx, block, idx):
    """1-based block/shape -> 16 rows x 16 palette indices (None = transparent)."""
    if block < 1 or block > 23 or idx < 1 or idx > 50:
        return None
    base = (block - 1) * BLK + 3 + (idx - 1) * 160
    if base + 160 > len(gfx):
        return None
    d = gfx[base:base + 160]
    px = []
    for y in range(16):
        row = []
        for g in range(2):          # two 8-pixel groups per row
            o = y * 10 + g * 5
            mask = d[o]
            p0, p1, p2, p3 = d[o + 1], d[o + 2], d[o + 3], d[o + 4]
            for bit in range(8):    # MSB = leftmost pixel
                m = 1 << (7 - bit)
                if mask & m:
                    c = ((p0 >> (7 - bit)) & 1) | (((p1 >> (7 - bit)) & 1) << 1) \
                        | (((p2 >> (7 - bit)) & 1) << 2) | (((p3 >> (7 - bit)) & 1) << 3)
                    row.append(c)
                else:
                    row.append(None)
        px.append(row)
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


def render(level, out=None, block0_sub=20):
    """block0_sub: what block number replaces block 0 in layer references.

    The 0xFFD/0xFFF two-wide-object commands draw their second layer from
    block [0x2d6].  That global is set exactly once in the binary (level 2's
    prologue, value 20) and persists across level loads; on a fresh boot it is
    0 and the game would read shape data from the interrupt-vector table
    (machine-dependent garbage).  Sequential play yields 20 for every level,
    and the L0/L1 bottom-row ground strip only makes sense with real tiles,
    so 20 is the faithful substitution.
    """
    gfx = open(GFX, "rb").read()
    oracle = json.load(open(ORACLE))
    cells = oracle[str(level)]["cells"]
    W, H = 640, 400
    buf = bytearray(W * H * 3)          # black background

    def blit(px, X, Y):
        for yy in range(16):
            fy = Y + yy
            if fy < 0 or fy >= H:
                continue
            for xx in range(16):
                fx = X + xx
                if fx < 0 or fx >= W:
                    continue
                c = px[yy][xx]
                if c is None:
                    continue            # mask bit clear -> transparent
                k = (fy * W + fx) * 3
                buf[k], buf[k + 1], buf[k + 2] = EGA[c]

    n = 0
    for r in range(25):
        for c in range(40):
            w = struct.unpack("<10H", bytes.fromhex(cells[r][c]))
            w2, w3, w4, w5, w6, w7 = w[2], w[3], w[4], w[5], w[6], w[7]
            X, Y = c * 16, r * 16
            for blk, shp in ((w4, w2), (w5, w3), (w6, w7)):
                if shp <= 0:
                    continue
                blk &= 0xFF
                shp &= 0xFF
                if blk == 0 and block0_sub:
                    blk = block0_sub
                px = decode_shape(gfx, blk, shp)
                if px:
                    blit(px, X, Y)
                    n += 1
    out = out or os.path.join(HERE, "out", "oracle_L%02d.png" % level)
    write_png(out, W, H, buf)
    print("level %d -> %s  layers_drawn=%d" % (level, out, n))
    return out


if __name__ == "__main__":
    for a in sys.argv[1:] or ["0"]:
        render(int(a))
