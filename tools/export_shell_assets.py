#!/usr/bin/env python3
"""Export shell assets for the Phase 5 HUD/menu port.

Outputs:
  out/portable/screens.bin     4 x 320x200 indexed pixels (APG, TTL, CDT, END)
  out/portable/screens.json    screen ids/order metadata
  out/portable/mni_f1.bin      50 glyphs x 40 B (3-byte MNI header stripped)
  out/portable/mni_f2.bin      same
  out/portable/mni_spl.bin     same
  out/portable/shell_text.json length-prefixed text blocks (menu/story/help)
  wasm/shell_text_data.h       generated C tables for the shell
"""
import json
import os
import re
import struct

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXTRACT = os.path.join(HERE, "extracted")
OUT = os.path.join(HERE, "out", "portable")
WASM = os.path.join(HERE, "wasm")
EXE = os.path.join(EXTRACT, "CC1-UNLZ.EXE")
W, H = 320, 200

SCREENS = [
    ("apogee", "CC1.APG"),
    ("title", "CC1.TTL"),
    ("credits", "CC1.CDT"),
    ("ending", "CC1.END"),
]

# Text blocks that end with the "END OF WINDOW" sentinel.  The sentinel's
# length byte is the last byte in the block; text starts at start+1.
SENTINEL = b"\rEND OF WINDOW"

# Blocks that do not end with END OF WINDOW (story/instruction continuations).
EXTRA_PAGES = [
    ("story_can_he", 0x11d27),
    ("story_log2", 0x12263),
    ("story_encyclopedia2", 0x12a90),
]

# Stable ids for the important screens.  Unknown blocks get BLOCK_<off>.
ID_BY_FIRST_LINE = {
    "    Please select an option": "menu_main",
    "CRYSTAL CAVES HELP MENU": "menu_help",
    "Quit to: ^": "menu_quit",
    "Warp To Level (1-16):": "menu_warp",
    "KEYBOARD REDEFINE": "menu_key_redefine",
    "KEYBOARD REDEFINED": "menu_key_ok",
    "Sorry, that key is unavailable.": "menu_key_unavailable",
    "    VIDEO BOARD COMPATIBILITY": "menu_video",
    "         INSTRUCTIONS": "instructions_1",
    "You collect what you consider": "story_twibbles",
    "     Three months later...": "story_twibbles2",
    "Congratulations, but your quest": "story_finale",
    "SHIP@S LOG - STARDATE 2121.04": "story_log1",
    "     GALACTIC ENCYCLOPEDIA": "story_encyclopedia1",
    "   HOW TO ORDER CRYSTAL CAVES!": "ordering",
    "ORDER ALL THREE AMAZING EPISODES!": "ordering_disk",
    "       FOREIGN CUSTOMERS": "foreign",
    "   THE OFFICIAL APOGEE BBS": "bbs",
    "     About Apogee Software": "about",
    "   Top Intergalactic Traders": "hiscore_title",
    " Your score is in the top ten! ": "hiscore_name",
    "  Game paused": "message_pause",
    "The last crystal is collected.": "message_last_crystal",
    "The creatures are stunned for": "message_stunned",
    "Don@t give up now Mylo, there": "message_more_crystals",
    "  File Error": "message_file_error",
    "  Set your joystick so that": "message_joystick_calibrate",
    "After eating the red mushroom,": "message_red_mushroom",
    "Use the fire key to": "message_lever",
    "Your pistol can kill any": "message_power_shot",
    "Whoops...you ate a poison": "message_poison",
    "Cheat mode is now": "message_cheat",
    " Game saved.": "message_saved",
    " You can only save the": "message_save_hub_only",
    "You hit an air generator!": "message_air_generator",
    "You can only restore the": "message_restore_hub_only",
    "LEFT  - ": "message_key_redefine",
    "    Whoa!  Look out Mylo.": "message_gravity",
    "Perfect health! $50,000!": "message_perfect_health",
    '"Uh oh...the steering': "message_steering",
    '"Whoa! Where@d that come from!"': "message_surprise",
    '"Whew...made it!  Now I': "message_landed",
}


def load_module():
    data = open(EXE, "rb").read()
    hdr = int.from_bytes(data[8:10], "little") * 16
    return data[hdr:]


