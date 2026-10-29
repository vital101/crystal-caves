#!/usr/bin/env python3
"""Generate per-handler oracle fixtures (Phase 4).

Each scenario starts from the exact startup+loader image for its level, applies
a seed, then either calls one handler directly with the original (y,x,slot)
argument order or runs the real object/full frame loop.  State changes are
stored as deltas so the committed fixture stays compact.
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

import emulate_load as E  # noqa: E402
from combat_fixtures import (  # noqa: E402
    C_GLOBALS, GLOBAL_OFF, OBJ_FIELDS, apply_seed, dump_state, state_diff,
)
from oracle_physics import PhysicsOracle  # noqa: E402

HANDLERS = [
    0x298A, 0x2ACF, 0x2BD4, 0x2D37, 0x2E25, 0x2F83, 0x32F6, 0x3416,
    0x36B0, 0x3904, 0x3A2E, 0x3F6C, 0x403A, 0x4145, 0x47B9, 0x48FE,
    0x4AD3, 0x4C92, 0x4FD4, 0x52E7, 0x55EC, 0x5C8F, 0x6232, 0x62F2,
    0x6458,
]

# Handler 0x3F6C has no static Episode 1 spawn (its '@' map command is unused).
DYNAMIC_3F6C = {
    "level": 3, "slot": 50, "kind": 0x1C, "shape": 37, "block": 14,
    "p18": 0, "speed": 0, "vx": 0, "vy": 0,
}


def make_seed(level, rec, player_mode="near", cam=True):
    x = rec["x"]
    y = rec["y"]
    seed = {
        "player": {"x": x - 8 if player_mode == "near" else (x + 0x220),
                   "y": y if player_mode == "near" else (y + 0x120)},
        "globals": {
            "a0f8": 0, "a0f6": 0, "513c": 1, "5140": 0,
            "a0ea": 0xFFFF, "5124": 0,
        },
        "rng": [0x1234, 0x5678],
    }
    if cam:
        seed["globals"]["512e"] = max(0, min(x - 0x80, 0x180))
        seed["globals"]["5130"] = max(0, min(y - 0x60, 0xC8))
    return seed


def predicted(rec):
    return (int(rec["x"]) + int(rec.get("vx", 0)) * int(rec.get("speed", 0)),
            int(rec["y"]) + int(rec.get("vy", 0)) * int(rec.get("speed", 0)))


def find_records(handles):
    d = json.load(open(os.path.join(ROOT, "notes", "oracle_objects.json")))
    by_handler = {h: [] for h in handles}
    for lv in map(str, range(19)):
        if int(lv) < 2:
            continue
        for sp in d[lv]["spawns"]:
            h = sp["handler_off"]
            if h not in by_handler:
                continue
            slot = sp["slot"]
            if slot <= 3 or not (0 <= slot < E.OBJ_SLOTS):
                continue
            rec = dict(d[lv]["objects"][slot])
            rec.update({"level": int(lv), "slot": slot})
            by_handler[h].append(rec)
    out = {}
    for h, recs in by_handler.items():
        if recs:
            out[h] = recs
    return out


def template_3f6c():
    return {
        "level": DYNAMIC_3F6C["level"], "slot": DYNAMIC_3F6C["slot"],
        "kind": DYNAMIC_3F6C["kind"], "shape": DYNAMIC_3F6C["shape"],
        "block": DYNAMIC_3F6C["block"], "p18": DYNAMIC_3F6C["p18"],
        "speed": DYNAMIC_3F6C["speed"], "vx": DYNAMIC_3F6C["vx"],
        "vy": DYNAMIC_3F6C["vy"], "x": 10 * 16, "y": 10 * 16,
        "state": 2, "ready": 1, "handler_off": 0x3F6C,
        "handler_seg": 0x1F9A, "tile": 14, "p10": 1, "p12": 37,
        "p14": 37, "p16": 9, "p1a": 0, "p1e": 0, "p20": 0,
        "p22": 0, "p24": 0, "p02": 0, "p04": 1,
    }


def object_seed(rec, variant=None):
    fields = {k: rec[k] for k in (
        "state", "p02", "p04", "x", "y", "x_prev", "y_prev", "tile",
        "p10", "p12", "p14", "p16", "p18", "p1a", "shape", "p1e", "p20",
        "p22", "p24", "kind", "ready", "handler_off", "handler_seg",
    ) if k in rec}
    fields["state"] = 0
    fields["ready"] = 0
    if variant == "state1":
        fields["state"] = 1
    elif variant == "state3":
        fields["state"] = 3
    elif variant == "p18_1":
        fields["p18"] = 1
    elif variant == "p18_2":
        fields["p18"] = 2
    elif variant == "p18_3":
        fields["p18"] = 3
    return fields


def snap_delta(prev, cur):
    obj_changes, cell_changes = state_diff(prev, cur)
    return {
        "player": cur["player"], "globals": cur["globals"],
        "rng": cur["rng"], "cursor": cur["cursor"], "sound": cur["sound"],
        "object_changes": obj_changes, "cell_changes": cell_changes,
    }


def handler_scenario(name, handler, rec, player_mode, variant=None,
                     call_offsets=((0, 0),)):
    seed = make_seed(rec["level"], rec, player_mode)
    seed["objects"] = {str(rec["slot"]): object_seed(rec, variant)}
    o = PhysicsOracle(rec["level"])
    o.reset()
    apply_seed(o, seed)
    prev = dump_state(o)
    calls = []
    for dx, dy in call_offsets:
        px, py = predicted(rec)
        cx, cy = int(px) + dx, int(py) + dy
        o.call_handler(handler, rec["slot"], cx, cy)
        cur = dump_state(o)
        delta = snap_delta(prev, cur)
        delta["x"] = cx
        delta["y"] = cy
        calls.append(delta)
        prev = cur
    return {
        "name": name, "level": rec["level"], "mode": "handler",
        "seed": seed, "calls": calls, "handler": handler,
        "slot": rec["slot"],
    }


def frame_scenario(name, handler, rec, player_mode="near", mode="objects",
                   frames=8):
    seed = make_seed(rec["level"], rec, player_mode)
    seed["objects"] = {str(rec["slot"]): object_seed(rec)}
    o = PhysicsOracle(rec["level"])
    o.reset()
    apply_seed(o, seed)
    prev = dump_state(o)
    out = []
    for i in range(frames):
        inp = {"right": (i // 3) % 2 == 0}
        o.set_inputs(**inp)
        if mode == "full":
            o.step_full()
        else:
            o.step_objects_only()
        cur = dump_state(o)
        delta = snap_delta(prev, cur)
        delta["input"] = inp
        out.append(delta)
        prev = cur
    return {
        "name": name, "level": rec["level"], "mode": mode,
        "seed": seed, "frames": out, "handler": handler,
        "slot": rec["slot"],
    }


def main():
    records = find_records(HANDLERS)
    records[0x3F6C] = [template_3f6c()]
    scenarios = []
    for h in HANDLERS:
        recs = records.get(h)
        if not recs:
            raise RuntimeError("no oracle spawn record for handler %04X" % h)
        # One record per distinct level, capped at two levels for size.
        chosen = []
        seen = set()
        for rec in recs:
            if rec["level"] in seen:
                continue
            seen.add(rec["level"])
            chosen.append(rec)
            if len(chosen) >= 2:
                break
        for ri, rec in enumerate(chosen):
            tag = "%04X_l%d" % (h, rec["level"])
            scenarios.append(handler_scenario(
                tag + "_near", h, rec, "near"))
            scenarios.append(handler_scenario(
                tag + "_far", h, rec, "far"))
            scenarios.append(handler_scenario(
                tag + "_edge", h, rec, "near", "state1",
                call_offsets=((-16, 0), (0, -16), (0, 0))))
            scenarios.append(handler_scenario(
                tag + "_p18", h, rec, "near", "p18_2"))
            # One update-loop scenario: this is the dispatch/gating test.
            scenarios.append(frame_scenario(
                tag + "_objects", h, rec, "near", "objects", 8))

    payload = {
        "globals": [[name, off] for name, (off, _w) in GLOBAL_OFF.items()
                    if name in C_GLOBALS],
        "object_fields": [[name, off] for name, off, _b in OBJ_FIELDS],
        "levels": {},
        "scenarios": scenarios,
    }
    for lv in sorted({s["level"] for s in scenarios}):
        o = PhysicsOracle(lv)
        o.reset()
        payload["levels"][str(lv)] = dump_state(o)
    out = os.path.join(ROOT, "notes", "handler_fixtures.json")
    with open(out, "w") as f:
        json.dump(payload, f, separators=(",", ":"))
    print("wrote", out, "scenarios:", len(scenarios),
          "levels:", sorted(payload["levels"]))


if __name__ == "__main__":
    main()
