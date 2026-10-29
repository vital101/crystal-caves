#!/usr/bin/env python3
"""Generate deterministic player-physics fixtures from the real game routines.

Writes notes/physics_fixtures.json.  Each scenario records per-frame input,
player/camera/physics state, and (for platform cases) the platform object
record so the wasm test can prove exact carry behaviour.

Usage:
    python3 tools/physics_fixtures.py [--only name,...]
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

from oracle_physics import PhysicsOracle, FIELDS  # noqa: E402
import emulate_load as E  # noqa: E402


def inputs(n, **kw):
    return [dict(kw) for _ in range(n)]


def seq(parts):
    out = []
    for n, kw in parts:
        out.extend(inputs(n, **kw))
    return out


PHYS_SEED = {
    "jump_phase": 0x13, "airborne": 0, "jump_flag": 0,
    "walk_phase": 0, "walk_speed": 0, "impulse": 0,
    "move_dir": 1, "gravity_scale": 1,
}


def base_seed(x, y):
    return dict(PHYS_SEED, x=x, y=y,
                cam_x=max(0, min(0x140, x - 0xA0)),
                cam_y=max(0x10, min(0xC8, y - 0x64)))


SCENARIOS = [
    {
        "name": "walk_right_l2", "level": 2, "mode": "physics",
        "frames": inputs(60, right=True),
    },
    {
        "name": "walk_left_l2", "level": 2, "mode": "physics",
        "frames": inputs(60, left=True),
    },
    {
        "name": "turn_release_l2", "level": 2, "mode": "physics",
        "frames": seq([(10, {"right": True}), (10, {}),
                       (30, {"left": True}), (10, {})]),
    },
    {
        "name": "wall_right_l2", "level": 2, "mode": "physics",
        "seed": base_seed(608, 80),
        "frames": inputs(30, right=True),
    },
    {
        "name": "camera_left_l2", "level": 2, "mode": "physics",
        "seed": base_seed(16, 80),
        "frames": inputs(20, left=True),
    },
    {
        "name": "jump_tap_l2", "level": 2, "mode": "physics",
        "frames": seq([(2, {}), (1, {"jump": True}), (37, {})]),
    },
    {
        "name": "jump_hold_l2", "level": 2, "mode": "physics",
        "frames": seq([(2, {}), (18, {"jump": True}), (20, {})]),
    },
    {
        "name": "headbump_l2", "level": 2, "mode": "physics",
        "seed": base_seed(192, 128),
        "frames": seq([(1, {"jump": True}), (29, {})]),
    },
    {
        "name": "fall_l2", "level": 2, "mode": "physics",
        "seed": base_seed(96, 128),
        "frames": inputs(40, right=True),
    },
    {
        "name": "reversed_gravity_l2", "level": 2, "mode": "physics",
        "seed": dict(base_seed(496, 224), gravity_scale=-1,
                     player_block=8, airborne=1, jump_phase=0x13),
        "frames": inputs(20),
    },
    {
        "name": "walk_right_l3", "level": 3, "mode": "physics",
        "frames": inputs(60, right=True),
    },
    {
        "name": "jump_tap_l3", "level": 3, "mode": "physics",
        "frames": seq([(2, {}), (1, {"jump": True}), (37, {})]),
    },
    {
        "name": "headbump_l3", "level": 3, "mode": "physics",
        "seed": base_seed(304, 224),
        "frames": seq([(1, {"jump": True}), (29, {})]),
    },
    {
        "name": "platform_h_l2", "level": 2, "mode": "platform",
        "seed": dict(base_seed(152, 120), airborne=1, jump_phase=0x13),
        "frames": inputs(60),
        "objects": [13],
    },
    {
        "name": "platform_v_l2", "level": 2, "mode": "platform",
        "seed": dict(base_seed(120, 296), airborne=1, jump_phase=0x13),
        "frames": inputs(40),
        "objects": [23],
    },
    {
        "name": "platform_v_l3", "level": 3, "mode": "platform",
        "seed": dict(base_seed(232, 312), airborne=1, jump_phase=0x13),
        "frames": inputs(40),
        "objects": [27],
    },
]

OBJ_FIELDS = ["x", "y", "p1a", "p18", "p1e", "p20", "p22", "shape",
              "state", "ready"]


def run_scenario(sc):
    o = PhysicsOracle(sc["level"])
    o.reset()
    o.set_inputs()
    seed = sc.get("seed", {})
    if seed:
        o.seed(**seed)
    rec_objs = sc.get("objects", [])
    frames = []
    for inp in sc["frames"]:
        o.set_inputs(**inp)
        o.step(with_objects=(sc["mode"] == "platform"))
        rec = {"input": inp, "state": o.dump()}
        if rec_objs:
            oh = o.dump_objects()
            rec["objects"] = [{f: oh[i][f] for f in OBJ_FIELDS}
                              for i in rec_objs]
        frames.append(rec)
    return {
        "name": sc["name"],
        "level": sc["level"],
        "mode": sc["mode"],
        "seed": seed,
        "object_slots": rec_objs,
        "frames": frames,
    }


def main():
    only = None
    for arg in sys.argv[1:]:
        if arg.startswith("--only="):
            only = set(arg.split("=", 1)[1].split(","))
    out = {
        "exe_sha256": E.sha256_file(E.EXE),
        "fields": [name for name, _, _ in FIELDS],
        "object_fields": OBJ_FIELDS,
        "tick_hz": 1193182.0 / 0x2147 / 8.0,
        "scenarios": [],
    }
    for sc in SCENARIOS:
        if only and sc["name"] not in only:
            continue
        print("running", sc["name"], "...", flush=True)
        out["scenarios"].append(run_scenario(sc))
    path = os.path.join(ROOT, "notes", "physics_fixtures.json")
    with open(path + ".tmp", "w") as f:
        json.dump(out, f, separators=(",", ":"))
    os.replace(path + ".tmp", path)
    print("wrote", path, "scenarios:", len(out["scenarios"]))


if __name__ == "__main__":
    main()
