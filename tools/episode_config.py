#!/usr/bin/env python3
"""Per-episode constants for the original-binary oracle tooling.

Episode 1 is the default and every tool keeps its historical behaviour unless
another episode is applied.  Episode 2 values come from the R0 delta analysis
in notes/ep2_diff.md (load offsets shift, segment bases move, code layout of
the handler block and TP runtime is unchanged).

Registered episode data is committed in this private fork; the tools read
the image under extracted/ep2/ or extracted/ep3/.

Usage:
    import episode_config
    episode_config.apply("ep2")     # mutates emulate_load / emulate_startup
    cfg = episode_config.get("ep2")
"""

import os

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

EPISODES = {
    "ep1": {
        "name": "ep1",
        "prefix": "CC1",
        "exe": os.path.join(ROOT, "extracted", "CC1-UNLZ.EXE"),
        "startup": 0xC3E,          # new-game initializer
        "loader": 0xB1A9,          # level loader
        "load_end": 0xCEEC,        # end-of-load / draw-map jump target
        "data_seg": 0x2CD3,        # DS = ES
        "stack_seg": 0x370C,       # MZ header SS
        "stack_ptr": 0x4000,       # MZ header SP
        "skip_segs": (0x28B5, 0x26C5, 0x0FC5),  # TP library, draw, cell events
        "blitter_seg": 0x28B5,      # EGA blitter code segment
        "rng_seg": 0x2BB5,         # TP RNG routine segment
        "rng_ip": 0x0980,
        "spawn": 0x047D,
        "draw_sites": (0x052B, 0x0560),
        "handler_seg": 0x1F9A,
        "crystal_w7": (0x01, 0x04),
        "frame": {
            "horiz": 0xE011, "jump": 0xE36B, "contact": 0xE5D3,
            "fire": 0xEBF2, "status": 0xEAEA, "objects": 0xD72A,
        },
        "cell_event_linear": 0x1B776,
        "cell_event_seg": 0x0FC5,
        "cell_event_off": 0xBB26,
        "play_sound": 0x1FBC5,
        "shell": {
            "module": os.path.join(ROOT, "extracted", "CC1-UNLZ.EXE"),
            "seg": 0x0FC5,
            "video_segs": (0x28B5, 0x26C5),
            "render_site": 0x104C7,
            "render_sites": (0x104C7, 0x204C7),
            "linebreak_site": 0x10939,
            "menu_entry": 0x17564,
            "menu_input": 0x17729,
            "menu_runs": (
                (0x1390A, ("ordering_episodes", "ordering_offer", "ordering_disk")),
                (0x19130, ("foreign", "foreign_au", "foreign_de", "foreign_uk")),
                (0x19A88, ("bbs",)),
                (0x19E1F, ("about",)),
                (0x17978, ("menu_help",)),
                (0x15E01, ("menu_quit",)),
            ),
            "video_entry": 0x1A688,
            "video_site": 0x1A7C0,
            "quit_entry": 0x114A1,
            "quit_site": 0x114E4,
            "story_intro": (0x123F8, 0x12BE2),
            "story_finale": (0x11DE8, 0x116C2, 0x119A8),
            "hiscore_defaults": 0x18388,
            "hiscore_screen": 0x13CC8,
            "key_table": 0xFC8E,
            "sound_seg": 0x1F9A,
            "sound_play": 0x21F,
            "title_entry_off": 0x8265,
            "title_hold": 0x17F74,
            "title_draw": 0x18024,
            "title_flip": 0x18074,
            "screens_bin": os.path.join(ROOT, "out", "portable", "screens.bin"),
        },
        "ending_screens": 1,
        "levels": 19,
    },
    "ep2": {
        "name": "ep2",
        "prefix": "CC2",
        "exe": os.path.join(ROOT, "extracted", "ep2", "CC2-UNLZ.EXE"),
        "startup": 0xC3E,          # byte-identical to ep1
        "loader": 0xB12E,          # 0xB1A9 - 123
        "load_end": 0xCE76,        # 0xCEEC - 118
        "data_seg": 0x2C95,        # 0x2CD3 - 0x3E paragraphs
        "stack_seg": 0x36CE,       # MZ header SS
        "stack_ptr": 0x7D00,       # MZ header SP
        "skip_segs": (0x2877, 0x269D, 0x0FBD),
        "blitter_seg": 0x2877,
        "rng_seg": 0x2B77,
        "rng_ip": 0x0980,
        "spawn": 0x047D,
        "draw_sites": (0x052B, 0x0560),
        "handler_seg": 0x1F72,
        "crystal_w7": (0x0D, 0x10),
        "frame": {
            "horiz": 0xDF9B, "jump": 0xE2F5, "contact": 0xE55D,
            "fire": 0xEB7C, "status": 0xEA74, "objects": 0xD6B4,
        },
        "cell_event_linear": 0x1B4ED,
        "cell_event_seg": 0x0FBD,
        "cell_event_off": 0xB91D,
        "play_sound": 0x1F945,
        "shell": {
            "module": os.path.join(ROOT, "extracted", "ep2", "CC2-UNLZ.EXE"),
            "seg": 0x0FBD,
            "video_segs": (0x2877, 0x269D),
            "render_site": 0x10447,
            "render_sites": (0x10447, 0x20247),
            "linebreak_site": 0x108B9,
            "menu_entry": 0x1731B,
            "menu_input": 0x174E0,
            "menu_runs": (
                (0x136C1, ("ordering_episodes", "ordering_offer", "ordering_disk")),
                (0x18ECD, ("foreign", "foreign_au", "foreign_de", "foreign_uk")),
                (0x19825, ("bbs",)),
                (0x19BBC, ("about",)),
                (0x1772F, ("menu_help",)),
                (0x15BB8, ("menu_quit",)),
            ),
            "video_entry": 0x1A427,
            "video_site": 0x1A55F,
            "quit_entry": 0x11421,
            "quit_site": 0x11464,
            "story_intro": (0x12327, 0x129B7),
            "story_finale": (0x11EC3, 0x11617, 0x119E3),
            "hiscore_defaults": 0x1813F,
            "hiscore_screen": 0x13A7F,
            "key_table": 0xFC0E,
            "sound_seg": 0x1F72,
            "sound_play": 0x21F,
            "title_entry_off": 0x809C,
            "title_hold": 0x17D2B,
            "title_draw": 0x17DDB,
            "title_flip": 0x17E2B,
            "screens_bin": os.path.join(ROOT, "out", "portable_ep2", "screens.bin"),
        },
        "ending_screens": 1,
        "levels": 19,
    },
    "ep3": {
        "name": "ep3",
        "prefix": "CC3",
        "exe": os.path.join(ROOT, "extracted", "ep3", "CC3-UNLZ.EXE"),
        "startup": 0xC3E,          # byte-identical to ep1/ep2
        "loader": 0xB0CE,          # 0xB1A9 - 219
        "load_end": 0xCD05,        # 0xCEEC - 487 (draw-map sequence)
        "data_seg": 0x2E17,        # from the TP runtime prologue
        "stack_seg": 0x3850,       # MZ header SS
        "stack_ptr": 0x7D00,       # MZ header SP
        "skip_segs": (0x29F9, 0x282F, 0x0FA6),
        "blitter_seg": 0x29F9,
        "rng_seg": 0x2CF9,
        "rng_ip": 0x0980,
        "spawn": 0x047D,
        "draw_sites": (0x052B, 0x0560),
        "handler_seg": 0x2104,      # ep1 offsets unchanged (0x1F9A + 0x16A)
        "crystal_w7": (0x21, 0x24),  # ep3 crystals w7 0x21..0x24
        "frame": {
            "horiz": 0xDE2A, "jump": 0xE184, "contact": 0xE3EC,
            "fire": 0xEA0B, "status": 0xE903, "objects": 0xD543,
        },
        "cell_event_linear": 0x1CE07,
        "cell_event_seg": 0x0FA6,
        "cell_event_off": 0xD3A7,
        "play_sound": 0x21265,
        "shell": {
            "module": os.path.join(ROOT, "extracted", "ep3", "CC3-UNLZ.EXE"),
            "seg": 0x0FA6,
            "video_segs": (0x29F9, 0x282F),
            "render_site": 0x102D7,
            "render_sites": (0x102D7, 0x21B67),
            "linebreak_site": 0x10749,
            "menu_entry": 0x18964,
            "menu_input": 0x18B29,
            "menu_runs": (
                (0x14D0A, ("ordering_episodes", "ordering_offer", "ordering_disk")),
                (0x1A516, ("foreign", "foreign_au", "foreign_de", "foreign_uk")),
                (0x1AE6E, ("bbs",)),
                (0x1B205, ("about",)),
                (0x18D78, ("menu_help",)),
                (0x17201, ("menu_quit",)),
            ),
            "video_entry": 0x1BA70,
            "video_site": 0x1BBA8,
            "quit_entry": 0x112B1,
            "quit_site": 0x112F4,
            "story_intro": (0x139F5, 0x14000),
            "story_finale": (0x114A6, 0x11B06, 0x122C9, 0x12494,
                             0x12925, 0x12E5C, 0x131F3, 0x135B9),
            "hiscore_defaults": 0x19788,
            "hiscore_screen": 0x150C8,
            "key_table": 0xFA9E,
            "sound_seg": 0x2104,
            "sound_play": 0x21F,
            "title_entry_off": 0x9855,
            "title_hold": 0x19374,
            "title_draw": 0x19424,
            "title_flip": 0x19474,
            "screens_bin": os.path.join(ROOT, "out", "portable_ep3", "screens.bin"),
        },
        "ending_screens": 3,
        "levels": 19,
    },
}


