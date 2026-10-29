#!/usr/bin/env python3
"""Generate full-frame per-cave integration fixtures (levels 3..18)."""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

from combat_fixtures import (  # noqa: E402
    C_GLOBALS, GLOBAL_OFF, OBJ_FIELDS, apply_seed, dump_state, state_diff,
)
from oracle_physics import PhysicsOracle  # noqa: E402


def inputs_for(i):
    # A deterministic mixed route: move, fire, jump, reverse.
    phase = i % 60
    return {
        "right": phase < 24 or 40 <= phase < 50,
        "left": 24 <= phase < 40,
        "jump": phase in (18, 19, 44),
        "fire": phase % 7 == 0,
    }


def make_scenario(level, tag, seed, frames):
    o = PhysicsOracle(level)
    o.reset()
    apply_seed(o, seed)
    prev = dump_state(o)
    out = []
    for i in range(frames):
        inp = inputs_for(i)
        o.set_inputs(**inp)
        o.step_full()
        cur = dump_state(o)
        # Stop before death/exit/story transitions: those paths are covered by
        # the combat fixtures and would make this a restart fixture.
        if (cur["globals"].get("5124", 0) != 0 or
                cur["globals"].get("a0f6", 0) or
                cur["globals"].get("a0f7", 0) or
                cur["globals"].get("a0fb", 0) or
                cur["globals"].get("a126", 0)):
            break
        obj_changes, cell_changes = state_diff(prev, cur)
        delta = {
            "object_changes": obj_changes, "cell_changes": cell_changes,
            "player": cur["player"], "globals": cur["globals"],
            "rng": cur["rng"], "cursor": cur["cursor"],
            "sound": cur["sound"], "input": inp,
        }
        out.append(delta)
        prev = cur
    return {
        "name": tag, "level": level, "mode": "full",
        "seed": seed, "frames": out,
    }


def main():
    scenarios = []
    for level in range(3, 19):
        # Start-of-level route.
        scenarios.append(make_scenario(level, "L%d_start" % level, {}, 24))
    payload = {
        "globals": [[name, off] for name, (off, _w) in GLOBAL_OFF.items()
                    if name in C_GLOBALS],
        "object_fields": [[name, off] for name, off, _b in OBJ_FIELDS],
        "levels": {},
        "scenarios": scenarios,
    }
    for level in range(3, 19):
        o = PhysicsOracle(level)
        o.reset()
        payload["levels"][str(level)] = dump_state(o)
    out = os.path.join(ROOT, "notes", "cave_fixtures.json")
    with open(out, "w") as f:
        json.dump(payload, f, separators=(",", ":"))
    print("wrote", out, "scenarios:", len(scenarios))


if __name__ == "__main__":
    main()
