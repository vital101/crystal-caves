#!/usr/bin/env python3
"""Golden gameplay oracle: run the real object update loop in Unicorn.

Unlike oracle_frame.py (which isolates player physics subroutines and stubs
segment 0x1F9A), this harness runs the real update loop at 0xD72A with the
real object handlers in segment 0x1F9A.  Video and sound segments are stubbed;
everything else (collision 0xF38 / 0x1F9A:0x43, spawn 0x47D, helpers, math)
runs for real.

Usage:
    python3 tools/emulate_game.py <level> <frames> [left] [right] [jump] [fire]
    python3 tools/emulate_game.py 2 10 --json > frame.json

Outputs one JSON document with per-frame player globals, object records,
the full 20-byte cell map, and input flags.  This is the ground truth for the
C port's deterministic frame tests.
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

import emulate_load as E          # noqa: E402
import oracle_physics as OP      # noqa: E402
from oracle_physics import PhysicsOracle, FIELDS  # noqa: E402

# The update loop and handlers live in 0x1F9A; do not stub it.  Video/sound
# are irrelevant to state and are stubbed by the existing hooks.

UPDATE_LOOP = 0xD72A
INPUTS = {
    "left": 0x338E,
    "right": 0x338F,
    "jump": 0x338B,
    "fire": 0x3390,
}


class GameOracle(PhysicsOracle):
    """Run the real object loop (0xD72A) from exact startup/load state."""
    def __init__(self, level):
        super().__init__(level)

    def set_input(self, name, pressed):
        flags = {"left": False, "right": False, "jump": False, "fire": False,
                 "up": False, "down": False}
        self._inputs[name] = bool(pressed)
        flags.update(self._inputs)
        self.set_inputs(left=flags["left"], right=flags["right"],
                        jump=flags["jump"] or flags["up"],
                        fire=flags["fire"])

    _inputs = {}

    def step(self, frames=1):
        for _ in range(frames):
            self._run(UPDATE_LOOP)

    def dump_frame(self):
        d = self.dump()
        return {
            "level": self.level,
            "player": {
                "x": d["x"],
                "y": d["y"],
                "cam_x": d["cam_x"],
                "cam_y": d["cam_y"],
                "move_dir": d["move_dir"],
            },
            "game": {
                "lives": d["lives"],
                "crystals": d["ammo"],
                "score": (self.state(0x50A7) << 16) | (self.state(0x50A5) & 0xFFFF),
                "g5124": d["exit_state"],
                "a0ea": d["invuln"],
            },
            "globals": {name: self.state(off) for name, off, _ in FIELDS},
            "objects": E.dump_objects(self.mu),
            "cells": E.dump_cells(self.mu),
        }


def main(argv):
    if len(argv) < 3:
        raise SystemExit(__doc__)
    level = int(argv[1])
    frames = int(argv[2])
    pressed = set(a for a in argv[3:] if not a.startswith("-"))
    as_json = "--json" in argv
    px = py = None
    for a in argv[3:]:
        if a.startswith("--px="):
            px = int(a.split("=", 1)[1])
        elif a.startswith("--py="):
            py = int(a.split("=", 1)[1])

    o = GameOracle(level)
    o._inputs = {name: (name in pressed) for name in INPUTS}
    for name in INPUTS:
        o.set_input(name, name in pressed)
    if px is not None and py is not None:
        o.seed(x=px, y=py)

    initial_cells = E.dump_cells(o.mu)
    frames_out = []
    for _ in range(frames):
        o.step(1)
        frames_out.append(o.dump_frame())

    if as_json:
        json.dump({"level": level, "initial_cells": initial_cells,
                   "frames": frames_out}, sys.stdout,
                  separators=(",", ":"))
        sys.stdout.write("\n")
    else:
        last = frames_out[-1]
        print("level %d frames=%d player=(%d,%d) cam=(%d,%d) move=%d"
              % (level, frames, last["player"]["x"], last["player"]["y"],
                 last["player"]["cam_x"], last["player"]["cam_y"],
                 last["player"]["move_dir"]))
        active = [ob for ob in last["objects"] if ob["kind"] and ob["state"] != 2]
        print("active objects:", len(active))
        for ob in active:
            print("  slot %2d kind=%3d handler=%04X state=%d x=%d y=%d p10=%d p18=%d ready=%d"
                  % (ob["index"], ob["kind"], ob["handler_off"], ob["state"],
                     ob["x"], ob["y"], ob["p10"], ob["p18"], ob["ready"]))


if __name__ == "__main__":
    main(sys.argv)
