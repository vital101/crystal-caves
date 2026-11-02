#!/usr/bin/env python3
"""Scan wasm/objects.c and inventory top-level declarations with line ranges.

This is a one-off analysis helper for the Phase 1 split. It is not part of the
build. It walks the file looking for top-level definitions (column 0) and
reports function bodies, typedefs, macros and file-scope variable declarations.

NOTE: `wasm/objects.c` was removed from the tree after the split.  Restore it
from git history (see refactor/split_objects.py) if you need to re-run this.
"""
import re
import sys

PATH = sys.argv[1] if len(sys.argv) > 1 else "wasm/objects.c"

with open(PATH, "r", encoding="utf-8") as f:
    lines = f.readlines()

# Function definition at column 0: [static] TYPE NAME(args) { ... }
# Body ends at a column-0 "}".
fn_re = re.compile(r"^(static\s+)?([A-Za-z_][\w \t\*]*?)\b([A-Za-z_]\w*)\s*\(([^;]*)\)\s*\{\s*$")

i = 0
items = []
n = len(lines)
while i < n:
    line = lines[i]
    if line.startswith("/*"):
        # comment block
        start = i
        while i < n and "*/" not in lines[i]:
            i += 1
        i += 1
        items.append(("comment", start + 1, i, lines[start].strip()))
        continue
    m = fn_re.match(line)
    if m:
        start = i
        depth = 0
        while i < n:
            depth += lines[i].count("{") - lines[i].count("}")
            if depth == 0 and "}" in lines[i] and i > start:
                break
            i += 1
        i += 1
        items.append(("func", start + 1, i, m.group(3)))
        continue
    i += 1

for kind, a, b, name in items:
    print(f"{kind:8s} {a:5d}-{b:5d}  ({b-a+1:4d})  {name}")
