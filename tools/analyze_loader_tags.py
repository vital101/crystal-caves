#!/usr/bin/env python3
"""Regenerate notes/loader_tags.json (source-dependent loader writes).

Runs each destination loader from two source levels with different carried
globals/RNG state, diffs the final cell/object bytes, and tags the entries that
must be replayed at runtime (map-command shapes read from [0x2d6], and
RNG-selected spawn fields).  This is slow (~15 min); the resulting JSON is
committed so normal builds do not need Unicorn.
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

import emulate_load as E  # noqa: E402

CELL_LO = E.CELL_BASE
CELL_HI = CELL_LO + 40 * E.CELL_COL_STRIDE + 25 * E.CELL_ROW_STRIDE + 20
OBJ_LO = E.OBJ_BASE
OBJ_HI = OBJ_LO + E.OBJ_SLOTS * E.OBJ_STRIDE
CAND = ["2b8", "2cc", "2ce", "2d4", "2d6", "2d8", "2fa", "2fc", "2fe",
        "260", "511e", "5122", "50dc", "50de", "50e0", "a0f0", "a0fa"]
SOURCES = (2, 5)


def step(lo, hi):
    ax = lo
    bx = hi
    cx = (ax << 3) & 0xFFFF
    prod = ax * 0x8405
    dx = (prod >> 16) & 0xFFFF
    ax = prod & 0xFFFF
    ch = ((cx >> 8) + (cx & 0xFF)) & 0xFF
    cx = ((ch << 8) | (cx & 0xFF)) & 0xFFFF
    dx = (dx + cx + bx) & 0xFFFF
    bx = (bx << 2) & 0xFFFF
    dx = (dx + bx) & 0xFFFF
    dh = ((dx >> 8) + (bx & 0xFF)) & 0xFF
    dx = ((dh << 8) | (dx & 0xFF)) & 0xFFFF
    bx = (bx << 5) & 0xFFFF
    dh = ((dx >> 8) + (bx & 0xFF)) & 0xFF
    dx = ((dh << 8) | (dx & 0xFF)) & 0xFFFF
    ax = (ax + 1) & 0xFFFF
    if ax == 0:
        dx = (dx + 1) & 0xFFFF
    return ax, dx, dx


def draw_results(src, ns):
    lo, hi = src["rng"]
    out = []
    for n in ns:
        lo, hi, val = step(lo, hi)
        out.append(val % n if n else 0)
    return out


def run(dest, src):
    mod = E.load_module()
    mu = E.make_machine(mod, startup=False)
    mu.mem_write(E.DATA_SEG << 4, src["ds"])
    st, _sp, _w = E.run_loader(mu, mod, E.level_arg(dest), trace_writes=True)
    finals = {}
    meta = {}
    for off, size, val, rc, retip, args in _w:
        if OBJ_LO <= off < OBJ_HI or CELL_LO <= off < CELL_HI:
            for i in range(size):
                finals[off + i] = (val >> (8 * i)) & 0xFF
                meta[off + i] = (rc, retip, args)
    return finals, meta, st


def main():
    srcs = {}
    for lv in SOURCES:
        mod = E.load_module()
        mu = E.make_machine(mod, True)
        E.run_loader(mu, mod, E.level_arg(lv))
        ds = bytes(mu.mem_read(E.DATA_SEG << 4, 0x10000))
        vals = {name: int.from_bytes(ds[int(name, 16):int(name, 16) + 2],
                                     "little") for name in CAND}
        srcs[lv] = {"ds": ds, "vals": vals,
                    "rng": (int.from_bytes(ds[0x2A6:0x2A8], "little"),
                            int.from_bytes(ds[0x2A8:0x2AA], "little"))}
    out = {}
    for dest in range(3, 19):
        f = {}
        m = {}
        st = {}
        for lv in SOURCES:
            f[lv], m[lv], st[lv] = run(dest, srcs[lv])
        ns = st[SOURCES[0]]["rng_calls"]
        draws = {lv: draw_results(srcs[lv], ns) for lv in SOURCES}
        diff = [k for k in set(f[2]) | set(f[5]) if f[2].get(k) != f[5].get(k)]
        entries = []
        for k in diff:
            vals = (f[2].get(k), f[5].get(k))
            rc, retip, args = m[2].get(k, (0, 0, ()))
            tag = None
            if rc > 0 and rc - 1 < len(ns):
                di = rc - 1
                n = ns[di]
                if n:
                    add = (vals[0] - draws[2][di]) & 0xFF
                    if add == ((vals[1] - draws[5][di]) & 0xFF):
                        tag = ["rng", di, n, add]
            if tag is None:
                for name in CAND:
                    if (srcs[2]["vals"][name] & 0xFF,
                            srcs[5]["vals"][name] & 0xFF) == vals:
                        tag = ["g", name, 0]
                        break
                    if ((srcs[2]["vals"][name] >> 8) & 0xFF,
                            (srcs[5]["vals"][name] >> 8) & 0xFF) == vals:
                        tag = ["g", name, 1]
                        break
            if tag is None:
                raise SystemExit("unclassified source-dependent write %#x on level %d: %r"
                                 % (k, dest, (vals, retip, args)))
            entries.append([k] + tag)
        out[str(dest)] = entries
        print("level", dest, "entries", len(entries), flush=True)
    path = os.path.join(ROOT, "notes", "loader_tags.json")
    with open(path, "w") as fh:
        json.dump(out, fh, indent=1)
    print("wrote", path)


if __name__ == "__main__":
    main()
