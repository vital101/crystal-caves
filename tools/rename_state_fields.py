#!/usr/bin/env python3
"""Phase 3 (plan 3.2, cc_state_t): rename verified state fields.

Word-boundary renames of cc_state_t members across wasm/game only.
Declaration order is untouched; DS-offset comments stay on the
declarations (state.c CC_OFFSETOF asserts pin the layout; the 929
digest snapshots and the save round-trip fixtures are the guards).
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GAME = ROOT / "wasm" / "game"

MAP = {
    "g_a0cc": "crystals_remaining",
    "g_50a2": "crystals_collected",
    "g_a0f5": "exit_unlocked",
    "g_a0f7": "hub_door_active",
    "g_a0f9": "switch_latch",
    "g_a0f3": "lever_state",
    "g_a0fb": "all_caves_complete",
    "g_a126": "story_transition",
    "g_a10b": "collectible_spawn_active",
    "g_a10a": "suppress_base_stamp",
    "g_a0e8": "switch_repeat",
    "g_a0d0": "spawn_cell_x",
    "g_a0d2": "spawn_cell_y",
    "g_a0d4": "switch_a_col",
    "g_a0d6": "switch_a_row",
    "g_a0d8": "switch_b_col",
    "g_a0da": "switch_b_row",
    "g_a0dc": "switch_c_col",
    "g_a0de": "switch_c_row",
    "g_a0ce": "collectible_spawn_timer",
    "g_a0c2": "power_shot_timer_preset",
    "g_a0c8": "gravity_timer_preset",
    "g_a0ca": "red_mushroom_timer_preset",
    "g_a0e4": "stun_timer_preset",
    "g_a0c6": "power_subtimer",
    "g_509e": "health",
    "g_509c": "ammo",
    "g_5124": "death_state",
    "g_a0ea": "invuln_timer",
    "g_a0f6": "interaction_lock",
    "g_5118": "carry_x_delta",
    "g_5128": "carry_y_delta",
    "g_5126": "scroll_x_accum",
    "g_513c": "interaction_enabled",
    "g_5140": "interaction_suppressed",
    "g_513d": "riding_horizontal",
    "g_5142": "riding_vertical",
    "g_511a": "platform_slot_h",
    "g_511c": "platform_slot_v",
    "g_2b5e": "restart_timer",
    "g_2b5c": "text_scroll",
    "g_3394": "restart_input_latch",
    "g_a110": "restart_latch",
    "power_shot": "power_shot_active",
}


def main():
    # longest first keeps "power_shot" from firing inside a just-created name
    pairs = [(re.compile(r"\b" + old + r"\b"), new)
             for old, new in sorted(MAP.items(), key=lambda kv: -len(kv[0]))]
    for p in sorted(GAME.glob("*.c")) + sorted(GAME.glob("*.h")):
        s = t = p.read_text(encoding="utf-8")
        for rx, new in pairs:
            t = rx.sub(new, t)
        if t != s:
            p.write_text(t, encoding="utf-8")
            n = sum(1 for a, b in zip(s.split("\n"), t.split("\n")) if a != b)
            print(p.name, n)


if __name__ == "__main__":
    main()
