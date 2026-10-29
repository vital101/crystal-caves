#!/usr/bin/env python3
"""Generate deterministic cell/exit fixtures from the real routines.

Direct cell-event scenarios call 0x0FC5:0xBB26 with a seeded cell and compare
the complete post-call state.  Full-frame scenarios run the exact 0xF4E6
dispatch (including 0xE5D3 contact and the exit/door state machines).
"""
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

import emulate_load as E
from oracle_physics import PhysicsOracle

GLOBALS = [
    # (name, offset, width) - names match notes/startup.json where possible.
    ("a0f1", 0xA0F1, 1), ("a0f2", 0xA0F2, 1), ("a0f4", 0xA0F4, 1),
    ("a0f6", 0xA0F6, 1), ("a0f7", 0xA0F7, 1), ("a0f8", 0xA0F8, 1),
    ("a0f9", 0xA0F9, 1), ("a0f3", 0xA0F3, 1), ("a0fc", 0xA0FC, 1),
    ("a0fd", 0xA0FD, 1), ("a0ea", 0xA0EA, 2), ("a0b4", 0xA0B4, 2),
    ("a0b8", 0xA0B8, 2), ("2ba", 0x02BA, 2), ("2ca", 0x02CA, 2),
    ("2c8", 0x02C8, 2), ("2c6", 0x02C6, 2), ("2e0", 0x02E0, 2),
    ("306", 0x0306, 1), ("30a", 0x030A, 1), ("30b", 0x030B, 1),
    ("30c", 0x030C, 1), ("461b", 0x461B, 2), ("461d", 0x461D, 2),
    ("461f", 0x461F, 2), ("4627", 0x4627, 2), ("4629", 0x4629, 2),
    ("460d", 0x460D, 2), ("5118", 0x5118, 2), ("5126", 0x5126, 2),
    ("5128", 0x5128, 2), ("5124", 0x5124, 2), ("512e", 0x512E, 2),
    ("5130", 0x5130, 2), ("5132", 0x5132, 2), ("5134", 0x5134, 2),
    ("513c", 0x513C, 1), ("513d", 0x513D, 1), ("5140", 0x5140, 1),
    ("5142", 0x5142, 1), ("509c", 0x509C, 2), ("509e", 0x509E, 2),
    ("4660", 0x4660, 1), ("4652", 0x4652, 2), ("2a6", 0x02A6, 2),
    ("2a8", 0x02A8, 2), ("2da", 0x02DA, 2), ("2f2", 0x02F2, 2),
    ("2b8", 0x02B8, 2), ("2b5e", 0x2B5E, 2), ("511e", 0x511E, 2),
    ("5122", 0x5122, 2), ("30d", 0x030D, 1), ("50a5", 0x50A5, 2),
    ("50a7", 0x50A7, 2), ("50a0", 0x50A0, 2), ("50be", 0x50BE, 2),
    ("50c4", 0x50C4, 2), ("50c7", 0x50C7, 2), ("50c9", 0x50C9, 2),
    ("a0ff", 0xA0FF, 1), ("a0c4", 0xA0C4, 2),
    ("a0ce", 0xA0CE, 2), ("a10b", 0xA10B, 1), ("a0cc", 0xA0CC, 2),
    ("a0f5", 0xA0F5, 1), ("a0be", 0xA0BE, 2), ("50a2", 0x50A2, 2),
    ("a0e0", 0xA0E0, 2), ("a0e2", 0xA0E2, 2), ("a0e6", 0xA0E6, 2),
    ("a0e8", 0xA0E8, 2), ("a0d0", 0xA0D0, 2), ("a0d2", 0xA0D2, 2),
    ("a0d4", 0xA0D4, 2), ("a0d6", 0xA0D6, 2), ("a0d8", 0xA0D8, 2),
    ("a0da", 0xA0DA, 2), ("a0dc", 0xA0DC, 2), ("a0de", 0xA0DE, 2),
    ("a0c2", 0xA0C2, 2), ("a0c8", 0xA0C8, 2), ("a0ca", 0xA0CA, 2),
    ("a0e4", 0xA0E4, 2), ("a0c6", 0xA0C6, 2), ("a100", 0xA100, 1),
    ("a101", 0xA101, 1), ("a102", 0xA102, 1), ("a104", 0xA104, 1),
    ("a105", 0xA105, 1), ("a106", 0xA106, 1), ("a107", 0xA107, 1),
    ("a108", 0xA108, 1), ("a109", 0xA109, 1), ("a10a", 0xA10A, 1),
    ("a10d", 0xA10D, 1), ("a0fb", 0xA0FB, 1), ("a126", 0xA126, 1),
    ("3392", 0x3392, 1), ("3395", 0x3395, 1), ("3396", 0x3396, 1),
]
GLOBALS += [("50%02x" % (0xA8 + i), 0x50A8 + i, 1) for i in range(1, 17)]
GLOBAL_OFF = {name: (off, width) for name, off, width in GLOBALS}
OBJ_FIELDS = [[name, off, (kind == "byte")]
              for name, off, kind in E.OBJ_FIELDS]


