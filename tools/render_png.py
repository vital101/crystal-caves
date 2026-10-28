#!/usr/bin/env python3
"""Render a Crystal Caves level to PNG (compatibility CLI).

This was the original renderer; its shape decoder assumed a 5-plane
packed format and its cells came from a hand interpreter with several wrong
assumptions, which is why the early `out/level_XX.png` renders looked wrong.
Both parts are now fixed by delegation:

  * cells   -> tools/build_cells.build_cells() (golden-oracle emulator dumps)
  * drawing -> tools/render_oracle.render()   (confirmed EGA group format:
               16 rows x 2 groups x 5 bytes [mask][plane0..plane3], 16-colour
               BIOS EGA palette, layer order 1,2,3)

Validated bit-exact against the full-fidelity emulator (tools/emulate_render.py).
"""
import os
import sys

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(HERE, "tools"))
import render_oracle  # noqa: E402


def render(level):
    out = os.path.join(HERE, "out", "level_%02d.png" % level)
    return render_oracle.render(level, out=out)


if __name__ == "__main__":
    for a in sys.argv[1:] or ["0"]:
        render(int(a))
