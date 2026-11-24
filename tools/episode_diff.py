#!/usr/bin/env python3
"""Compare two decompressed Crystal Caves episode EXEs and their data assets.

Spike tool for the Episode 2/3 port: builds an address-correspondence map
between two sibling LZEXE-unpacked builds, maps known Episode-1 landmarks into
the other image, and reports changed regions and data-asset deltas.

The two EXEs are near-identical relinks, so the bulk of the load module matches
byte-for-byte once MZ relocation words are masked (a relocated word holds a
segment value that legitimately differs between episodes).  Everything that
does not match is either genuinely changed code/data or data that moved; the
report tells which, per region.

Only structural facts (offsets, sizes, run lengths) are emitted -- no game
content is copied or committed.

Usage:
  python3 tools/episode_diff.py CC1-UNLZ.EXE CC2-UNLZ.EXE \
      [--assets-a extracted] [--assets-b extracted/ep2] \
      [--prefix-a CC1] [--prefix-b CC2] [--json notes/ep2_diff.json]
"""

import json
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OBJ_JSON = os.path.join(ROOT, "out", "portable", "objects.json")

# Episode-1 landmarks (load-module offsets, i.e. file offset - header size).
# Sources: AGENTS.md "Key Conventions & Quirks" and notes/gameplay.md.
LANDMARKS = {
    0x0C3E: "startup (0xC3E)",
    0x0B1A9: "setup_level loader (0xB1A9)",
    0xCF18: "level-start sound (0xCF18)",
    0xCF6C: "player fire (0xCF6C)",
    0xD72A: "object update loop (0xD72A)",
    0xE011: "frame step 1 (0xE011)",
    0xE36B: "frame step 2 (0xE36B)",
    0xE5D3: "hazard contact (0xE5D3)",
    0xEBF2: "frame step 4 (0xEBF2)",
    0xEAEA: "status timers (0xEAEA)",
    0xEFCF: "level reload clear (0xEFCF)",
    0x1B776: "cell event (0x0FC5:0xBB26)",
    0x1FBBF: "sound driver (0x1F9A:0x021F)",
    0x203DF: "sound ISR (0x1F9A:0x0A3F)",
    0x1FCB0: "anim_next helper",
    0x1FCF7: "anim_select_shape helper",
    0x2177B: "player_proximity_test helper",
    0x2C4D0: "TP RNG (0x2BB5:0x0980)",
}

WIN = 16          # signature window for block matching
MIN_RUN = 32      # ignore matched runs shorter than this
MAX_CANDIDATES = 48
GFX_BLOCK = 8003
SND_RECORD = 610


def parse_mz(path):
    data = open(path, "rb").read()
    (cblp, cp, crlc, cparhdr, minalloc, maxalloc,
     ss, sp, csum, ip, cs, lfarlc) = struct.unpack_from("<12H", data, 2)
    hdr = cparhdr * 16
    mod = bytearray(data[hdr:])
    relocs = []
    for i in range(crlc):
        off, seg = struct.unpack_from("<HH", data, lfarlc + 4 * i)
        relocs.append(seg * 16 + off)
    masked = bytearray(mod)
    for r in relocs:
        if 0 <= r <= len(masked) - 2:
            masked[r:r + 2] = b"\x00\x00"
    info = {
        "file": path,
        "file_size": len(data),
        "header_size": hdr,
        "load_size": len(mod),
        "relocs": crlc,
        "entry": "%04X:%04X" % (cs, ip),
        "stack": "%04X:%04X" % (ss, sp),
        "min_alloc": minalloc,
        "max_alloc": maxalloc,
    }
    return info, mod, masked


def window_ok(w):
    """Skip windows that are all one byte value (BSS/padding noise)."""
    return len(set(w)) > 1


def build_runs(a, b):
    """Rolling-window match: list of (a_start, b_start, length) runs."""
    index = {}
    for i in range(len(a) - WIN + 1):
        w = bytes(a[i:i + WIN])
        if not window_ok(w):
            continue
        bucket = index.get(w)
        if bucket is None:
            index[w] = [i]
        elif len(bucket) < MAX_CANDIDATES:
            bucket.append(i)

    raw = []
    i = 0
    n = len(b) - WIN + 1
    while i < n:
        w = bytes(b[i:i + WIN])
        if not window_ok(w):
            i += 1
            continue
        bucket = index.get(w)
        if not bucket:
            i += 1
            continue
        best = None
        for j in bucket:
            if a[j:j + WIN] != w:
                continue
            back = 0
            while j - back - 1 >= 0 and i - back - 1 >= 0 \
                    and a[j - back - 1] == b[i - back - 1]:
                back += 1
            fwd = WIN
            while j + fwd < len(a) and i + fwd < len(b) \
                    and a[j + fwd] == b[i + fwd]:
                fwd += 1
            run = (j - back, i - back, back + fwd)
            if best is None or run[2] > best[2]:
                best = run
        if best and best[2] >= MIN_RUN:
            raw.append(best)
            i = max(i + 1, best[1] + best[2])
        else:
            i += 1

    # Greedy longest-first de-overlap, then order by A position.
    raw.sort(key=lambda r: r[2], reverse=True)
    keep = []
    for r in raw:
        a0, b0, ln = r
        clash = False
        for k in keep:
            ka, kb, kl = k
            if a0 < ka + kl and ka < a0 + ln and b0 < kb + kl and kb < b0 + ln:
                clash = True
                break
        if not clash:
            keep.append(r)
    keep.sort(key=lambda r: r[0])
    return keep