def plausible(lines):
    if not lines:
        return False
    for ln in lines:
        if not ln:
            return False
        try:
            ln.encode("ascii")
        except UnicodeEncodeError:
            return False
    return True


def walk_backward(mod, end):
    """Find the longest plausible length-prefixed block ending at `end`."""
    best = None
    for start in range(max(0, end - 0x1000), end):
        p = start
        lines = []
        ok = True
        while p < end:
            n = mod[p]
            if n < 1 or n > 100:
                ok = False
                break
            s = mod[p + 1:p + 1 + n]
            if len(s) != n or any((c < 0x20 and c != 0x0d) or c > 0x7e for c in s):
                ok = False
                break
            lines.append(s.decode("ascii"))
            p += 1 + n
        if ok and p == end and len(lines) >= 2:
            if best is None or len(lines) > len(best[1]):
                best = (start, lines)
    return best


def walk_forward(mod, start, stop=0x100000):
    """Parse a length-prefixed page starting at `start`; stop at a key line."""
    p = start
    lines = []
    while p < len(mod):
        n = mod[p]
        if n < 1 or n > 100:
            break
        s = mod[p + 1:p + 1 + n]
        if len(s) != n or any((c < 0x20 and c != 0x0d) or c > 0x7e for c in s):
            break
        t = s.decode("ascii")
        lines.append(t)
        p += 1 + n
        if t.strip().lower().startswith("press any key") or t.strip().lower().startswith("press escape"):
            break
        if p >= stop:
            break
    return lines, p


# A few message wrappers pad their line buffers with empty rows that are not
# present in the static table.  Keep those rows so the exported block uses the
# same height/position as the original window.
WRAPPER_PADDING = {
    "message_gravity": (4, 1),
    "message_saved": (1, 2),
}


def apply_wrapper_padding(blocks):
    by_id = {b["id"]: b for b in blocks}
    for bid, (index, count) in WRAPPER_PADDING.items():
        b = by_id.get(bid)
        if b and index <= len(b["lines"]):
            b["lines"][index:index] = [""] * count
    return blocks


def walk_sentinel_table(mod, start):
    """Re-read a known block start, preserving empty spacer lines."""
    p = start
    lines = []
    while p < len(mod):
        n = mod[p]
        if n > 100:
            return None
        s = mod[p + 1:p + 1 + n]
        if len(s) != n or any((c < 0x20 and c != 0x0d) or c > 0x7e for c in s):
            return None
        if s == b"END OF WINDOW":
            return lines if any(lines) else None
        lines.append(s.decode("ascii"))
        p += 1 + n
    return None


def enrich_empty_lines(mod, blocks):
    """Keep the original zero-length lines that separate paragraphs.

    walk_backward() rejects empty strings while locating a block start; once
    the start is known we can safely re-read the table and restore them.
    """
    for b in blocks:
        lines = walk_sentinel_table(mod, b["start"])
        if not lines:
            continue
        if [ln for ln in b["lines"] if ln] != [ln for ln in lines if ln]:
            continue
        b["lines"] = lines
    return blocks


def find_needle(mod, needle):
    i = mod.find(needle.encode("ascii"))
    if i < 0:
        return None
    # Text lines may have leading spaces, so the length byte is not always
    # immediately before the matched substring.  Walk back to a valid line.
    for p in range(i, max(0, i - 100), -1):
        n = mod[p]
        if n < 1 or n > 100 or p + 1 + n > len(mod):
            continue
        s = mod[p + 1:p + 1 + n]
        if any((c < 0x20 and c != 0x0d) or c > 0x7e for c in s):
            continue
        try:
            line = s.decode("ascii")
        except UnicodeDecodeError:
            continue
        if needle in line:
            return p
    return i - 1


def split_instruction_pages(blocks):
    """Split long instruction runs at their all-caps section headings."""
    headings = {"TIMED EVENTS", "PLAYER CONTROLS", "HINTS AND SECRETS"}
    out = []
    for b in blocks:
        if not b["id"].startswith("instructions_"):
            out.append(b)
            continue
        if not any(ln.strip() in headings for ln in b["lines"]):
            out.append(b)
            continue
        chunks = []
        cur = []
        for ln in b["lines"]:
            if cur and ln.strip() in headings:
                chunks.append(cur)
                cur = []
            cur.append(ln)
        if cur:
            chunks.append(cur)
        for i, ch in enumerate(chunks, 1):
            bid = b["id"] if i == 1 else "%s_%d" % (b["id"], i)
            out.append({"id": bid, "start": b["start"], "lines": ch})
    return out


