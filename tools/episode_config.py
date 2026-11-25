#!/usr/bin/env python3
"""Per-episode constants for the original-binary oracle tooling.

Episode 1 is the default and every tool keeps its historical behaviour unless
another episode is applied.  Episode 2 values come from the R0 delta analysis
in notes/ep2_diff.md (load offsets shift, segment bases move, code layout of
the handler block and TP runtime is unchanged).

Registered episode data is bring-your-own and stays out of git; point the
tools at an unpacked image under extracted/ep2/ (git-ignored).

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

    attrs = {
        "EXE": cfg["exe"],
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
