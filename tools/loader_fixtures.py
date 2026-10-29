#!/usr/bin/env python3
"""Generate exact transition fixtures from the real loader.

Scenarios poison the source level's object/cell bytes and gameplay globals,
then run the destination loader.  The committed fixtures include the full
final object/cell/global state so the C loader can be checked byte-for-byte.
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

import emulate_load as E  # noqa: E402
from combat_fixtures import C_GLOBALS  # noqa: E402
from emulate_startup import GLOBALS as SGLOBALS  # noqa: E402

GLOBAL_OFF = {name: (off, width) for name, off, width in SGLOBALS}


def mutate(mu):
    E.fill_source(mu, 0xA5, 0xA5)
    # Sentinels chosen to survive any single-byte clobber and be obvious.
    E.w16(mu, E.DATA_SEG, 0x50A5, 0x1234)
    E.w16(mu, E.DATA_SEG, 0x50A7, 0x9ABC)
    E.w16(mu, E.DATA_SEG, 0x509C, 7)
    E.w16(mu, E.DATA_SEG, 0x509E, 2)
    E.w16(mu, E.DATA_SEG, 0x50A2, 9)
    E.w16(mu, E.DATA_SEG, 0x50A0, 0x7D00)
    E.w16(mu, E.DATA_SEG, 0x50BE, 7)
    E.w16(mu, E.DATA_SEG, 0x50C4, 9)
    E.w16(mu, E.DATA_SEG, 0x50C7, 0x1234)
    E.w16(mu, E.DATA_SEG, 0x50C9, 0x9ABC)
    E.w16(mu, E.DATA_SEG, 0x02A6, 0x1111)
    E.w16(mu, E.DATA_SEG, 0x02A8, 0x2222)
    E.w16(mu, E.DATA_SEG, 0x5120, 42)
    for i in range(1, 17):
        mu.mem_write((E.DATA_SEG << 4) + 0x50A8 + i, bytes([i & 1]))


def main():
    scenarios = []
    for dest in range(3, 19):
        r = E.run_transition(2, dest, prefill=True, mutate=mutate,
                             trace_writes=False)
        if r["stats"]["stopped"] not in ("ret", "load_end"):
            raise RuntimeError("transition 2->%d stopped: %s" % (dest, r["stats"]["stopped"]))
        scenarios.append({
            "name": "hub_to_L%d" % dest, "source": 2, "dest": dest,
            "arg": r["arg"], "objects": r["objects"], "cells": r["cells"],
            "cursor": r["cursor"], "globals": r["globals"], "rng": r["rng"],
        })
    payload = {
        "object_fields": [[n, o] for n, o, _k in E.OBJ_FIELDS],
        "globals": [[n, off] for n, (off, _w) in GLOBAL_OFF.items()
                    if n in C_GLOBALS],
        "scenarios": scenarios,
    }
    out = os.path.join(ROOT, "notes", "loader_fixtures.json")
    with open(out, "w") as f:
        json.dump(payload, f, separators=(",", ":"))
    print("wrote", out, "scenarios:", len(scenarios))


if __name__ == "__main__":
    main()
