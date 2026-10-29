#!/usr/bin/env python3
"""Exact player-physics oracle for Crystal Caves.

Unlike `emulate_game.py` (object loop only) this harness runs the real
horizontal/jump/vertical routines from a complete startup + level-load DS
image.  Video, redraw and sound far-calls are skipped; collision, cell reads
and object handlers run for real.

Routine order in the original normal-state frame (0xF4E6 branch):
    0xE011  horizontal + camera-x/camera-y
    0xE36B  jump start + ascending (calls 0xDCB9)
    0xE5D3  hazard/contact (out of Phase 1 scope)
    0xEBF2  items/fire/exit (out of Phase 1 scope)
    0xEAEA  timers/status (out of Phase 1 scope)
    0xD72A  object update loop

Phase 1 fixtures use the physics subset: E011, E36B, D72A.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import emulate_load as E  # noqa: E402
import emulate_startup as S  # noqa: E402
from oracle_frame import u16, s16, w16  # noqa: E402

from unicorn import (  # noqa: E402
    Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_CODE, UC_HOOK_INTR,
)
from unicorn.x86_const import (  # noqa: E402
    UC_X86_REG_CS, UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS,
    UC_X86_REG_SP, UC_X86_REG_IP, UC_X86_REG_AX, UC_X86_REG_FLAGS,
)

RET_SENTINEL = E.RET_SENTINEL
SKIP_SEGS = {0x28B5, 0x26C5, 0x0FC5}
HORIZ = 0xE011
JUMP = 0xE36B
OBJECTS = 0xD72A

# Inputs: keyboard flags.  (0x338A is joystick fire, not the keyboard key.)
INPUT_LEFT = 0x338E
INPUT_RIGHT = 0x338F
INPUT_JUMP = 0x338B
INPUT_FIRE = 0x3390

# Physics state captured per frame.
FIELDS = [
    ("x", 0x4611, "s16"), ("y", 0x4613, "s16"),
    ("cam_x", 0x512E, "s16"), ("cam_y", 0x5130, "s16"),
    ("move_dir", 0x4629, "s16"),
    ("walk_speed", 0x2BA, "u16"), ("impulse", 0x2CA, "u16"),
    ("walk_phase", 0xA0B4, "u16"),
    ("jump_phase", 0x5134, "u16"),
    ("jump_flag", 0x306, "u8"), ("airborne", 0xA0FD, "u8"),
    ("anim_suppress", 0x30A, "u8"),
    ("shape", 0x4627, "u16"),
    ("shape_a", 0x461F, "u16"), ("shape_b", 0x461D, "u16"),
    ("player_block", 0x4619, "u16"), ("anim_timer", 0x461B, "u16"),
    ("carry_x", 0x5118, "u16"), ("scroll_x", 0x5126, "u16"),
    ("carry_y", 0x5128, "u16"),
    ("ride_h", 0x513D, "u8"), ("ride_v", 0x5142, "u8"),
    ("blocked_l", 0xA0F1, "u8"), ("blocked_r", 0xA0F2, "u8"),
    ("gravity_scale", 0x5132, "s16"),
    ("c8", 0x2C8, "u16"), ("c6", 0x2C6, "u16"),
    ("lives", 0x509E, "u16"), ("ammo", 0x509C, "u16"),
    ("invuln", 0xA0EA, "u16"), ("hit_lock", 0x4660, "u8"),
    ("exit_state", 0x5124, "u16"),
]


class PhysicsOracle:
    def __init__(self, level):
        self.level = level
        r = S.run_level(level, include_ds=True)
        self.level_data = r
        self.ds = r["ds"]
        self.objects = r["objects"]
        self.cells = r["cells"]

        module = E.load_module()
        self.module = module
        self.mu = Uc(UC_ARCH_X86, UC_MODE_16)
        mu = self.mu
        mu.mem_map(0, 0x110000)
        mu.mem_write(0, module)
        mu.mem_write(RET_SENTINEL, b"\xf4")
        mu.reg_write(UC_X86_REG_SS, E.STACK_SEG)
        mu.reg_write(UC_X86_REG_DS, E.DATA_SEG)
        mu.reg_write(UC_X86_REG_ES, E.DATA_SEG)
        mu.reg_write(UC_X86_REG_CS, 0)
        self.stopped = None
        self._install_hooks()
        self.reset()
        self.start = self.dump()

    def _install_hooks(self):
        mu = self.mu

        def hook_code(uc, address, size, user):
            if address == RET_SENTINEL:
                self.stopped = "ret"
                uc.emu_stop()
                return
            if uc.mem_read(address, 1)[0] == 0x9A:
                raw = uc.mem_read(address, 5)
                off = int.from_bytes(raw[1:3], "little")
                seg = int.from_bytes(raw[3:5], "little")
                if seg in SKIP_SEGS:
                    n = E.retf_argbytes(self.module, seg, off)
                    sp = uc.reg_read(UC_X86_REG_SP)
                    uc.reg_write(UC_X86_REG_SP, (sp + n) & 0xFFFF)
                    uc.reg_write(UC_X86_REG_IP, (address + 5) & 0xFFFF)

        def hook_intr(uc, intno, user):
            ip = uc.reg_read(UC_X86_REG_IP)
            ax = uc.reg_read(UC_X86_REG_AX)
            if intno == 0x10 and (ax >> 8) == 0x0F:
                uc.reg_write(UC_X86_REG_AX, (ax & 0xFF00) | 0x13)
            elif intno == 0x16 and (ax >> 8) in (0x01, 0x11):
                uc.reg_write(UC_X86_REG_FLAGS,
                              uc.reg_read(UC_X86_REG_FLAGS) | 0x40)
            uc.reg_write(UC_X86_REG_IP, (ip + 2) & 0xFFFF)

        mu.hook_add(UC_HOOK_CODE, hook_code)
        mu.hook_add(UC_HOOK_INTR, hook_intr)

    # ---------------------------------------------------------- state access
    def reset(self):
        """Restore the exact post-startup/post-load DS image."""
        self.mu.mem_write(E.DATA_SEG << 4, self.ds)
        self.stopped = None

    def state(self, off):
        return s16(self.mu, E.DATA_SEG, off)

    def set_global(self, off, val, width=2):
        self.mu.mem_write((E.DATA_SEG << 4) + off,
                          (val & 0xFF).to_bytes(1, "little") if width == 1
                          else (val & 0xFFFF).to_bytes(2, "little"))

    def set_inputs(self, left=False, right=False, jump=False, fire=False):
        mu = self.mu
        self.set_global(INPUT_LEFT, 1 if left else 0, 1)
        self.set_global(INPUT_RIGHT, 1 if right else 0, 1)
        self.set_global(INPUT_JUMP, 1 if jump else 0, 1)
        self.set_global(INPUT_FIRE, 1 if fire else 0, 1)
        self.set_global(0x338A, 0, 1)  # joystick fire
        # Keyboard-ISR side effects relevant to physics (0x9B3..0x9F0).
        if not (left or right):
            w16(mu, E.DATA_SEG, 0xA0B4, 0)
        if left or right:
            self.set_global(0x030A, 0, 1)

    def seed(self, x=None, y=None, cam_x=None, cam_y=None, **globals_):
        if x is not None:
            self.set_global(0x4611, x)
        if y is not None:
            self.set_global(0x4613, y)
        if cam_x is not None:
            self.set_global(0x512E, cam_x)
        if cam_y is not None:
            self.set_global(0x5130, cam_y)
        for name, val in globals_.items():
            off = {
                "jump_phase": 0x5134, "jump_flag": 0x306,
                "airborne": 0xA0FD, "walk_phase": 0xA0B4,
                "walk_speed": 0x2BA, "impulse": 0x2CA,
                "gravity_scale": 0x5132, "move_dir": 0x4629,
                "shape": 0x4627, "shape_a": 0x461F, "shape_b": 0x461D,
                "player_block": 0x4619, "anim_timer": 0x461B,
                "carry_x": 0x5118, "scroll_x": 0x5126, "carry_y": 0x5128,
                "ride_h": 0x513D, "ride_v": 0x5142,
                "c8": 0x2C8, "c6": 0x2C6, "lives": 0x509E,
                "ammo": 0x509C, "invuln": 0xA0EA,
            }[name]
            self.set_global(off, val, 1 if name in (
                "jump_flag", "airborne", "ride_h", "ride_v",
                "hit_lock") else 2)

    # ---------------------------------------------------------- execution
    def _run(self, entry):
        mu = self.mu
        sp = E.STACK_PTR
        sp -= 2
        w16(mu, E.STACK_SEG, sp, RET_SENTINEL)
        mu.reg_write(UC_X86_REG_SP, sp)
        mu.reg_write(UC_X86_REG_CS, 0)
        mu.reg_write(UC_X86_REG_IP, entry)
        self.stopped = None
        try:
            mu.emu_start(entry, 0x110000, count=1_000_000)
        except Exception as exc:  # pragma: no cover
            self.stopped = "error: %s" % exc
        if self.stopped not in (None, "ret"):
            raise RuntimeError("routine %04X stopped: %s" % (entry, self.stopped))

    def step(self, with_objects=True):
        self._run(HORIZ)
        self._run(JUMP)
        if with_objects:
            self._run(OBJECTS)

    def dump(self):
        out = {}
        for name, off, kind in FIELDS:
            mv = self.mu.mem_read((E.DATA_SEG << 4) + off, 1 if kind == "u8" else 2)
            out[name] = mv[0] if kind == "u8" else int.from_bytes(mv, "little",
                                                                 signed=(kind == "s16"))
        return out

    def dump_objects(self):
        return E.dump_objects(self.mu)

    def dump_cells(self):
        return E.dump_cells(self.mu)
