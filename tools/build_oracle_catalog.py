#!/usr/bin/env python3
"""Convert oracle_objects.json (Unicorn golden-oracle dump) into a clean
per-level object catalog used by the WASM port.

This REPLACES tools/build_objects.py (notes/level_objects.json), which
double-counted `[X` prefix pairs and overcounted level 11 to 108 objects.

The oracle runs the real loader (0xB1A9), so this catalog is ground truth:
per-level 60-slot object image exactly as the original binary leaves it
after load (state included: 2=inactive slot, 0/0xFFFF=active object).

Output: notes/oracle_catalog.json
"""
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SRC = os.path.join(ROOT, "notes", "oracle_objects.json")
DST = os.path.join(ROOT, "notes", "oracle_catalog.json")

FIELDS = ["state", "p02", "p04", "x", "y", "x_prev", "y_prev", "tile",
          "p10", "p12", "p14", "p16", "p18", "p1a", "shape", "p1e", "p20",
          "p22", "p24", "kind", "ready", "handler_off", "handler_seg"]


def main():
    src = json.load(open(SRC))
    catalog = {"levels": {}, "note": "oracle-derived from Unicorn run of 0xB1A9"}
    for lv in sorted(src, key=lambda k: int(k)):
        recs = src[lv]["objects"]
        objects = []
        spawns = []
        for o in recs:
            objects.append({f: o[f] for f in FIELDS})
        # Map-spawned objects (ret-ip not in prologue range 0xBEB/0xC2F) get a
        # compact spawn summary.  Map position comes from the object record
        # (x=col*16, y=row*16), which is authoritative; stack reads at spawn
        # time use a different frame and are not.
        for s in src[lv]["spawns"]:
            if s["ret_ip"] not in (0x0BEB, 0x0C2F):
                o = recs[s["slot"]]
                spawns.append({
                    "slot": s["slot"],
                    "col": o["x"] // 16,
                    "row": o["y"] // 16,
                    "tile": s["args"][11],
                    "kind": s["args"][12],
                    "handler_off": s["handler_off"],
                    "handler_seg": s["handler_seg"],
                    "state": o["state"],
                    "shape": o["shape"],
                })
        catalog["levels"][lv] = {
            "cursor": src[lv]["cursor"],
            "spawns": spawns,
            "objects": objects,
        }
    json.dump(catalog, open(DST, "w"), indent=1)
    total = sum(len(v["spawns"]) for v in catalog["levels"].values())
    print("wrote", DST)
    print("levels:", len(catalog["levels"]), "map spawns total:", total)
    for lv in sorted(catalog["levels"], key=lambda k: int(k)):
        v = catalog["levels"][lv]
        print("  L%02d cursor=%d map_spawns=%d active=%d"
              % (int(lv), v["cursor"], len(v["spawns"]),
                 sum(1 for o in v["objects"] if o["state"] != 2)))


if __name__ == "__main__":
    main()
