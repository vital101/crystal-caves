#!/usr/bin/env python3
"""Mechanically split wasm/objects.c into wasm/game/*.c modules.

Phase 1 of refactor/phase-1-gameplay-split.md.  This script is one-off tooling:
it moves contiguous ranges of the original file into cohesive modules, strips
the `S` macro, and emits an internal umbrella header with the shared typedefs,
macros, extern globals and prototypes.

NOTE: the original `wasm/objects.c` was removed from the tree after the split.
To re-run this script, restore it first, e.g.:
    git show <phase-0-rev>:wasm/objects.c > wasm/objects.c
Then run the script and remove the file again.

No function bodies are rewritten; only mechanical transforms are applied:
  * `S->` becomes `g_state.`
  * the `#define S (&g_state)` line is dropped
  * bare `S` pointer expressions become `(&g_state)`
  * file-scope `static` is removed so cross-module calls link
  * generated data headers and shared types move to game_internal.h
"""
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "wasm", "objects.c")
OUTDIR = os.path.join(ROOT, "wasm", "game")

# Ranges are 1-based inclusive line numbers in the original file.
MODULES = {
    "state.c": [(1, 346)],
    "physics.c": [(347, 849)],
    "handlers_platforms.c": [(850, 1264)],
    "combat.c": [(1265, 1708)],
    "handlers_interaction.c": [(1709, 2084)],
    "cell_event.c": [(2085, 2619)],
    "handlers_actors_a.c": [(2620, 3066)],
    "handlers_actors_b.c": [(3067, 3550)],
    "update.c": [(3551, 3927)],
    "update_death.c": [(3928, 4205)],
    "level.c": [(4206, 4669)],
    "api_accessors.c": [(4670, 4779), (5112, 5223)],
    "api_globals.c": [(4780, 5033)],
    "rng.c": [(5034, 5078)],
    "handlers_table.c": [(5079, 5111)],
    "save_state.c": [(5224, 5370)],
}

# Regions that go into game_internal.h instead of a module.
HEADER_RANGES = [
    (1, 35),      # file comment
    (37, 42),     # includes
    (52, 54),     # OBJ_MAX / SCREEN_* macros
    (56, 223),    # cc_state_t typedef + comment
    (235, 235),   # CC_OFFSETOF macro
    (1196, 1202), # SpawnArgs typedef
    (2085, 2085), # handler_fn typedef
]

with open(SRC, "r", encoding="utf-8") as f:
    original = f.readlines()


def in_ranges(n, ranges):
    return any(a <= n <= b for a, b in ranges)


def module_lines(ranges):
    out = []
    for a, b in ranges:
        for n in range(a, b + 1):
            if not in_ranges(n, HEADER_RANGES):
                out.append((n, original[n - 1]))
    return out


def strip_comments_and_strings(text):
    """Remove comments and string/char literals so brace counting is reliable."""
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    text = re.sub(r'"(?:\\.|[^"\\])*"', '""', text)
    text = re.sub(r"'(?:\\.|[^'\\])*'", "''", text)
    return text


def find_functions(lines_with_no):
    """Return (name, signature) for top-level function definitions."""
    text = "".join(t for _, t in lines_with_no)
    clean = strip_comments_and_strings(text)
    # Map cleaned offsets back to original text by tracking line lengths.
    # Simpler: scan line by line on the cleaned lines.
    clean_lines = []
    pos = 0
    for n, line in lines_with_no:
        cl = strip_comments_and_strings(line)
        clean_lines.append((n, cl))
    funcs = []
    i = 0
    nlines = len(clean_lines)
    while i < nlines:
        _, cl = clean_lines[i]
        stripped = cl.strip()
        if not stripped or stripped[0] in ("/", "*", "#", "}"):
            i += 1
            continue
        # Accumulate until we see { or ; at top level.
        sig = cl
        j = i
        while "{" not in sig and ";" not in sig and j + 1 < nlines:
            j += 1
            sig += clean_lines[j][1]
        if ";" in sig and "{" not in sig:
            i = j + 1
            continue
        sig_clean = " ".join(sig.split("{", 1)[0].split())
        m = re.search(r"([A-Za-z_]\w*)\s*\(", sig_clean)
        if m:
            funcs.append((m.group(1), sig_clean + ";"))
        # Skip body by brace counting on cleaned text.
        depth = 0
        started = False
        k = i
        while k < nlines:
            t = clean_lines[k][1]
            depth += t.count("{") - t.count("}")
            if "{" in t:
                started = True
            if started and depth <= 0:
                break
            k += 1
        i = k + 1
    return funcs