def run_for(runs, off):
    for a0, b0, ln in runs:
        if a0 <= off < a0 + ln:
            return a0, b0, ln
    return None


def map_landmark(runs, masked_a, masked_b, off):
    found = run_for(runs, off)
    if found is None:
        # Allow a small slack (landmark at a run boundary).
        best = None
        for a0, b0, ln in runs:
            d = min(abs(off - a0), abs(off - (a0 + ln)))
            if d <= 64 and (best is None or d < best[0]):
                best = (d, a0, b0, ln)
        if best is None:
            return None
        _, a0, b0, ln = best
        mapped = off - a0 + b0
        exact = masked_a[off:off + 12] == masked_b[mapped:mapped + 12]
        return mapped, ln, exact
    a0, b0, ln = found
    mapped = off - a0 + b0
    exact = masked_a[off:off + 12] == masked_b[mapped:mapped + 12]
    return mapped, ln, exact


def covered_len(runs):
    """Total matched bytes in A (union: runs may overlap in A)."""
    intervals = sorted((a0, a0 + ln) for a0, b0, ln in runs)
    merged = []
    for lo, hi in intervals:
        if merged and lo <= merged[-1][1]:
            merged[-1][1] = max(merged[-1][1], hi)
        else:
            merged.append([lo, hi])
    return sum(hi - lo for lo, hi in merged)


def gaps(runs, size):
    """Unmatched A ranges.  Match runs may overlap in A (different B
    positions), so merge the covered intervals first."""
    intervals = sorted((a0, a0 + ln) for a0, b0, ln in runs)
    merged = []
    for lo, hi in intervals:
        if merged and lo <= merged[-1][1]:
            merged[-1][1] = max(merged[-1][1], hi)
        else:
            merged.append([lo, hi])
    out = []
    prev = 0
    for lo, hi in merged:
        if lo > prev:
            out.append((prev, lo - prev))
        prev = max(prev, hi)
    if prev < size:
        out.append((prev, size - prev))
    return out


def load_handler_landmarks():
    try:
        table = json.load(open(OBJ_JSON))["handler_table"]
    except (OSError, KeyError, ValueError):
        return {}
    return {0x1F9A0 + off: "handler h_%04X" % off for off in table}


def canonical(name, prefix):
    if name.upper().startswith(prefix.upper()):
        return "CCx" + name[len(prefix):]
    return name


def diff_bytes(a, b):
    limit = min(len(a), len(b))
    return sum(1 for i in range(limit) if a[i] != b[i])


