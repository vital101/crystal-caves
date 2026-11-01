#!/usr/bin/env python3
"""Generate the 1200x630 Open Graph card from a real shell screen.

The image is not redrawn: it is the original title screen decoded from the
committed `out/portable/screens.bin` palette-index dump and upscaled with
nearest-neighbour pixels, centred on a 1200x630 black field.  That keeps the
Milo/dragon artwork and the Crystal Caves logo crisp at the size Apple
Messages, Slack and Discord display link previews.

No third-party deps (Pillow is not required): PNG is written with zlib/struct.

    python3 tools/make_social_card.py [--screen title] [--out wasm/social-card.png]
"""
import argparse
import json
import os
import struct
import zlib

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(HERE, "out", "portable")
WASM = os.path.join(HERE, "wasm")

CARD_W = 1200
CARD_H = 630
MARGIN_X = 24
MARGIN_Y = 15
BACKGROUND = (0, 0, 0)


def chunk(typ, data):
    c = struct.pack(">I", len(data)) + typ + data
    return c + struct.pack(">I", zlib.crc32(typ + data) & 0xFFFFFFFF)


def rgb_png_bytes(width, height, pixels):
    """pixels: flat bytes, RGB, row-major.  Filter 0 keeps it simple."""
    stride = width * 3
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        raw += pixels[y * stride:(y + 1) * stride]
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    return png


def load_screen(screen_id):
    with open(os.path.join(OUT, "screens.json")) as f:
        meta = json.load(f)["screens"]
    entry = next((s for s in meta if s["id"] == screen_id), None)
    if entry is None:
        raise SystemExit("no screen %r in screens.json (have: %s)"
                         % (screen_id, ", ".join(s["id"] for s in meta)))
    with open(os.path.join(OUT, "screens.bin"), "rb") as f:
        f.seek(entry["offset"])
        data = f.read(entry["width"] * entry["height"])
    if len(data) != entry["width"] * entry["height"]:
        raise SystemExit("short screen data for %s" % screen_id)
    with open(os.path.join(OUT, "palette.json")) as f:
        palette = [tuple(c) for c in json.load(f)["colors"]]
    return entry["width"], entry["height"], data, palette


def compose(screen_id):
    sw, sh, data, palette = load_screen(screen_id)
    scale = min((CARD_W - 2 * MARGIN_X) // sw, (CARD_H - 2 * MARGIN_Y) // sh)
    if scale < 1:
        raise SystemExit("screen does not fit the card")
    draw_w, draw_h = sw * scale, sh * scale
    ox = (CARD_W - draw_w) // 2
    oy = (CARD_H - draw_h) // 2

    bg = bytes(BACKGROUND)
    pixels = bytearray(bg * (CARD_W * CARD_H))
    for y in range(draw_h):
        sy = y // scale
        row = data[sy * sw:(sy + 1) * sw]
        dst = ((oy + y) * CARD_W + ox) * 3
        for x in range(draw_w):
            r, g, b = palette[row[x // scale]]
            pixels[dst:dst + 3] = bytes((r, g, b))
            dst += 3

    print("card: %dx%d, %s %dx%d at %dx, %dx"
          % (CARD_W, CARD_H, screen_id, draw_w, draw_h, ox, oy))
    return rgb_png_bytes(CARD_W, CARD_H, pixels)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--screen", default="title",
                    help="shell screen id from out/portable/screens.json (default: title)")
    ap.add_argument("--out", default=os.path.join(WASM, "social-card.png"),
                    help="output path (default: wasm/social-card.png)")
    args = ap.parse_args()

    png = compose(args.screen)
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, "wb") as f:
        f.write(png)
    print("wrote %s (%d bytes)" % (args.out, len(png)))


if __name__ == "__main__":
    main()