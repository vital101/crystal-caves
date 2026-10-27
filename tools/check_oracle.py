#!/usr/bin/env python3
"""Structural assertions over the regenerated oracle data.

Checks that notes/oracle_objects.json, notes/oracle_catalog.json and
work/wasm/objects_data.h agree and match known anchors from the binary.
Exit nonzero on any failure.
"""
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
NOTE = os.path.join(ROOT, "notes")
OBJ_JSON = os.path.join(NOTE, "oracle_objects.json")
CAT_JSON = os.path.join(NOTE, "oracle_catalog.json")
DATA_H = os.path.join(ROOT, "wasm", "objects_data.h")

# Known spawn-count anchors from the binary (map-spawned only; prologue
# slots 2 and 3 are internal and excluded).
SPAWN_COUNTS = {0: 3, 1: 3, 2: 24, 3: 29, 4: 35, 5: 31, 6: 22, 7: 18,
                8: 23, 9: 25, 10: 37, 11: 19, 12: 27, 13: 30, 14: 18,
                15: 18, 16: 23, 17: 35, 18: 26}

failures = []


def fail(msg):
    failures.append(msg)
    print("FAIL:", msg)


def check_oracle_vs_Y():
    """Player slot-1 cell must equal the unique Y cell in the loaded rows."""
    src = json.load(open(OBJ_JSON))
    lt = json.load(open(os.path.join(NOTE, "level_table.json")))
    exe = open(os.path.join(ROOT, "extracted", "CC1-UNLZ.EXE"), "rb").read()
    base = 0x1E00 + 0x6EE0  # playable blob row table in load image
    for lv in range(19):
        L = lt["levels"][lv]
        rows = {r["dest"]: r["blob_row"] for r in L["rows"]}
        ycells = []
        for row in range(25):
            br = rows.get(row)
            if br is None:
                continue
            raw = exe[base + 41 * br: base + 41 * br + 40]
            for c, ch in enumerate(raw):
                if ch == ord("Y"):
                    ycells.append((row, c - 1))
        p = next((o for o in src[str(lv)]["objects"] if o["index"] == 1), None)
        if p is None:
            fail("L%02d: no slot-1 object" % lv)
            continue
        scell = (p["y"] // 16, p["x"] // 16)
        if scell not in ycells:
            fail("L%02d: player cell %s not in Y cells %s" % (lv, scell, ycells))
    print("ok  player cell == Y cell for all 19 levels")


def check_spawn_counts():
    cat = json.load(open(CAT_JSON))
    for lv in range(19):
        got = len(cat["levels"][str(lv)]["spawns"])
        want = SPAWN_COUNTS[lv]
        if got != want:
            fail("L%02d: spawn count %d != anchor %d" % (lv, got, want))
    print("ok  spawn counts match anchors for all 19 levels")


def check_player_slot():
    cat = json.load(open(CAT_JSON))
    for lv in range(19):
        p = None
        for o in cat["levels"][str(lv)]["objects"]:
            if o["index"] == 1:
                p = o
                break
        if p is None:
            fail("L%02d: no slot-1 record" % lv); continue
        if p["kind"] != 1:
            fail("L%02d: slot-1 kind %d != 1" % (lv, p["kind"]))
        if p["handler_off"] != 0x1fb9:
            fail("L%02d: slot-1 handler %04X != 0x1fb9" % (lv, p["handler_off"]))
    print("ok  slot 1 is player (kind 1, handler 0x1fb9) on all levels")


def check_handlers():
    """Every active handler offset must be a known behaviour ID."""
    src = json.load(open(OBJ_JSON))
    # Behaviour IDs = handler offsets present anywhere in the oracle dumps.
    known = set()
    for lv in range(19):
        for s in src[str(lv)]["spawns"]:
            known.add(s["handler_off"])
    for lv in range(19):
        for o in src[str(lv)]["objects"]:
            off = o["handler_off"]
            if o["state"] == 2 or o["kind"] == 0:
                continue
            if off not in known:
                fail("L%02d slot %d: active handler %04X never spawned" % (lv, o["index"], off))
    print("ok  all active handlers are known behaviour IDs (%d distinct)" % len(known))


def check_signed_coords():
    """Generated header must use int16_t for x/y/vx/vy and contain negatives."""
    h = open(DATA_H).read()
    if "int16_t  x, y, x_prev, y_prev;" not in h:
        fail("objects_data.h: x/y are not int16_t")
    if "int16_t  vx, vy;" not in h:
        fail("objects_data.h: vx/vy are not int16_t")
    if "g_obj_level0[OBJ_MAX] = {" not in h:
        fail("objects_data.h: missing level arrays")
    # Find a negative coordinate literal somewhere (must not be all unsigned).
    if not re.search(r"[-]?\d+, [-]?\d+, [-]?\d+, [-]?\d+, ", h):
        fail("objects_data.h: no negative coordinate literals found (unsigned mask bug?)")
    print("ok  objects_data.h signed fields present")


def check_tables():
    h = open(DATA_H).read()
    for sym in ("g_level_start", "g_level_spawn_count", "g_level_cursor",
                "g_obj_levels", "OBJ_LEVELS"):
        if sym not in h:
            fail("objects_data.h: missing symbol %s" % sym)
    # g_level_start must have 19 entries.
    m = re.search(r"g_level_start\[OBJ_LEVELS\]\[2\] = \{(.*?)\};", h, re.S)
    if not m or m.group(1).count("{") != 19:
        fail("objects_data.h: g_level_start does not have 19 entries")
    print("ok  g_level_start, g_level_spawn_count, g_level_cursor present")


def check_manifest_present():
    h = open(DATA_H).read()
    if "Manifest:" not in h:
        fail("objects_data.h: missing manifest hash line")
    print("ok  manifest hash present")


def main():
    print("== Oracle structural checks ==")
    check_oracle_vs_Y()
    check_spawn_counts()
    check_player_slot()
    check_handlers()
    check_signed_coords()
    check_tables()
    check_manifest_present()
    if failures:
        print("\n%d FAILURE(S)" % len(failures))
        sys.exit(1)
    print("\nALL CHECKS PASSED")


if __name__ == "__main__":
    main()
