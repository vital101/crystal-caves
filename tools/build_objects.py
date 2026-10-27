#!/usr/bin/env python3
"""Build the per-level object-spawn catalog from the decoded command tables.

The command decoder now resolves every `call 0x47D` site to
(handler_offset, handler_segment).  This tool walks each level row like the
original loader, evaluates the spawn arguments, and records the resulting
object record fields.

Output: notes/level_objects.json
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from build_cells import PROL, ev, load_tables  # noqa: E402

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NOTE = os.path.join(HERE, "notes")
BLOB_FILE = 0x8CE0
ROW_LEN = 41
HANDLER_SEG = 0x1F9A


def collect(level):
    reg, spc = load_tables()
    C = dict(PROL.get(level, {}))
    C.setdefault("5120", 4)
    C.setdefault("50dc", 4)
    C.setdefault("50de", 7)
    C.setdefault("50e0", 0xE)
    lt = json.load(open(os.path.join(NOTE, "level_table.json")))
    blob = open(os.path.join(HERE, "extracted", "CC1-UNLZ.EXE"), "rb").read()
    L = lt["levels"][level]
    objects = []
    unknown = {}

    for rw in L["rows"]:
        dest = rw["dest"]
        src = BLOB_FILE + rw["blob_row"] * ROW_LEN
        line = blob[src + 1: src + 1 + 40]
        for col in range(40):
            ch = line[col]
            key = chr(ch)
            if key in reg:
                cmd, table = reg[key], "reg"
            elif key in spc:
                cmd, table = spc[key], "spc"
            else:
                unknown.setdefault(key, []).append((dest, col))
                continue

            for kind, args in cmd:
                if kind != "Object":
                    continue
                if len(args) < 14:
                    unknown.setdefault("OBJECT<14", []).append((dest, col))
                    continue
                a = [ev(p, C, dest, col, table) for p in args[:-2]]
                handler_off, handler_seg = args[-2], args[-1]
                if not isinstance(handler_off, int) or not isinstance(handler_seg, int):
                    unknown.setdefault("OBJECT<dyn>", []).append((dest, col))
                    continue

                selector = a[5]
                shape = a[8] if selector == 1 else a[7]
                obj = {
                    "level": level,
                    "row": a[4],
                    "col": a[3],
                    "index": a[0],
                    "handler_off": handler_off,
                    "handler_seg": handler_seg,
                    "kind": a[1],
                    "block": a[2],
                    "selector": selector,
                    "shape": shape,
                    "shape_alt": a[6],
                    "shape_sel": a[8],
                    "anim_max": a[9],
                    "speed": a[10],
                    "p18": a[11],
                    "map_char": key,
                }
                objects.append(obj)
                # loader increments the spawn cursor after every spawn
                C["5120"] = a[0] + 1

    return objects, unknown


def main():
    lt = json.load(open(os.path.join(NOTE, "level_table.json")))
    nlev = len(lt["levels"])
    allobj = {}
    unknown_all = {}
    for lv in range(nlev):
        objs, unk = collect(lv)
        allobj[str(lv)] = objs
        if unk:
            unknown_all[str(lv)] = {repr(k): v for k, v in unk.items()}
        print("level %2d: %3d objects" % (lv, len(objs)))

    json.dump({"levels": allobj, "unknown": unknown_all},
              open(os.path.join(NOTE, "level_objects.json"), "w"), indent=1)

    # quick validation against the handler table
    htab = set(json.load(open(os.path.join(HERE, "out", "portable", "objects.json")))["handler_table"])
    bad = []
    for lv, objs in allobj.items():
        for o in objs:
            if o["handler_off"] not in htab:
                bad.append((lv, o["handler_off"], o["map_char"]))
            if o["handler_seg"] != HANDLER_SEG:
                bad.append((lv, "seg=%04X" % o["handler_seg"], o["map_char"]))
            if o["index"] < 1 or o["index"] > 60:
                bad.append((lv, "idx=%d" % o["index"], o["map_char"]))
    print("handler/segment/index problems:", bad[:20], "count", len(bad))
    print("wrote notes/level_objects.json")


if __name__ == "__main__":
    main()
