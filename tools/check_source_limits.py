#!/usr/bin/env python3
"""Source-size guardrail for the Crystal Caves C/WASM refactor.

Rules (refactor/phase-0-safety-net.md section 0.4):

* fail if a hand-written ``wasm/**/*.c`` exceeds 600 lines;
* warn above 500 lines;
* fail if a function body exceeds 120 lines;
* ignore generated headers (they are never ``.c`` files, but the allowlist
  documents the generated artefacts explicitly).

Phase 0 deliberately runs warning-only: the current large files are expected
to fail until Phases 1/2 split them.  ``--strict`` flips the exit status and is
what CI should use once the split is complete.

Usage:
    python3 tools/check_source_limits.py            # report, exit 0
    python3 tools/check_source_limits.py --strict   # report, exit 1 on fail
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WASM = ROOT / "wasm"

MAX_FILE_LINES = 600
WARN_FILE_LINES = 500
MAX_FUNCTION_LINES = 120

# Generated headers are data, not hand-written code.  They are listed here so
# the allowlist is explicit; the scanner only walks .c files.
GENERATED_HEADERS = {
    "cells_data.h",
    "objects_data.h",
    "sound_data.h",
    "startup_data.h",
    "shell_text_data.h",
    "shell_exact_data.h",
}


def strip_comments_and_literals(src: str) -> str:
    """Blank comments/strings while preserving every newline and byte offset.

    Keeping newlines preserves line numbers; replacing removed text with
    spaces keeps brace positions aligned with the original file.
    """
    out: list[str] = []
    i = 0
    n = len(src)
    while i < n:
        c = src[i]
        if c == "/" and i + 1 < n and src[i + 1] == "/":
            out.append(" ")
            out.append(" ")
            i += 2
            while i < n and src[i] != "\n":
                out.append(" ")
                i += 1
        elif c == "/" and i + 1 < n and src[i + 1] == "*":
            out.append(" ")
            out.append(" ")
            i += 2
            while i < n and not (src[i] == "*" and i + 1 < n and src[i + 1] == "/"):
                out.append("\n" if src[i] == "\n" else " ")
                i += 1
            if i < n:
                out.append(" ")
                out.append(" ")
                i += 2
        elif c in ('"', "'"):
            quote = c
            out.append(" ")
            i += 1
            while i < n:
                if src[i] == "\\" and i + 1 < n:
                    out.append(" ")
                    out.append("\n" if src[i + 1] == "\n" else " ")
                    i += 2
                    continue
                if src[i] == quote:
                    out.append(" ")
                    i += 1
                    break
                out.append("\n" if src[i] == "\n" else " ")
                i += 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


def strip_preprocessor(src: str) -> str:
    """Blank preprocessor directives (keeping newlines) so macro braces do not
    look like top-level function bodies."""
    lines = src.splitlines(keepends=True)
    out = []
    for line in lines:
        if line.lstrip().startswith("#"):
            out.append("".join("\n" if ch == "\n" else " " for ch in line))
        else:
            out.append(line)
    return "".join(out)


def line_of(src: str, index: int) -> int:
    return src.count("\n", 0, index) + 1


def is_function_prefix(prefix: str) -> bool:
    p = prefix.strip()
    if not p or ")" not in p:
        return False
    if p.startswith("#"):
        return False
    if re.match(r"^(typedef|struct|union|enum)\b", p):
        return False
    # An initializer brace follows '=', a function body does not.
    if "=" in p and p.rfind("=") > p.rfind(")"):
        return False
    return True


def function_name(prefix: str) -> str:
    p = prefix.strip()
    # Use the last parenthesised identifier before the body.
    closes = [m.start() for m in re.finditer(r"\)", p)]
    if not closes:
        return "<anonymous>"
    before = p[:closes[-1]]
    # Skip attribute lists: __attribute__((...)) leaves its inner ')' last.
    before = re.sub(r"__attribute__\s*\(\(.*?\)\)\s*$", "", before)
    opens = [m.start() for m in re.finditer(r"\(", before)]
    if not opens:
        return "<anonymous>"
    head = before[:opens[-1]].rstrip()
    m = re.search(r"([A-Za-z_][A-Za-z0-9_]*)\s*$", head)
    return m.group(1) if m else "<anonymous>"


def scan_functions(src: str) -> list[tuple[str, int, int]]:
    """Return (name, first_line, last_line) for every top-level C function."""
    cleaned = strip_preprocessor(strip_comments_and_literals(src))
    functions: list[tuple[str, int, int]] = []
    depth = 0
    segment_start = 0
    body_start: int | None = None
    start_line = 0
    name = ""

    for i, ch in enumerate(cleaned):
        if ch == "{":
            if depth == 0:
                prefix = cleaned[segment_start:i]
                if is_function_prefix(prefix):
                    body_start = i
                    start_line = line_of(cleaned, segment_start)
                    name = function_name(prefix)
            depth += 1
        elif ch == "}":
            if depth == 1 and body_start is not None:
                functions.append((name, start_line, line_of(cleaned, i)))
                body_start = None
            if depth > 0:
                depth -= 1
            if depth == 0:
                segment_start = i + 1
        elif ch == ";" and depth == 0:
            segment_start = i + 1

    return functions


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--strict", action="store_true",
                        help="exit 1 when any hard limit is exceeded")
    args = parser.parse_args()

    sources = sorted(p for p in WASM.rglob("*.c")
                     if "node_modules" not in p.parts)
    if not sources:
        print(f"no .c sources under {WASM}", file=sys.stderr)
        return 1

    failures = 0
    warnings = 0
    total_functions = 0

    for path in sources:
        rel = path.relative_to(ROOT)
        src = path.read_text(encoding="utf-8")
        lines = src.count("\n") + (0 if src.endswith("\n") else 1)

        if lines > MAX_FILE_LINES or lines > WARN_FILE_LINES:
            kind = "FAIL" if lines > MAX_FILE_LINES else "WARN"
            limit = MAX_FILE_LINES if kind == "FAIL" else WARN_FILE_LINES
            print(f"{kind} {rel}: {lines} lines (limit {limit})")
            if kind == "FAIL":
                failures += 1
            else:
                warnings += 1

        for name, first, last in scan_functions(src):
            total_functions += 1
            length = last - first + 1
            if length > MAX_FUNCTION_LINES:
                print(f"FAIL {rel}:{first}-{last}: {name}() is {length} lines "
                      f"(limit {MAX_FUNCTION_LINES})")
                failures += 1

    print(f"source limits: {len(sources)} .c files, {total_functions} functions, "
          f"{failures} failure(s), {warnings} warning(s), "
          f"{len(GENERATED_HEADERS)} generated headers allowlisted")
    if failures and args.strict:
        return 1
    if failures:
        print("warning-only: pass --strict to fail the build")
    return 0


if __name__ == "__main__":
    sys.exit(main())