def cell_hex(words=None, bytes_=None):
    raw = bytearray(20)
    for off, val in (words or {}).items():
        struct.pack_into("<H", raw, off, val & 0xFFFF)
    for off, val in (bytes_ or {}).items():
        raw[off] = val & 0xFF
    return bytes(raw).hex()


def opts(**kw):
    return kw


def snap(o):
    return {
        "player": [o.state(0x4611), o.state(0x4613)],
        "globals": {name: (o.mu.mem_read((E.DATA_SEG << 4) + off, width)[0]
                           if width == 1 else o.state(off))
                    for name, off, width in GLOBALS},
        "objects": E.dump_objects(o.mu),
        "cells": o.dump_cells(),
        "rng": o.rng(),
        "cursor": o.state(0x5120),
        "sound": list(o.sound_events),
    }


def apply_seed(o, seed):
    p = seed.get("player", {})
    if "x" in p: o.seed_global(0x4611, p["x"])
    if "y" in p: o.seed_global(0x4613, p["y"])
    for name, val in seed.get("globals", {}).items():
        off, width = GLOBAL_OFF[name]
        o.seed_global(off, val, width)
    for slot, fields in seed.get("objects", {}).items():
        o.seed_object(int(slot), fields)
    for col, row, off, val in seed.get("cell_bytes", []):
        raw = bytearray(bytes.fromhex(E.dump_cells(o.mu)[row][col]))
        raw[off] = val & 0xFF
        o.seed_cell(col, row, bytes(raw))
    for col, row, data in seed.get("cells", []):
        o.seed_cell(col, row, bytes.fromhex(data))
    if "rng" in seed: o.seed_rng(*seed["rng"])
    if "cursor" in seed: E.w16(o.mu, E.DATA_SEG, 0x5120, seed["cursor"])
    if "inputs" in seed:
        o.set_inputs(**seed["inputs"])


def diff(prev, cur):
    changes = {}
    for i, (a, b) in enumerate(zip(prev["objects"], cur["objects"])):
        fields = {}
        for name, _off, _byte in OBJ_FIELDS:
            if (a[name] & 0xFFFF) != (b[name] & 0xFFFF):
                fields[name] = b[name]
        if fields: changes[str(i)] = fields
    cell_changes = []
    for row in range(len(cur["cells"])):
        for col in range(len(cur["cells"][row])):
            if prev["cells"][row][col] != cur["cells"][row][col]:
                cell_changes.append([row, col, cur["cells"][row][col]])
    return changes, cell_changes


def direct(name, level, seed, x, y):
    o = PhysicsOracle(level)
    o.reset()
    apply_seed(o, seed)
    o.sound_events = []
    o.cell_event(x, y)
    st = snap(o)
    return {
        "name": name, "level": level, "mode": "cell",
        "seed": seed, "x": x, "y": y, "state": st,
    }


def frames(name, level, seed, inputs_list):
    o = PhysicsOracle(level)
    o.reset()
    apply_seed(o, seed)
    prev = snap(o)
    out = []
    for inp in inputs_list:
        o.set_inputs(**inp)
        o.sound_events = []
        o.step_full()
        cur = snap(o)
        obj_changes, cell_changes = diff(prev, cur)
        out.append({
            "input": inp, "player": cur["player"], "globals": cur["globals"],
            "rng": cur["rng"], "cursor": cur["cursor"],
            "sound": cur["sound"], "object_changes": obj_changes,
            "cell_changes": cell_changes,
        })
        prev = cur
    return {
        "name": name, "level": level, "mode": "full",
        "seed": seed, "frames": out,
    }


