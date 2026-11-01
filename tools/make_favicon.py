#!/usr/bin/env python3
"""Generate the web favicon set from Milo's real in-game sprite.

The art is not redrawn: it is decoded straight out of the committed GFX asset
with the same shape format the game's blitter uses.

  * shape = 16 rows x 2 groups x 5 bytes [mask][plane0..plane3];
    mask bit set = opaque pixel, colour index = p0 + 2*p1 + 4*p2 + 8*p3
    (plane0=blue .. plane3=intensity), MSB = leftmost pixel
  * block 6 is the normal-gravity player bank; 8 is gravity-flipped, 7 is the
    death bank, 0xB the rope-descent bank and 0x11 the ship/ending bank
  * standing shapes are 0x19 (facing right, move_dir == 1) and 0x1A (left)

Outputs (transparent unless noted):
  favicon.ico          16 + 32 px, 32-bit BGRA DIBs with a real AND mask
  favicon-16.png       1:1 native pixels
  favicon-32.png       2x nearest-neighbour
  apple-touch-icon.png 180x180, 11x scale centred on an opaque #111 field
  favicon.svg          one rect per opaque pixel, crispEdges

PNG/ICO/SVG are written by hand (zlib/struct), no third-party deps.
"""
import argparse
import json
import os
import struct
import zlib

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GFX = os.path.join(HERE, "out", "portable", "gfx.bin")
PALETTE = os.path.join(HERE, "out", "portable", "palette.json")

BLK = 8003
PLAYER_BLOCK = 6
SHAPE_RIGHT = 0x19
SHAPE_LEFT = 0x1A
SIZE = 16

TOUCH_SIZE = 180
TOUCH_BG = (0x11, 0x11, 0x11)


def decode_shape(gfx, block, idx):
    """1-based block/shape -> 16 rows x 16 palette indices (None = transparent)."""
    if block < 1 or block > 23 or idx < 1 or idx > 50:
        raise SystemExit("block/shape out of range: %d/%d" % (block, idx))
    base = (block - 1) * BLK + 3 + (idx - 1) * 160
    if base + 160 > len(gfx):
        raise SystemExit("shape %d/%d past end of gfx.bin" % (block, idx))
    d = gfx[base:base + 160]
    px = []
    for y in range(SIZE):
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


def load_palette():
    with open(PALETTE) as f:
        return [tuple(c) for c in json.load(f)["colors"]]


def rgba_rows(px, pal):
    """16x16 index grid -> 16 rows of 16 RGBA tuples."""
    rows = []
    for row in px:
        out = []
        for c in row:
            if c is None:
                out.append((0, 0, 0, 0))
            else:
                r, g, b = pal[c]
                out.append((r, g, b, 255))
        rows.append(out)
    return rows


