#!/usr/bin/env python3
"""Generate deterministic combat fixtures from the real game routines.

The fixture file stores one full baseline per level plus per-scenario seeds and
per-frame object/cell deltas.  The wasm test reconstructs the full expected
state each frame and compares every object field, every cell byte, the captured
gameplay globals, RNG state, object cursor and sound-ID events.

Usage:
    python3 tools/combat_fixtures.py [--only name,...]
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

import emulate_load as E  # noqa: E402
import emulate_startup as S  # noqa: E402
from oracle_physics import PhysicsOracle  # noqa: E402

# Gameplay globals the C port currently stores, in capture-name form.
C_GLOBALS = [
    "a0f1", "a0f2", "a0f6", "a0f8", "a0fc", "a0fd", "a0ea", "a0b4", "a0b8",
    "2ba", "2ca", "2c8", "2c6", "2e0", "306", "30a", "30b", "30c",
    "461b", "461d", "461f", "4619", "4627", "4629", "460d",
    "5118", "5126", "5128", "5124", "512e", "5130", "5132", "5134",
    "513c", "513d", "5140", "5142", "509c", "509e", "4660", "4652",
    "2a6", "2a8", "2da", "50a5", "50a7", "50a0", "50be", "50c4",
    "50c7", "50c9", "a0ff", "a0c4",
]
GLOBAL_OFF = {name: (off, width) for name, off, width in S.GLOBALS}
OBJ_FIELDS = [[name, off, (kind == "byte")]
              for name, off, kind in E.OBJ_FIELDS]


def apply_seed(o, seed):
    player = seed.get("player", {})
    if "x" in player:
        o.seed_global(0x4611, player["x"])
    if "y" in player:
        o.seed_global(0x4613, player["y"])
    for name, val in seed.get("globals", {}).items():
        off, width = GLOBAL_OFF[name]
        o.seed_global(off, val, width)
    for slot, fields in seed.get("objects", {}).items():
        o.seed_object(int(slot), fields)
    for col, row, off, val in seed.get("cell_bytes", []):
        raw = bytearray(bytes.fromhex(
            E.dump_cells(o.mu)[row][col]))
        raw[off] = val & 0xFF
        o.seed_cell(col, row, bytes(raw))
    for col, row, data in seed.get("cells", []):
        o.seed_cell(col, row, data)
    if "rng" in seed:
        o.seed_rng(*seed["rng"])
    if "cursor" in seed:
        E.w16(o.mu, E.DATA_SEG, 0x5120, seed["cursor"])


def dump_state(o):
    return {
        "player": [o.state(0x4611), o.state(0x4613)],
        "globals": o.dump_globals(C_GLOBALS),
        "objects": E.dump_objects(o.mu),
        "cells": E.dump_cells(o.mu),
        "rng": o.rng(),
        "cursor": E.u16(o.mu, E.DATA_SEG, 0x5120),
        "sound": list(o.sound_events),
    }


def guard_branches(o):
    """Phase 2 fixtures must not reach the unported EBF2 spawner/exit paths."""
    a0ce = o.state(0xA0CE) & 0xFFFF
    a10b = o.state(0xA10B) & 0xFF
    if a0ce > 0x10E or a10b:
        raise RuntimeError(
            "EBF2 spawner branch reached (a0ce=%d a10b=%d)" % (a0ce, a10b))
    a0be = o.state(0xA0BE) & 0xFFFF
    a0cc = o.state(0xA0CC) & 0xFFFF
    a0f5 = o.state(0xA0F5) & 0xFF
    if a0be > 0 and a0cc == 0 and a0f5 == 0:
        raise RuntimeError("EBF2 exit branch is reachable")


def state_diff(prev, cur):
    changes = {}
    for i, (a, b) in enumerate(zip(prev["objects"], cur["objects"])):
        fields = {}
        for name, _off, _byte in OBJ_FIELDS:
            if (a[name] & 0xFFFF) != (b[name] & 0xFFFF):
                fields[name] = b[name]
        if fields:
            changes[str(i)] = fields
    cell_changes = []
    for row in range(len(cur["cells"])):
        for col in range(len(cur["cells"][row])):
            if prev["cells"][row][col] != cur["cells"][row][col]:
                cell_changes.append([row, col, cur["cells"][row][col]])
    return changes, cell_changes


def run_scenario(sc, baselines):
    level = sc["level"]
    o = PhysicsOracle(level)
    o.reset()
    apply_seed(o, sc.get("seed", {}))
    prev = dump_state(o)
    frames = []
    for inp in sc["frames"]:
        o.set_inputs(**inp)
        if sc["mode"] == "objects":
            o.step_objects_only()
        else:
            guard_branches(o)
            o.step_full()
            guard_branches(o)
        cur = dump_state(o)
        obj_changes, cell_changes = state_diff(prev, cur)
        frames.append({
            "input": inp,
            "player": cur["player"],
            "globals": cur["globals"],
            "rng": cur["rng"],
            "cursor": cur["cursor"],
            "sound": cur["sound"],
            "object_changes": obj_changes,
            "cell_changes": cell_changes,
        })
        prev = cur
    return {
        "name": sc["name"],
        "level": level,
        "mode": sc["mode"],
        "seed": sc.get("seed", {}),
        "frames": frames,
    }


def inputs(n, **kw):
    return [dict(kw) for _ in range(n)]


SCENARIOS = [
    {
        "name": "noop_objects_l3", "level": 3, "mode": "objects",
        "seed": {
            "player": {"x": 288, "y": 224},
            "globals": {"50a5": 0x1234, "50a7": 0x9ABC},
            "objects": {"50": {"p02": 0x1234, "p04": 0x5678}},
            "cell_bytes": [[0, 0, 0, 0xAB], [39, 24, 19, 0xCD]],
            "rng": [0x1234, 0x5678],
            "cursor": 40,
        },
        "frames": inputs(10),
    },
]


def main():
    only = None
    for arg in sys.argv[1:]:
        if arg.startswith("--only="):
            only = set(arg.split("=", 1)[1].split(","))
    scenarios = [sc for sc in SCENARIOS if not only or sc["name"] in only]
    levels = sorted({sc["level"] for sc in scenarios})
    baselines = {}
    for level in levels:
        print("baseline level", level, "...", flush=True)
        o = PhysicsOracle(level)
        baselines[str(level)] = dump_state(o)
    out = {
        "exe_sha256": E.sha256_file(E.EXE),
        "globals": [[name, GLOBAL_OFF[name][0]] for name in C_GLOBALS],
        "object_fields": OBJ_FIELDS,
        "levels": baselines,
        "scenarios": [],
    }
    for sc in scenarios:
        print("running", sc["name"], "...", flush=True)
        out["scenarios"].append(run_scenario(sc, baselines))
    path = os.path.join(ROOT, "notes", "combat_fixtures.json")
    with open(path + ".tmp", "w") as f:
        json.dump(out, f, separators=(",", ":"))
    os.replace(path + ".tmp", path)
    print("wrote", path, "scenarios:", len(out["scenarios"]))


if __name__ == "__main__":
    main()
