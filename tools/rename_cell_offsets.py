#!/usr/bin/env python3
"""Phase 3.2: replace raw cell-record offsets with cell_fields.h names.

Mechanical: only the third (offset) argument of the cell helpers changes.
Byte-pair writes that reconstruct a whole word are folded to the word
helper by hand afterwards (byte-for-byte identical).
"""
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[1]

WORD = {"0x00": "CELL_W0", "0x02": "CELL_W1", "0x04": "CELL_W2",
        "0x06": "CELL_W3", "0x08": "CELL_W4", "0x0A": "CELL_W5",
        "0x0C": "CELL_W6", "0x0E": "CELL_W7"}
BYTE = dict(WORD)
BYTE.update({"0x10": "CELL_FLAG_SOLID_LO", "0x11": "CELL_FLAG_SOLID",
             "0x12": "CELL_FLAG_EXIT_LO", "0x13": "CELL_FLAG_EXIT"})

# (helper regex words, offset map).  Offset is the 3rd argument; the first
# two arguments may contain balanced parenthesised sub-expressions whose
# own hex constants (pixel offsets) must stay untouched.
ARGS = r"(?:[^,;()]|\([^()]*\))+"
TARGETS = [
    (r"cell_word_cur|cell_word_put|cell_word_raw|cell_word_raw_put", WORD),
    (r"cell_byte_cur|cell_put|cell_border_byte|cell_border_put", BYTE),
]

changed = 0
for path in sorted(list(ROOT.glob("wasm/game/*.c"))
                   + [ROOT / "wasm" / "render" / "render.c"]):
    text = original = path.read_text(encoding="utf-8", newline="")
    for helper, mapping in TARGETS:
        pattern = (r"\b(" + helper + r")(\()"
                   + r"(" + ARGS + ", " + ARGS + ", )"
                   + r"(0x[0-9A-Fa-f]{2})")
        def sub(m, mapping=mapping):
            key = "0x" + m.group(4)[2:].upper()
            return m.group(1) + m.group(2) + m.group(3) + mapping.get(key, key)
        text = re.sub(pattern, sub, text)
    if text != original:
        path.write_text(text, encoding="utf-8", newline="")
        changed += 1
        print(f"changed {path.relative_to(ROOT)}")
print(f"changed={changed}")
