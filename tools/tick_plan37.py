#!/usr/bin/env python3
"""Tick remaining Phase 3.7 plan checkboxes (docs-only, committed later)."""
from pathlib import Path

p = Path(__file__).resolve().parents[1] / "refactor" / "phase-3-readability.md"
text = p.read_text(encoding="utf-8")

edits = [
    ("- [ ] Split `cc_shell_key()` into per-state input handlers",
     "- [x] Split `cc_shell_key()` into per-state input handlers"),
    ("- [ ] Split `cc_shell_render()` into per-page renderers",
     "- [x] Split `cc_shell_render()` into per-page renderers"),
    ("- [ ] Replace magic screen/colour constants with named enums",
     "- [x] Replace magic screen/colour constants with named enums"),
    ("- [ ] Replace magic HUD offsets with named accessors",
     "- [x] Replace magic HUD offsets with named accessors"),
    ("### Remaining — structural (Phase 3.7: shell)",
     "### Done — structural (Phase 3.7: shell)"),
]
for old, new in edits:
    if old not in text:
        raise SystemExit(f"missing: {old!r}")
    text = text.replace(old, new)
p.write_text(text, encoding="utf-8")
print("phase-3-readability.md 3.7 ticked")