def transform(text):
    text = text.replace("S->", "g_state.")
    # Remove the S macro definition before replacing bare S.
    text = re.sub(r"^\s*#define S\s+\(&g_state\)\s*\n", "", text, flags=re.M)
    # Bare S is used as a pointer expression (e.g. memcpy_(..., S, ...)).
    text = re.sub(r"\bS\b", "(&g_state)", text)
    return text


def strip_static(text):
    return re.sub(r"^static\s+", "", text, flags=re.M)


def main():
    os.makedirs(OUTDIR, exist_ok=True)

    module_texts = {}
    all_prototypes = []
    for name, ranges in MODULES.items():
        mod = module_lines(ranges)
        raw = "".join(t for _, t in mod)
        raw = transform(raw)
        raw = strip_static(raw)
        module_texts[name] = raw
        for fn, sig in find_functions(mod):
            all_prototypes.append(sig)

    seen = set()
    protos = []
    for p in all_prototypes:
        p = strip_static(p).strip()
        if not p or "/*" in p or "*/" in p:
            continue
        if p in seen:
            continue
        seen.add(p)
        protos.append(p)

    hdr = []
    hdr.append("/* Generated by refactor/split_objects.py - Phase 1 gameplay split.")
    hdr.append(" * Internal umbrella header.  Declares shared types, generated-data")
    hdr.append(" * includes, globals and cross-module prototypes.  Replace with")
    hdr.append(" * per-module headers in a later phase.")
    hdr.append(" */")
    hdr.append("#ifndef GAME_INTERNAL_H")
    hdr.append("#define GAME_INTERNAL_H")
    hdr.append("")
    hdr.append('#include "objects.h"')
    hdr.append('#include "objects_data.h"')
    hdr.append('#include "cells_data.h"')
    hdr.append('#include "startup_data.h"')
    hdr.append('#include "shell.h"')
    hdr.append('#include "sound.h"')
    hdr.append("")
    hdr.append("#define OBJ_MAX 60")
    hdr.append("#define SCREEN_W 320")
    hdr.append("#define SCREEN_H 200")
    hdr.append("#define CC_OFFSETOF(type, field) __builtin_offsetof(type, field)")
    hdr.append("")
    hdr.extend(l.rstrip("\n") for l in original[56 - 1:223])
    hdr.append("")
    hdr.extend(l.rstrip("\n") for l in original[1196 - 1:1202])
    hdr.append("")
    hdr.extend(l.rstrip("\n") for l in original[2085 - 1:2085])
    hdr.append("")
    hdr.append("/* Shared file-scope state.  Definitions live in the owning module. */")
    hdr.append("extern cc_state_t g_state;")
    hdr.append("extern const unsigned char *g_cells_asset;")
    hdr.append("extern void (*g_sound_callback)(uint16_t id);")
    hdr.append("extern uint16_t g_sound_log[256];")
    hdr.append("extern uint16_t g_sound_log_count;")
    hdr.append("extern uint32_t g_sound_log_total;")
    hdr.append("extern uint32_t g_unknown_handler_hits;")
    hdr.append("extern const unsigned char *g_level_overlay;")
    hdr.append("extern cc_state_t g_load_tmp;")
    hdr.append("extern int g_state_error;")
    hdr.append("")
    hdr.append("/* Cross-module prototypes. */")
    hdr.extend(protos)
    hdr.append("")
    hdr.append("#endif /* GAME_INTERNAL_H */")

    with open(os.path.join(OUTDIR, "game_internal.h"), "w", encoding="utf-8") as f:
        f.write("\n".join(hdr) + "\n")

    for name, raw in module_texts.items():
        path = os.path.join(OUTDIR, name)
        with open(path, "w", encoding="utf-8") as f:
            f.write('/* Crystal Caves gameplay module - split from wasm/objects.c. */\n')
            f.write('#include "game_internal.h"\n\n')
            f.write(raw)
        print("wrote", path, len(raw.splitlines()), "lines")

    print("wrote", os.path.join(OUTDIR, "game_internal.h"), len(hdr), "lines")
    print("prototypes:", len(protos))


if __name__ == "__main__":
    main()
