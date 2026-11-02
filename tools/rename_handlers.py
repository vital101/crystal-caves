#!/usr/bin/env python3
"""Phase 3 (plan 3.3/3.4): name the original handlers and unify dispatch.

- Renames every automatic h_<offset> function to a descriptive name
  (map below) everywhere in wasm/game.
- Removes the hand-written handler_for() switch from handlers_actors_b.c.
- Rewrites game/handlers_table.c as the single dispatch table:
  HandlerEntry {offset, fn, original label}; handler_for()/is_real_handler()
  both look that one table up; the coverage metric is unchanged.
- Adds `enum handler_offset` (named load offsets) to game_internal.h and
  replaces handler-field literals with the enum names.

Renames/lookups only: same 49 offsets, same inert fallback, same coverage
metric.  Behaviour is unchanged.
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GAME = ROOT / "wasm" / "game"

# load offset (segment offset, 0x1F9A:0xXXXX) -> new C name.
# Names come from the decoded behaviour; kinds are quoted only where the
# loader kind table (wasm/objects_data.h) confirms them.  notes/handlers.md
# carries the full per-handler evidence.
RENAMES = {
    "0x0831": ("h_pickup_effect", "h_831"),     # "effect for pick ups" (original comment)
    "0x1fb9": ("h_player_noop", "h_1fb9"),      # inert player-attached entry (fallback)
    "0x2176": ("h_map_companion", "h_2176"),    # kind 0x34 (52) companion
    "0x2241": ("h_dug_block", "h_2241"),        # block left after digging
    "0x251d": ("h_slash_update", "h_251d"),     # shared slash-blade animation update
    "0x2850": ("h_attack_gate", "h_2850"),      # kind 2: arms/limits the player attack
    "0x2877": ("h_dissolve", "h_2877"),         # kind 3: dissolve-away object
    "0x1fc2": ("h_companion_spawner", "h_1fc2"),
    "0x323c": ("h_spawn_below", "h_323c"),
    "0x3be8": ("h_expand_anim", "h_3be8"),      # kind 0x15 (21) expanding animated
    "0x3d23": ("h_door_lever", "h_3d23"),       # kind 0x22 (34) door/switch object
    "0x3e70": ("h_ambient_anim_a", "h_3e70"),   # kind 0x24 (36)
    "0x3f13": ("h_door", "h_3f13"),             # kind 60 hub door
    "0x4a4b": ("h_ambient_anim_b", "h_4a4b"),   # kind 0x1F (31)
    "0x4a82": ("h_state_toggle", "h_4a82"),     # kind 0x2C (44)
    "0x298a": ("h_walk_two_phase", "h_298a"),
    "0x2acf": ("h_walk_flip_contact", "h_2acf"),
    "0x2bd4": ("h_walk_h_prox", "h_2bd4"),
    "0x3904": ("h_lever_switch", "h_3904"),     # ON/OFF cell switch (starts kind 0x27 lifts)
    "0x3a2e": ("h_lever_anim", "h_3a2e"),
    "0x47b9": ("h_walk_two_anim", "h_47b9"),
    "0x48fe": ("h_walk_random", "h_48fe"),
    "0x4ad3": ("h_walk_anim_lr", "h_4ad3"),
    "0x4eeb": ("h_walk_companion", "h_4eeb"),
    "0x52e7": ("h_projectile", "h_52e7"),       # kind 0x37 (55)
    "0x6232": ("h_walk_two_frame", "h_6232"),
    "0x62f2": ("h_walk_respawn", "h_62f2"),
    "0x2d37": ("h_move_v_prox", "h_2d37"),
    "0x2e25": ("h_spawn_cam_rel", "h_2e25"),
    "0x2f83": ("h_move_v_paired", "h_2f83"),
    "0x32f6": ("h_move_v_companion", "h_32f6"),
    "0x3416": ("h_walk_prox_spawn", "h_3416"),
    "0x36b0": ("h_move_v_formation", "h_36b0"),
    "0x4145": ("h_bounce_adversary", "h_4145"), # kind 35 bouncing adversary
    "0x403a": ("h_player_shover", "h_403a"),
    "0x3f6c": ("h_player_shover_alt", "h_3f6c"),
    "0x55ec": ("h_move_h_paired", "h_55ec"),
    "0x4c92": ("h_spawn_projectile", "h_4c92"),
    "0x5c8f": ("h_boss_formation", "h_5c8f"),
    "0x4fd4": ("h_boss_retarget", "h_4fd4"),    # kind 0x39 (57)
    "0x6458": ("h_companion_pair", "h_6458"),
    "0x5855": ("h_group_move3", "h_5855"),      # kind 48
    "0x5bbe": ("h_anim_mover", "h_5bbe"),       # kind 0x2D
    "0x6681": ("h_cell_erase", "h_6681"),
    "0x672b": ("h_crystal_bob", "h_672b"),
    "0x69d3": ("h_pickup_rise", "h_69d3"),
    "0x6afa": ("h_platform_h", "h_6afa"),       # kind 0x13 (19)
    "0x6d31": ("h_platform_v", "h_6d31"),       # kinds 0x14/0x27 (20/39)
    "0x6ff6": ("h_flip_mover", "h_6ff6"),       # kind 0x20 (32)
    "0x70e4": ("h_flip_partner", "h_70e4"),     # kind 0x21 (33)
}

# The 49 dispatched offsets in the order of the original handler_for switch.
SWITCH_ORDER = [
    "0x1fb9", "0x2241", "0x2850", "0x2877", "0x0831", "0x1fc2", "0x2176",
    "0x323c", "0x3be8", "0x3d23", "0x3e70", "0x3f13", "0x298a", "0x2d37",
    "0x3416", "0x403a", "0x55ec", "0x62f2", "0x6232", "0x4c92", "0x5c8f",
    "0x4eeb", "0x52e7", "0x2f83", "0x3a2e", "0x3f6c", "0x2e25", "0x4ad3",
    "0x36b0", "0x2acf", "0x2bd4", "0x3904", "0x4145", "0x47b9", "0x32f6",
    "0x4a4b", "0x4a82", "0x48fe", "0x4fd4", "0x6458", "0x5855", "0x5bbe",
    "0x6681", "0x672b", "0x69d3", "0x6afa", "0x6d31", "0x6ff6", "0x70e4",
]

# offset -> file where the implementation lives (verified by grep of defs)
DEF_FILE = {}
for f, offs in {
    "handlers_interaction.c": ["0x3e70", "0x4a4b", "0x4a82", "0x1fc2", "0x323c",
                               "0x2176", "0x5855", "0x5bbe", "0x3be8", "0x2241"],
    "handlers_actors_a.c": ["0x6681", "0x672b", "0x69d3", "0x298a", "0x403a",
                            "0x55ec", "0x3a2e", "0x3f6c", "0x2e25", "0x2f83",
                            "0x4ad3", "0x36b0", "0x4eeb", "0x52e7", "0x6232",
                            "0x4c92"],
    "handlers_actors_b.c": ["0x5c8f", "0x62f2", "0x3416", "0x2d37", "0x2acf",
                            "0x2bd4", "0x3904", "0x47b9", "0x32f6", "0x4145",
                            "0x48fe", "0x6458", "0x4fd4"],
    "handlers_platforms.c": ["0x1fb9", "0x2850", "0x2877", "0x6ff6", "0x70e4",
                             "0x3f13", "0x3d23", "0x6afa", "0x6d31", "0x0831"],
}.items():
    for off in offs:
        assert off not in DEF_FILE, off
        DEF_FILE[off] = f
assert sorted(map(lambda x: int(x, 16), DEF_FILE)) == sorted(map(lambda x: int(x, 16), SWITCH_ORDER))
assert {int(x, 16) for x in SWITCH_ORDER} == {int(k, 16) for k in RENAMES} - {0x251d}

LOOKUP = """    for (const HandlerEntry *e = g_handlers;
         e != g_handlers + sizeof(g_handlers) / sizeof(g_handlers[0]); ++e) {
        if (e->offset == off)
            %s
    }"""


def table_text():
    lines = [
        "/*",
        " * The single dispatch table for the original handler list (all 49",
        " * offsets the object loop dispatches to).  Each entry is the handler",
        " * load offset (segment offset into the 0x1F9A block), the C function",
        " * that implements it, and the automatic disassembler label it was",
        " * ported from.  The loader kind table (wasm/objects_data.h) is what",
        " * associates an object kind with one of these offsets, so kinds are",
        " * only named in the comments where that table confirms them.",
        " */",
        "typedef struct {",
        "    uint16_t offset;   /* handler load offset (0x1F9A segment) */",
        "    handler_fn fn;",
        "    const char *name;  /* original automatic label */",
        "} HandlerEntry;",
        "",
        "static const HandlerEntry g_handlers[] = {",
    ]
    for fname in ("game/handlers_platforms.c", "game/handlers_interaction.c",
                  "game/handlers_actors_a.c", "game/handlers_actors_b.c"):
        lines.append(f"    /* {fname} */")
        for off in SWITCH_ORDER:
            if "game/" + DEF_FILE[off] != fname:
                continue
            cname, label = RENAMES[off]
            lines.append(f'    {{0x{off[2:]}, {cname}, "{label}"}},')
        lines.append("")
    lines.pop()
    lines += [
        "};",
        "",
        "/* Function for one handler load offset, or the inert fallback. */",
        "handler_fn handler_for(uint16_t off) {",
        LOOKUP % "return e->fn;",
        "    g_unknown_handler_hits++;",
        "    return h_player_noop;   /* inert fallback */",
        "}",
        "",
        "/* True if the original game would have dispatched to this offset. */",
        "int is_real_handler(uint16_t off) {",
        LOOKUP % "return 1;",
        "    return 0;",
        "}",
        "",
        "/* Original disassembler label for a handler load offset. */",
        "const char *handler_name(uint16_t off) {",
        LOOKUP % "return e->name;",
        "    return 0;",
        "}",
        "",
        "/* Porting-progress coverage metric (see test_gameplay.js). */",
    ]
    return "\n".join(lines)


NEW_TABLE_C = """/* Crystal Caves gameplay module - split from wasm/objects.c.
 *
 * This module owns the single handler dispatch table for the original
 * object loop: one entry per handler load offset.  handler_for(),
 * is_real_handler() and handler_name() all resolve through it, so there is
 * exactly one list of the original handler offsets in the tree.
 */
