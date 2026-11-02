#!/usr/bin/env python3
"""Phase 3 (plan 3.1): name the sound-effect ID literals.

Adds wasm/sound_ids.h (already written), includes it from the game and
shell umbrella headers, and replaces every play_sound()/cc_sound_play()
raw ID literal in wasm/game and wasm/shell with its named constant.
Pure literal substitution: the enum values are the same numbers.
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

NAMES = {
    1: "SND_JUMP",
    3: "SND_EXIT_FOUND",
    4: "SND_EXIT_LOCKED",
    5: "SND_HURT",
    6: "SND_HIT",
    7: "SND_PLAYER_DRAW",
    8: "SND_CRYSTAL",
    9: "SND_PICKUP",
    0x0A: "SND_BLOCK_POP",
    0x0B: "SND_PRIZE_CELL",
    0x0C: "SND_NEW_ITEM",
    0x0D: "SND_HIT_2C",
    0x0E: "SND_SHOT",
    0x0F: "SND_POWER_SHOT",
    0x10: "SND_COMPANION",
    0x11: "SND_DIG",
    0x12: "SND_STUN",
    0x13: "SND_GRAVITY",
    0x14: "SND_DOOR",
    0x15: "SND_SWITCH_TOGGLE",
    0x16: "SND_MUSHROOM",
    0x17: "SND_MULTIPART_MOVE",
    0x18: "SND_MOVER_STOP",
    0x19: "SND_MOVER_WAKE",
    26: "SND_FINALE_FANFARE",
    0x1B: "SND_BIG_GEM",
    0x1C: "SND_FALLING_BLOCK",
    0x1D: "SND_POISON",
    0x1E: "SND_SHOT_DRY",
    0x1F: "SND_SWITCH_ACTIVATE",
    0x20: "SND_FANFARE",
    33: "SND_MENU_ACCEPT",
    34: "SND_HISCORE_NAME",
    35: "SND_TEXT_PAGE",
    36: "SND_LEVEL_START",
}

CALL = re.compile(r"\b(cc_sound_play|(?:cc_)?play_sound)\(\s*(0x[0-9a-fA-F]{1,4}|\d{1,3})\s*\)")

def sub_call(m):
    return "%s(%s)" % (m.group(1), NAMES[int(m.group(2), 0)])

def main():
    for hdr in (ROOT / "wasm/game/game_internal.h",
                ROOT / "wasm/shell/shell_internal.h"):
        s = hdr.read_text(encoding="utf-8")
        if '#include "sound_ids.h"' not in s:
            s = s.replace('#include "sound.h"', '#include "sound.h"\n#include "sound_ids.h"', 1)
            hdr.write_text(s, encoding="utf-8")

    total = 0
    for d in ("wasm/game", "wasm/shell"):
        for p in sorted((ROOT / d).glob("*.c")):
            s = t = p.read_text(encoding="utf-8")
            t = CALL.sub(sub_call, t)
            # the all-crystals conditional is not a plain literal:
            t = t.replace("? 0x20 : 8)", "? SND_FANFARE : SND_CRYSTAL)")
            if t != s:
                p.write_text(t, encoding="utf-8")
                total += sum(1 for a, b in zip(s.split("\n"), t.split("\n")) if a != b)
    print("call sites renamed:", total)
    left = [str(p) for p in (ROOT / "wasm/game").glob("*.c")
            if CALL.search(p.read_text(encoding="utf-8"))]
    print("unmapped remaining:", left)

if __name__ == "__main__":
    main()