def apply_menu_fixture(blocks):
    """Override the menu/information blocks with the real handler buffers.

    The static text tables omit the blank spacer rows that the handlers insert
    at run time, so the oracle capture in notes/menu_fixtures.json is the only
    exact source for these windows.  Blocks keep their original file offset so
    the generated enum order stays stable.
    """
    path = os.path.join(HERE, "notes", "menu_fixtures.json")
    if not os.path.exists(path):
        return blocks
    fixture = json.load(open(path))
    pages = fixture.get("pages", {})
    starts = {
        "ordering_episodes": 0x1378D, "ordering_offer": 0x13428,
        "ordering_disk": 0x130D3,
        "foreign": 0x18CB1, "foreign_au": 0x18E2A, "foreign_de": 0x18F28,
        "foreign_uk": 0x1902D,
        "bbs": 0x19910, "about": 0x19C8F,
        "menu_help": 0x17865, "menu_quit": 0x15DCB, "menu_main": 0x1745D,
        "menu_video": 0x1A4D8, "menu_quit_dos": 0x1147F,
    }
    # "ordering" was the mis-identified first page; the fixture splits the
    # three ordering pages into explicit ids.
    blocks = [b for b in blocks if b["id"] != "ordering"]
    by_id = {b["id"]: b for b in blocks}
    for name, lines in pages.items():
        if name in by_id:
            by_id[name]["lines"] = list(lines)
        else:
            blocks.append({"id": name, "start": starts.get(name, 0),
                           "lines": list(lines)})
    return sorted(blocks, key=lambda b: (b["start"], b["id"]))


def extract_text_blocks(mod):
    blocks = []
    for m in re.finditer(re.escape(SENTINEL), mod):
        end = m.start()
        found = walk_backward(mod, end)
        if not found:
            continue
        start, lines = found
        if not plausible(lines):
            continue
        key = lines[0]
        bid = ID_BY_FIRST_LINE.get(key)
        if bid is None:
            bid = "block_%05x" % start
        blocks.append({"id": bid, "start": start, "lines": lines})

    # Instructions pages after page 1 (they are not sentinel-terminated).
    p = find_needle(mod, "You@ll encounter many dangerous")
    if p is not None:
        idx = 2
        while p < 0x14908:
            lines, nxt = walk_forward(mod, p)
            if not lines:
                break
            blocks.append({"id": "instructions_%d" % idx, "start": p, "lines": lines})
            idx += 1
            if nxt <= p:
                break
            p = nxt

    # Story pages that are separate paragraphs without END OF WINDOW.
    for bid, p in EXTRA_PAGES:
        lines, _ = walk_forward(mod, p)
        if lines:
            blocks.append({"id": bid, "start": p, "lines": lines})

    blocks = enrich_empty_lines(mod, blocks)
    blocks = apply_wrapper_padding(blocks)
    blocks = split_instruction_pages(blocks)

    # De-duplicate ids, keep first occurrence; sort by file offset.
    seen = set()
    uniq = []
    for b in sorted(blocks, key=lambda x: x["start"]):
        if b["id"] in seen:
            continue
        seen.add(b["id"])
        uniq.append(b)
    return uniq


def decode_pcx(path):
    d = open(path, "rb").read()
    if d[0] != 0x0A or d[2] != 1:
        raise ValueError("unsupported PCX: %s" % path)
    nplanes = d[65]
    bpl = struct.unpack("<H", d[66:68])[0]
    if nplanes != 4:
        raise ValueError("expected 4 planes: %s" % path)
    pos = 128
    rows = []
    run = 0
    run_val = 0
    for _y in range(H):
        planes = [bytearray() for _ in range(4)]
        for p in range(4):
            line = bytearray()
            while len(line) < bpl:
                if run == 0:
                    b = d[pos]; pos += 1
                    if b >= 0xC0:
                        run = b & 0x3F
                        run_val = d[pos]; pos += 1
                        if run == 0:
                            continue
                    else:
                        run = 1
                        run_val = b
                take = min(run, bpl - len(line))
                line.extend([run_val] * take)
                run -= take
            planes[p] = line[:bpl]
        row = bytearray(W)
        for x in range(W):
            byte_i = x >> 3
            bit = 7 - (x & 7)
            v = 0
            for p in range(4):
                if (planes[p][byte_i] >> bit) & 1:
                    v |= 1 << p
            row[x] = v
        rows.append(row)
    return b"".join(rows)