def compare_assets(dir_a, prefix_a, dir_b, prefix_b):
    files_a = {canonical(f, prefix_a): f
               for f in sorted(os.listdir(dir_a))
               if os.path.isfile(os.path.join(dir_a, f))}
    files_b = {canonical(f, prefix_b): f
               for f in sorted(os.listdir(dir_b))
               if os.path.isfile(os.path.join(dir_b, f))}
    report = {"only_a": sorted(set(files_a) - set(files_b)),
              "only_b": sorted(set(files_b) - set(files_a)),
              "files": []}
    for key in sorted(set(files_a) & set(files_b)):
        pa = os.path.join(dir_a, files_a[key])
        pb = os.path.join(dir_b, files_b[key])
        a = open(pa, "rb").read()
        b = open(pb, "rb").read()
        rec = {"name": key, "size_a": len(a), "size_b": len(b)}
        if len(a) == len(b):
            rec["diff_bytes"] = diff_bytes(a, b)
            rec["identical"] = rec["diff_bytes"] == 0
            if key.endswith(".GFX") and len(a) % GFX_BLOCK == 0:
                blocks = []
                for i in range(0, len(a), GFX_BLOCK):
                    d = diff_bytes(a[i:i + GFX_BLOCK], b[i:i + GFX_BLOCK])
                    blocks.append({"block": i // GFX_BLOCK, "diff_bytes": d})
                rec["gfx_blocks"] = blocks
            if key.endswith(".SND") and len(a) % SND_RECORD == 0:
                recs = []
                for i in range(0, len(a), SND_RECORD):
                    d = diff_bytes(a[i:i + SND_RECORD], b[i:i + SND_RECORD])
                    recs.append({"record": i // SND_RECORD, "diff_bytes": d})
                rec["snd_records"] = recs
        else:
            rec["identical"] = False
        report["files"].append(rec)
    return report


def fmt_off(off):
    if 0x1F9A0 <= off < 0x1F9A0 + 0x8000:
        return "1F9A:%04X" % (off - 0x1F9A0)
    return "%05X" % off


def main(argv):
    if len(argv) < 3:
        raise SystemExit(__doc__)
    path_a, path_b = argv[1], argv[2]
    assets_a = assets_b = None
    prefix_a, prefix_b = "CC1", "CC2"
    out_json = None
    i = 3
    while i < len(argv):
        opt = argv[i]
        if opt == "--assets-a":
            assets_a = argv[i + 1]; i += 2
        elif opt == "--assets-b":
            assets_b = argv[i + 1]; i += 2
        elif opt == "--prefix-a":
            prefix_a = argv[i + 1]; i += 2
        elif opt == "--prefix-b":
            prefix_b = argv[i + 1]; i += 2
        elif opt == "--json":
            out_json = argv[i + 1]; i += 2
        else:
            raise SystemExit("unknown option: " + opt)

    info_a, mod_a, masked_a = parse_mz(path_a)
    info_b, mod_b, masked_b = parse_mz(path_b)
    runs = build_runs(bytes(masked_a), bytes(masked_b))

    covered_a = covered_len(runs)
    covered_b = covered_a
    landmarks = dict(LANDMARKS)
    landmarks.update(load_handler_landmarks())

    mapped = []
    for off in sorted(landmarks):
        got = map_landmark(runs, masked_a, masked_b, off)
        rec = {"name": landmarks[off], "a_off": off}
        if got is None:
            rec.update({"b_off": None, "delta": None, "run": 0,
                        "exact": False})
        else:
            b_off, run, exact = got
            rec.update({"b_off": b_off, "delta": b_off - off, "run": run,
                        "exact": exact})
        mapped.append(rec)

    gap_a = sorted(gaps(runs, len(mod_a)), key=lambda g: g[1], reverse=True)
    gap_b = sorted(gaps(runs, len(mod_b)), key=lambda g: g[1], reverse=True)

    report = {
        "exe_a": info_a,
        "exe_b": info_b,
        "coverage": {"matched_a": covered_a,
                     "matched_a_pct": round(100.0 * covered_a / len(mod_a), 2),
                     "runs": len(runs)},
        "runs": [{"a": a0, "b": b0, "len": ln, "delta": b0 - a0}
                 for a0, b0, ln in runs],
        "landmarks": mapped,
        "gaps_a": [{"off": o, "len": ln} for o, ln in gap_a],
        "gaps_b": [{"off": o, "len": ln} for o, ln in gap_b],
    }
    if assets_a and assets_b:
        report["assets"] = compare_assets(assets_a, prefix_a,
                                          assets_b, prefix_b)

    print("A: %s  (%d B load, %d relocs, entry %s)"
          % (os.path.basename(path_a), info_a["load_size"],
             info_a["relocs"], info_a["entry"]))
    print("B: %s  (%d B load, %d relocs, entry %s)"
          % (os.path.basename(path_b), info_b["load_size"],
             info_b["relocs"], info_b["entry"]))
    print("matched: %d B (%.2f%%) in %d runs"
          % (covered_a, 100.0 * covered_a / len(mod_a), len(runs)))
    print()
    print("%-34s %10s %10s %8s %7s" % ("landmark", "A", "B", "delta", "run"))
    for rec in mapped:
        b_off = rec["b_off"]
        print("%-34s %10s %10s %8s %7d%s"
              % (rec["name"], fmt_off(rec["a_off"]),
                 fmt_off(b_off) if b_off is not None else "-",
                 "%+d" % rec["delta"] if rec["delta"] is not None else "-",
                 rec["run"], "" if rec["exact"] else "  (moved/edited)"))
    print()
    print("largest unmatched regions in A:")
    for off, ln in gap_a[:12]:
        print("  %s  %6d B" % (fmt_off(off), ln))
    print("largest unmatched regions in B:")
    for off, ln in gap_b[:12]:
        print("  %s  %6d B" % (fmt_off(off), ln))
    if "assets" in report:
        print()
        print("%-12s %9s %9s %9s" % ("asset", "size A", "size B", "diff B"))
        for rec in report["assets"]["files"]:
            print("%-12s %9d %9d %9s"
                  % (rec["name"], rec["size_a"], rec["size_b"],
                     rec.get("diff_bytes", "size")))
        if report["assets"]["only_a"]:
            print("only in A:", ", ".join(report["assets"]["only_a"]))
        if report["assets"]["only_b"]:
            print("only in B:", ", ".join(report["assets"]["only_b"]))

    if out_json:
        with open(out_json, "w") as fh:
            json.dump(report, fh, indent=1)
        print("\nwrote", out_json)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