#include "game_internal.h"

/* Handler offset (behaviour ID, segment 0x1F9A) for slot i. */
uint16_t cc_obj_handler(int i)  { return (i >= 0 && i < OBJ_MAX) ? g_state.obj[i].handler : 0; }

""" + table_text() + """int cc_handler_coverage_num(void) {
    int n = 0, d = 0;
    for (int i = 0; i < OBJ_MAX; i++) {
        if (g_state.obj[i].kind == 0) continue;
        if (g_state.obj[i].state == 2) continue;
        d++;
        if (is_real_handler(g_state.obj[i].handler)) n++;
    }
    return d ? (n * 100) / d : 100;
}
"""


def enum_text():
    lines = [
        "",
        "/*",
        " * Handler load offsets (segment offsets into the 0x1F9A handler",
        " * block, see notes/handlers.md), named after the dispatch table",
        " * entry that implements each one.",
        " */",
        "enum handler_offset {",
    ]
    for off in SWITCH_ORDER:
        cname = RENAMES[off][0]
        lines.append(f"    HDL_{cname[2:].upper()} = {off},")
    lines += ["    HDL_SLASH_UPDATE = 0x251d,   /* shared slash-blade update helper */",
              "};",
              ""]
    return "\n".join(lines)


def main():
    files = sorted(GAME.glob("*.c")) + sorted(GAME.glob("*.h"))

    # 1) rename every h_<off> identifier in wasm/game (label = original spelling)
    pairs = [(re.compile(r"\b" + re.escape(RENAMES[off][1]) + r"\b"), RENAMES[off][0])
             for off in sorted(RENAMES, key=lambda k: -len(RENAMES[k][1]))]
    for p in files:
        s = t = p.read_text(encoding="utf-8")
        for rx, name in pairs:
            t = rx.sub(name, t)
        if t != s:
            p.write_text(t, encoding="utf-8")

    # 2) drop the hand-written handler_for switch from handlers_actors_b.c
    p = GAME / "handlers_actors_b.c"
    s = p.read_text(encoding="utf-8")
    new, n = re.subn(r"\nhandler_fn handler_for\(uint16_t off\) \{\n(?:.*?\n)*?\}\n",
                     "\n", s)
    assert n == 1, f"handler_for removal matched {n}"
    p.write_text(new.rstrip() + "\n", encoding="utf-8")

    # 3) rewrite handlers_table.c with the unified table
    (GAME / "handlers_table.c").write_text(NEW_TABLE_C, encoding="utf-8")

    # 4) named-offset enum in game_internal.h + literal replacement
    p = GAME / "game_internal.h"
    s = p.read_text(encoding="utf-8")
    assert "int is_real_handler(uint16_t off);" in s
    s = s.replace("int is_real_handler(uint16_t off);",
                  "int is_real_handler(uint16_t off);\nconst char *handler_name(uint16_t off);")
    anchor = "typedef void (*handler_fn)(ObjectEntry *, int, int16_t, int16_t);"
    assert anchor in s, "handler_fn anchor missing"
    s = s.replace(anchor, anchor + "\n" + enum_text(), 1)
    p.write_text(s, encoding="utf-8")

    enum_by_val = {}
    for off in SWITCH_ORDER:
        enum_by_val[int(off, 16)] = "HDL_" + RENAMES[off][0][2:].upper()

    total = 0
    for p in files:
        if p.name in ("handlers_table.c", "game_internal.h"):
            continue
        s = t = p.read_text(encoding="utf-8")
        for val, name in sorted(enum_by_val.items(), key=lambda kv: -kv[0]):
            for hexlit in sorted(("0x%04x" % val, "0x%x" % val,
                                  "0x%04X" % val, "0x%X" % val),
                                 key=len, reverse=True):
                t = re.sub(r"(\.handler\s*=\s*)" + hexlit + r"\b", r"\g<1>" + name, t)
                t = re.sub(r"(->handler\s*=\s*)" + hexlit + r"\b", r"\g<1>" + name, t)
                t = re.sub(r"(\.handler\s*(?:==|!=)\s*)" + hexlit + r"\b", r"\g<1>" + name, t)
                t = re.sub(r"(->handler\s*(?:==|!=)\s*)" + hexlit + r"\b", r"\g<1>" + name, t)
                t = re.sub(r"handler_for\(\s*\(?uint16_t\)?\s*" + hexlit + r"\s*\)",
                           "handler_for(" + name + ")", t)
                t = re.sub(r"handler_for\(" + hexlit + r"\)", "handler_for(" + name + ")", t)
        if t != s:
            p.write_text(t, encoding="utf-8")
            total += sum(1 for a, b in zip(s.split("\n"), t.split("\n")) if a != b)
    print("literal lines changed:", total)


if __name__ == "__main__":
    main()