def c_escape(s):
    out = []
    for ch in s:
        if ch == "\\":
            out.append("\\\\")
        elif ch == '"':
            out.append('\\"')
        else:
            out.append(ch)
    return "".join(out)


def write_text_header(blocks):
    path = os.path.join(WASM, "shell_text_data.h")
    with open(path, "w") as f:
        f.write("/* Generated by tools/export_shell_assets.py. Do not edit. */\n")
        f.write("#ifndef SHELL_TEXT_DATA_H\n#define SHELL_TEXT_DATA_H\n\n")
        f.write("typedef struct { const char *const *lines; int count; } ShellTextBlock;\n\n")
        f.write("enum {\n")
        for b in blocks:
            f.write("    SHELL_TEXT_%s,\n" % b["id"].upper())
        f.write("    SHELL_TEXT_COUNT\n};\n\n")
        for b in blocks:
            f.write("static const char *const t_%s[] = {\n" % b["id"])
            for ln in b["lines"]:
                f.write('    "%s",\n' % c_escape(ln))
            f.write("};\n")
        f.write("\nstatic const ShellTextBlock g_shell_text_blocks[] = {\n")
        for b in blocks:
            f.write('    { t_%s, %d }, /* SHELL_TEXT_%s @ %#x */\n' %
                    (b["id"], len(b["lines"]), b["id"].upper(), b["start"]))
        f.write("};\n")
        f.write("#endif\n")


def main():
    os.makedirs(OUT, exist_ok=True)
    mod = load_module()

    # PCX screens (indexed 4-bit pixels).
    screens = bytearray()
    meta = []
    for sid, fn in SCREENS:
        idx = decode_pcx(os.path.join(EXTRACT, fn))
        screens += idx
        meta.append({"id": sid, "file": fn, "offset": len(screens) - W * H,
                     "width": W, "height": H})
    open(os.path.join(OUT, "screens.bin"), "wb").write(screens)
    json.dump({"screen_bytes": W * H, "screens": meta},
              open(os.path.join(OUT, "screens.json"), "w"), indent=2)

    # Fonts: strip the 3-byte MNI header; leave exactly 50 glyphs.
    for src, dst in (("CC1-F1.MNI", "mni_f1.bin"),
                     ("CC1-F2.MNI", "mni_f2.bin"),
                     ("CC1-SPL.MNI", "mni_spl.bin")):
        raw = open(os.path.join(EXTRACT, src), "rb").read()
        glyphs = raw[3:3 + 50 * 40]
        if len(glyphs) != 50 * 40:
            raise ValueError("short MNI: %s" % src)
        open(os.path.join(OUT, dst), "wb").write(glyphs)

    # Text blocks.
    blocks = extract_text_blocks(mod)
    blocks = apply_menu_fixture(blocks)
    json.dump({"blocks": blocks}, open(os.path.join(OUT, "shell_text.json"), "w"), indent=1)
    write_text_header(blocks)

    # Manifest.
    mpath = os.path.join(OUT, "manifest.json")
    manifest = json.load(open(mpath)) if os.path.exists(mpath) else {}
    manifest["screens"] = {"file": "screens.bin", "bytes_per_screen": W * H,
                           "screens": meta}
    manifest["fonts"] = {
        "f1": "mni_f1.bin", "f2": "mni_f2.bin", "spl": "mni_spl.bin",
        "glyph_size": 40, "glyph_w": 8, "glyph_h": 8, "count": 50,
        "format": "8 rows x 5 bytes [mask][plane0..plane3]; MSB=leftmost",
    }
    manifest["shell_text"] = {"file": "shell_text.json", "blocks": len(blocks)}
    json.dump(manifest, open(mpath, "w"), indent=2)

    print("exported %d screens, %d text blocks" % (len(meta), len(blocks)))
    for b in blocks:
        print("  %-24s %3d lines" % (b["id"], len(b["lines"])))


if __name__ == "__main__":
    main()