def main():
    scenarios = []

    # Direct cell-event branches.  Cells are seeded at (10,10) unless stated.
    c = 10
    r = 10
    base = {"player": {"x": 160, "y": 160}, "globals": {"a0cc": 14, "a0be": 1}}
    scenarios.append(direct("crystal1", 3, {**base, "cells": [[c, r, cell_hex({0x0C: 0x0D, 0x0E: 1})]]}, c*16+8, r*16+8))
    scenarios.append(direct("crystal_last", 3, {**base, "globals": {"a0cc": 1, "a0be": 1}, "cells": [[c, r, cell_hex({0x0C: 0x0D, 0x0E: 2})]]}, c*16+8, r*16+8))
    scenarios.append(direct("ammo31", 3, {**base, "cells": [[c, r, cell_hex({0x0C: 6, 0x0E: 0x31})]]}, c*16+8, r*16+8))
    scenarios.append(direct("power32", 3, {**base, "cells": [[c, r, cell_hex({0x0C: 6, 0x0E: 0x32})]]}, c*16+8, r*16+8))
    scenarios.append(direct("score26", 3, {**base, "cells": [[c, r, cell_hex({0x0C: 0x0C, 0x0E: 0x26})]]}, c*16+8, r*16+8))
    scenarios.append(direct("spawn2c", 3, {**base, "cells": [[c, r, cell_hex({0x0C: 0x0D, 0x0E: 0x2C})]]}, c*16+8, r*16+8))
    scenarios.append(direct("bonus0b", 3, {**base, "cells": [[c, r, cell_hex({0x0C: 1, 0x0E: 0x0B})]]}, c*16+8, r*16+8))
    scenarios.append(direct("powerup19", 3, {**base, "cells": [[c, r, cell_hex({0x0C: 0x0E, 0x0E: 0x19})]]}, c*16+8, r*16+8))
    scenarios.append(direct("hazard31", 3, {**base, "globals": {"a0be": 1, "513c": 1, "a0ea": 0xFFFF}, "cells": [[c, r, cell_hex({0x0C: 0x0A, 0x0E: 0x31}, {0x13: 1})]]}, c*16+8, r*16+8))
    scenarios.append(direct("hubdoor1", 2, {"player": {"x": 80, "y": 128}, "globals": {"a0be": 0}, "cells": [[5, 8, cell_hex({0x00: 1, 0x04: 22, 0x06: 0x0D, 0x08: 1, 0x0A: 1})]]}, 83, 128))
    scenarios.append(direct("caveexit", 3, {**base, "globals": {"a0be": 1, "a0f5": 1, "509e": 3, "513c": 1}, "cells": [[c, r, cell_hex({0x06: 0x0D, 0x0A: 0x0C})]]}, c*16+8, r*16+8))
    scenarios.append(direct("switch11", 3, {**base, "globals": {"a0d4": 12, "a0d6": 12}, "inputs": {"fire": True}, "cells": [[c, r, cell_hex({0x06: 0x11, 0x0A: 9})]]}, c*16+8, r*16+8))
    scenarios.append(direct("lever0f", 3, {**base, "inputs": {"fire": True}, "cells": [[c, r, cell_hex({0x06: 0x0F, 0x0A: 9})]]}, c*16+8, r*16+8))

    # Full-frame contact and transition scenarios.
    scenarios.append(frames("unlock_exit_l3", 3, {
        "globals": {"a0cc": 0, "a0be": 1, "a0f5": 0},
    }, [opts()]))
    scenarios.append(frames("crystal_contact_l3", 3, {
        "player": {"x": 152, "y": 161},
        "globals": {"a0fd": 1, "a0b8": 0, "a0cc": 14, "a0be": 1},
        "cells": [[10, 10, cell_hex({0x06: 4, 0x0A: 0x14, 0x0C: 0x0D, 0x0E: 1})]],
    }, [opts()]))
    scenarios.append(frames("hazard_contact_l3", 3, {
        "player": {"x": 155, "y": 160},
        "globals": {"513c": 1, "a0ea": 0xFFFF},
        "cells": [[10, 10, cell_hex({0x0C: 0x0A, 0x0E: 0x31}, {0x13: 1})]],
    }, [opts()]))
    scenarios.append(frames("cave_exit_l3", 3, {
        "player": {"x": 37 * 16 + 3, "y": 17 * 16},
        "globals": {"a0be": 1, "a0f5": 1, "509e": 3, "513c": 1},
    }, [opts() for _ in range(17)]))

    payload = {
        "exe_sha256": "",
        "globals": [[name, off] for name, off, _w in GLOBALS],
        "object_fields": [[name, off] for name, off, _b in OBJ_FIELDS],
        "levels": {},
        "scenarios": scenarios,
    }
    for lv in sorted({s["level"] for s in scenarios}):
        o = PhysicsOracle(lv)
        o.reset()
        payload["levels"][str(lv)] = snap(o)
    out = os.path.join(ROOT, "notes", "cell_fixtures.json")
    with open(out, "w") as f:
        json.dump(payload, f, separators=(",", ":"))
    print("wrote", out, "scenarios:", len(scenarios))


if __name__ == "__main__":
    main()
