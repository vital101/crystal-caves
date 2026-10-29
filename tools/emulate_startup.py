#!/usr/bin/env python3
"""Capture the original game's startup data segment and per-level start state.

Why this exists
---------------
The level loader (0xB1A9) only writes objects/cells/player/camera.  Gameplay
defaults (gravity table, walk speed, phase counters, lives/ammo, ...) are
written by the new-game initializer at 0xC3E.  Starting the emulator at the
loader with a zeroed DS therefore loses all of that state.

This harness runs the real initializer first, snapshots the complete DS image,
then runs the real loader for every level and records the resulting gameplay
globals.  `tools/make_startup_data.py` turns the resulting JSON into the C
header used by the wasm port.

Address model: CS=0, DS=ES=0x2CD3, SS:SP=0x370C:0x4000.
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

import emulate_load as E  # noqa: E402

from unicorn import (  # noqa: E402
    Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_CODE, UC_HOOK_INTR,
)
from unicorn.x86_const import (  # noqa: E402
    UC_X86_REG_CS, UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS,
    UC_X86_REG_SP, UC_X86_REG_IP, UC_X86_REG_AX, UC_X86_REG_FLAGS,
)

STARTUP = 0xC3E
LOADER = 0xB1A9
LOAD_END = 0xCEEC
RET_SENTINEL = 0xFFF0
SKIP_SEGS = (0x28B5, 0x26C5, 0x0FC5)

# Named gameplay globals captured after startup and after each level load.
# (name, DS offset, width) -- width is 1 or 2.
GLOBALS = [
    ("a0f1", 0xA0F1, 1),
    ("a0f2", 0xA0F2, 1),
    ("a0f4", 0xA0F4, 1),
    ("a0f6", 0xA0F6, 1),
    ("a0f7", 0xA0F7, 1),
    ("a0f8", 0xA0F8, 1),
    ("a0fc", 0xA0FC, 1),
    ("a0fd", 0xA0FD, 1),
    ("a0ea", 0xA0EA, 2),
    ("a0b4", 0xA0B4, 2),
    ("a0b8", 0xA0B8, 2),
    ("2ba", 0x02BA, 2),
    ("2ca", 0x02CA, 2),
    ("2c8", 0x02C8, 2),
    ("2c6", 0x02C6, 2),
    ("2e0", 0x02E0, 2),
    ("306", 0x0306, 1),
    ("30a", 0x030A, 1),
    ("30b", 0x030B, 1),
    ("30c", 0x030C, 1),
    ("461b", 0x461B, 2),
    ("461d", 0x461D, 2),
    ("461f", 0x461F, 2),
    ("4627", 0x4627, 2),
    ("4629", 0x4629, 2),
    ("460d", 0x460D, 2),
    ("4611", 0x4611, 2),
    ("4613", 0x4613, 2),
    ("4619", 0x4619, 2),
    ("5118", 0x5118, 2),
    ("5126", 0x5126, 2),
    ("5128", 0x5128, 2),
    ("5124", 0x5124, 2),
    ("512e", 0x512E, 2),
    ("5130", 0x5130, 2),
    ("5132", 0x5132, 2),
    ("5134", 0x5134, 2),
    ("513c", 0x513C, 1),
    ("513d", 0x513D, 1),
    ("5140", 0x5140, 1),
    ("5142", 0x5142, 1),
    ("509c", 0x509C, 2),
    ("509e", 0x509E, 2),
    ("4660", 0x4660, 1),
    ("4652", 0x4652, 2),
    ("2b60", 0x2B60, 2),
    ("a0c0", 0xA0C0, 2),
    # Phase 2: combat/fire/RNG/restart state.
    ("2a6", 0x02A6, 2),
    ("2a8", 0x02A8, 2),
    ("2da", 0x02DA, 2),
    ("2f2", 0x02F2, 2),
    ("2b8", 0x02B8, 2),
    ("511e", 0x511E, 2),
    ("5122", 0x5122, 2),
    ("30d", 0x030D, 1),
    ("50a5", 0x50A5, 2),
    ("50a7", 0x50A7, 2),
    ("50a0", 0x50A0, 2),
    ("50be", 0x50BE, 2),
    ("50c4", 0x50C4, 2),
    ("50c7", 0x50C7, 2),
    ("50c9", 0x50C9, 2),
    ("a0ff", 0xA0FF, 1),
    ("a0c4", 0xA0C4, 2),
    # EBF2 spawner/exit branch guards (oracle-only; not yet in C).
    ("a0ce", 0xA0CE, 2),
    ("a10b", 0xA10B, 1),
    ("a0cc", 0xA0CC, 2),
    ("a0f5", 0xA0F5, 1),
    ("a0be", 0xA0BE, 2),
]

# Vertical displacement table used by 0xDCB9/0xE36B.  Base is DS:0xA111 and
# the index is the value of [0x5134] (0..0x13); index 0 is unused.
VERT_TABLE_BASE = 0xA111
VERT_TABLE_LEN = 20


def _u16(mu, off):
    return int.from_bytes(mu.mem_read((E.DATA_SEG << 4) + off, 2), "little")


def _u8(mu, off):
    return mu.mem_read((E.DATA_SEG << 4) + off, 1)[0]


def _globals(mu):
    out = {}
    for name, off, width in GLOBALS:
        out[name] = _u8(mu, off) if width == 1 else _u16(mu, off)
    return out


def _make_machine():
    module = E.load_module()
    mu = Uc(UC_ARCH_X86, UC_MODE_16)
    mu.mem_map(0, 0x110000)
    mu.mem_write(0, module)
    mu.mem_write(RET_SENTINEL, b"\xf4")
    for site in E.DRAW_SITES:
        mu.mem_write(site, b"\x83\xc4\x08\x90\x90")
    mu.reg_write(UC_X86_REG_SS, E.STACK_SEG)
    mu.reg_write(UC_X86_REG_DS, E.DATA_SEG)
    mu.reg_write(UC_X86_REG_ES, E.DATA_SEG)
    mu.reg_write(UC_X86_REG_CS, 0)
    return mu, module


def _install_hooks(mu, module, stop_addr, state):
    def hook_code(uc, address, size, user):
        state["steps"] += 1
        if address == RET_SENTINEL:
            state["stopped"] = "ret"
            uc.emu_stop()
            return
        if address == stop_addr:
            state["stopped"] = "stop"
            uc.emu_stop()
            return
        # Detect far calls without disassembling every instruction: opcode 9A.
        if uc.mem_read(address, 1)[0] == 0x9A:
            raw = uc.mem_read(address, 5)
            off = int.from_bytes(raw[1:3], "little")
            seg = int.from_bytes(raw[3:5], "little")
            if seg in SKIP_SEGS:
                n = E.retf_argbytes(module, seg, off)
                sp = uc.reg_read(UC_X86_REG_SP)
                uc.reg_write(UC_X86_REG_SP, (sp + n) & 0xFFFF)
                cs = uc.reg_read(UC_X86_REG_CS)
                uc.reg_write(UC_X86_REG_IP,
                             ((address + 5) - (cs << 4)) & 0xFFFF)
                state["skipped"][f"{seg:04X}:{off:04X}"] = (
                    state["skipped"].get(f"{seg:04X}:{off:04X}", 0) + 1
                )

    def hook_intr(uc, intno, user):
        cs = uc.reg_read(UC_X86_REG_CS)
        ip = uc.reg_read(UC_X86_REG_IP)
        ax = uc.reg_read(UC_X86_REG_AX)
        key = "int%02X ah=%02X @%04X:%04X" % (intno, ax >> 8, cs, (ip - 2) & 0xFFFF)
        state["ints"][key] = state["ints"].get(key, 0) + 1
        if intno == 0x10 and (ax >> 8) == 0x0F:
            uc.reg_write(UC_X86_REG_AX, (ax & 0xFF00) | 0x13)
        elif intno == 0x16 and (ax >> 8) in (0x01, 0x11):
            uc.reg_write(UC_X86_REG_FLAGS, uc.reg_read(UC_X86_REG_FLAGS) | 0x40)
        uc.reg_write(UC_X86_REG_IP, (ip + 2) & 0xFFFF)

    mu.hook_add(UC_HOOK_CODE, hook_code)
    mu.hook_add(UC_HOOK_INTR, hook_intr)


def _call(mu, entry, push_arg=None):
    sp = E.STACK_PTR
    if push_arg is not None:
        sp -= 2
        E.w16(mu, E.STACK_SEG, sp, push_arg & 0xFFFF)
    sp -= 2
    E.w16(mu, E.STACK_SEG, sp, RET_SENTINEL)
    mu.reg_write(UC_X86_REG_SP, sp)
    mu.reg_write(UC_X86_REG_CS, 0)
    mu.reg_write(UC_X86_REG_IP, entry)


def run_startup():
    """Run the new-game initializer and return (full DS image, globals)."""
    mu, module = _make_machine()
    state = {"steps": 0, "stopped": None, "skipped": {}, "ints": {}}
    _install_hooks(mu, module, RET_SENTINEL, state)
    _call(mu, STARTUP)
    try:
        mu.emu_start(STARTUP, 0x110000, count=5_000_000)
    except Exception as exc:  # pragma: no cover - diagnostic path
        state["stopped"] = "error: %s" % exc
    if state["stopped"] != "ret":
        raise RuntimeError("startup emulation failed: %s" % state)
    ds = bytes(mu.mem_read(E.DATA_SEG << 4, 0x10000))
    return ds, _globals(mu)


def _load_one(mu, module, level):
    arg = 0xFFFF if level == 0 else (0xFFFE if level == 1 else level - 2)
    E.w16(mu, E.DATA_SEG, 0x2B72, 0xFFFF)
    E.w16(mu, E.DATA_SEG, 0x2B74, 0x0000)
    E.w16(mu, E.DATA_SEG, 0xA0BE, arg & 0xFFFF)
    state = {"steps": 0, "stopped": None, "skipped": {}, "ints": {}}
    _install_hooks(mu, module, LOAD_END, state)
    _call(mu, LOADER, arg)
    try:
        mu.emu_start(LOADER, 0x110000, count=20_000_000)
    except Exception as exc:  # pragma: no cover
        state["stopped"] = "error: %s" % exc
    if state["stopped"] != "stop":
        raise RuntimeError("level %d load failed: %s" % (level, state))
    return state


def run_level(level, include_ds=False):
    """Run startup + loader for one level; return state and optional DS image."""
    ds0, startup_globals = run_startup()
    mu, module = _make_machine()
    mu.mem_write(E.DATA_SEG << 4, ds0)
    _load_one(mu, module, level)
    result = {
        "level": level,
        "startup_globals": startup_globals,
        "globals": _globals(mu),
        "objects": E.dump_objects(mu),
        "cells": E.dump_cells(mu),
        "cursor": _u16(mu, 0x5120),
        "table": [_u8(mu, VERT_TABLE_BASE + i) for i in range(VERT_TABLE_LEN)],
    }
    if include_ds:
        result["ds"] = bytes(mu.mem_read(E.DATA_SEG << 4, 0x10000))
    return result


def run_all(include_ds=False):
    """Run startup once and each level in sequence (fast path for regeneration)."""
    ds0, startup_globals = run_startup()
    levels = {}
    last = None
    for level in range(19):
        mu, module = _make_machine()
        mu.mem_write(E.DATA_SEG << 4, ds0)
        _load_one(mu, module, level)
        last = mu
        levels[str(level)] = {
            "globals": _globals(mu),
            "cursor": _u16(mu, 0x5120),
        }
        if include_ds:
            levels[str(level)]["ds"] = bytes(mu.mem_read(E.DATA_SEG << 4, 0x10000))
    return {
        "exe_sha256": E.sha256_file(E.EXE),
        "data_seg": E.DATA_SEG,
        "table_base": VERT_TABLE_BASE,
        "table": [_u8(last, VERT_TABLE_BASE + i) for i in range(VERT_TABLE_LEN)],
        "startup_globals": startup_globals,
        "levels": levels,
    }


def main():
    out = os.path.join(ROOT, "notes", "startup.json")
    data = run_all()
    tmp = out + ".tmp"
    with open(tmp, "w") as f:
        json.dump(data, f, indent=1)
    os.replace(tmp, out)
    print("wrote", out)
    print("table:", data["table"])
    for lv in (2, 3):
        g = data["levels"][str(lv)]["globals"]
        print("L%02d player=(%d,%d) cam=(%d,%d) 5132=%d 5134=%d" % (
            lv, g["4611"], g["4613"], g["512e"], g["5130"],
            g["5132"], g["5134"]))


if __name__ == "__main__":
    main()