def scale_nearest(rows, factor, bg=None):
    """Integer nearest-neighbour upscale; bg fills the transparent surround."""
    h, w = len(rows), len(rows[0])
    out = []
    for y in range(h * factor):
        src = rows[y // factor]
        line = []
        for x in range(w * factor):
            p = src[x // factor]
            line.append(p if p[3] else (bg if bg else p))
        out.append(line)
    return out


def centered_icon(size, rows, bg):
    """Centre the 16x16 sprite in a size x size opaque square."""
    scale = (size - SIZE) // SIZE
    if scale < 1:
        scale = 1
    pad = (size - SIZE * scale) // 2
    blank = [bg + (255,)] * size
    out = [list(blank) for _ in range(pad)]
    for row in scale_nearest(rows, scale, bg=bg + (255,)):
        out.append([bg + (255,)] * pad + row
                   + [bg + (255,)] * (size - pad - len(row)))
    out += [list(blank) for _ in range(size - len(out))]
    return out


def png_bytes(rows):
    h, w = len(rows), len(rows[0])

    def chunk(typ, data):
        c = struct.pack(">I", len(data)) + typ + data
        return c + struct.pack(">I", zlib.crc32(typ + data) & 0xffffffff)

    raw = bytearray()
    for row in rows:
        raw.append(0)                       # filter: none
        for r, g, b, a in row:
            raw += bytes((r, g, b, a))
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    return png


def write_png(path, rows):
    with open(path, "wb") as f:
        f.write(png_bytes(rows))


def dib_bytes(rows):
    """32-bit bottom-up BGRA DIB with the 1bpp AND mask an ICO reader expects."""
    h, w = len(rows), len(rows[0])
    xor = bytearray()
    for row in reversed(rows):
        for r, g, b, a in row:
            xor += bytes((b, g, r, a))
    # AND mask: 1 bit = transparent, rows padded to a 4-byte boundary.
    stride = ((w + 31) // 32) * 4
    and_mask = bytearray()
    for row in reversed(rows):
        bits = bytearray(stride)
        for x, (_, _, _, a) in enumerate(row):
            if a == 0:
                bits[x >> 3] |= 0x80 >> (x & 7)
        and_mask += bits
    header = struct.pack("<IiiHHIIiiII", 40, w, h * 2, 1, 32, 0,
                         len(xor) + len(and_mask), 0, 0, 0, 0)
    return header + bytes(xor) + bytes(and_mask)


def write_ico(path, grids):
    """grids: list of (size, rows) for the embedded images."""
    images = [(size, dib_bytes(rows)) for size, rows in grids]
    out = bytearray(struct.pack("<HHH", 0, 1, len(images)))
    offset = 6 + 16 * len(images)
    for size, data in images:
        out += struct.pack("<BBBBHHII", size & 0xFF, size & 0xFF, 0, 0,
                           1, 32, len(data), offset)
        offset += len(data)
    for _, data in images:
        out += data
    with open(path, "wb") as f:
        f.write(bytes(out))


def svg_bytes(px, pal):
    """Pixel-art SVG: merge horizontal runs of one colour into a single rect."""
    h, w = len(px), len(px[0])
    body = []
    for y, row in enumerate(px):
        x = 0
        while x < w:
            c = row[x]
            if c is None:
                x += 1
                continue
            run = 1
            while x + run < w and row[x + run] == c:
                run += 1
            r, g, b = pal[c]
            body.append('<rect x="%d" y="%d" width="%d" height="1" fill="#%02x%02x%02x"/>'
                        % (x, y, run, r, g, b))
            x += run
    return (('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %d %d" '
             'shape-rendering="crispEdges">\n  ' % (w, h))
            + "\n  ".join(body) + "\n</svg>\n").encode()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--outdir", default=os.path.join(HERE, "wasm"),
                    help="where to write the icons (default: wasm/)")
    ap.add_argument("--shape", default="right", choices=("right", "left"),
                    help="facing direction of the standing sprite")
    ap.add_argument("--preview", metavar="DIR",
                    help="also write 256px previews of both facings")
    args = ap.parse_args()

    with open(GFX, "rb") as f:
        gfx = f.read()
    pal = load_palette()

    shape = SHAPE_RIGHT if args.shape == "right" else SHAPE_LEFT
    px = decode_shape(gfx, PLAYER_BLOCK, shape)
    rows = rgba_rows(px, pal)

    os.makedirs(args.outdir, exist_ok=True)
    base = lambda name: os.path.join(args.outdir, name)

    write_png(base("favicon-16.png"), rows)
    write_png(base("favicon-32.png"), scale_nearest(rows, 2))
    write_ico(base("favicon.ico"), [(16, rows), (32, scale_nearest(rows, 2))])

    # iOS composites transparency over black; keep the tab-bar colour instead.
    # Largest integer scale that still leaves a margin, so the rounded-corner
    # mask iOS applies does not clip Milo's head or feet.
    write_png(base("apple-touch-icon.png"), centered_icon(TOUCH_SIZE, rows, TOUCH_BG))
    write_png(base("favicon-192.png"), centered_icon(192, rows, TOUCH_BG))
    write_png(base("favicon-512.png"), centered_icon(512, rows, TOUCH_BG))

    with open(base("favicon.svg"), "wb") as f:
        f.write(svg_bytes(px, pal))

    print("wrote favicon.ico, favicon-16.png, favicon-32.png, "
          "apple-touch-icon.png, favicon-192.png, favicon-512.png, "
          "favicon.svg to %s (block %d shape 0x%02X)"
          % (args.outdir, PLAYER_BLOCK, shape))

    if args.preview:
        os.makedirs(args.preview, exist_ok=True)
        for name, idx in (("right", SHAPE_RIGHT), ("left", SHAPE_LEFT)):
            p = decode_shape(gfx, PLAYER_BLOCK, idx)
            write_png(os.path.join(args.preview, "milo_%s_256.png" % name),
                      scale_nearest(rgba_rows(p, pal), 16))
        print("wrote previews to %s" % args.preview)


if __name__ == "__main__":
    main()