def get(name):
    if name not in EPISODES:
        raise SystemExit("unknown episode %r (have: %s)"
                         % (name, ", ".join(sorted(EPISODES))))
    return EPISODES[name]


def apply(name):
    """Apply an episode to the loaded oracle modules.

    Constants are read at call time by emulate_load/emulate_startup, so
    rebinding the module globals is enough.  Both modules are patched so a
    runner can mix startup and loader calls; when a tool runs as ``__main__``
    it is a separate module object from its imported copy, so patch that too.
    """
    cfg = get(name)
    import sys

    import emulate_load as load
    import emulate_startup as startup
    targets = {load, startup}
    main = sys.modules.get("__main__")
    if main is not None and main not in targets and hasattr(main, "SKIP_SEGS"):
        targets.add(main)

    with open(cfg["exe"], "rb") as fh:
        mz_head = fh.read(0x40)
    attrs = {
        "EXE": cfg["exe"],
        # LZEXE builds use different MZ header sizes (ep3 is 0x2000).
        "HDR": int.from_bytes(mz_head[8:10], "little") * 16,
        "DATA_SEG": cfg["data_seg"],
        "STACK_SEG": cfg["stack_seg"],
        "STACK_PTR": cfg["stack_ptr"],
        "STARTUP": cfg["startup"],
        "LOADER": cfg["loader"],
        "LOAD_END": cfg["load_end"],
        "SKIP_SEGS": set(cfg["skip_segs"]),
        "SPAWN": cfg["spawn"],
        "DRAW_SITES": tuple(cfg["draw_sites"]),
        "RNG_SEG": cfg["rng_seg"],
        "RNG_IP": cfg["rng_ip"],
        "BLITTER_SEG": cfg["blitter_seg"],
        "LEVEL_COUNT": cfg["levels"],
    }
    for module in targets:
        for attr, value in attrs.items():
            setattr(module, attr, value)

    # Physics/game oracles keep their own copies of the frame-routine
    # addresses and skip sets; patch them when those modules are loaded.
    physics = sys.modules.get("oracle_physics")
    if physics is not None:
        physics.SKIP_SEGS = set(cfg["skip_segs"])
        physics.LIVE_FC5 = {(cfg["cell_event_seg"], cfg["cell_event_off"])}
        physics.HORIZ = cfg["frame"]["horiz"]
        physics.JUMP = cfg["frame"]["jump"]
        physics.CONTACT = cfg["frame"]["contact"]
        physics.FIRE_TIMERS = cfg["frame"]["fire"]
        physics.STATUS_TIMERS = cfg["frame"]["status"]
        physics.OBJECTS = cfg["frame"]["objects"]
        physics.CELL_EVENT = cfg["cell_event_linear"]
        physics.PLAY_SOUND = cfg["play_sound"]
    game = sys.modules.get("emulate_game")
    if game is not None:
        game.UPDATE_LOOP = cfg["frame"]["objects"]
    return cfg
