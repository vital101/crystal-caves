#!/usr/bin/env python3
"""Golden-oracle level loader.

Runs the original new-game initializer (0xC3E) and level-load routine
(0xB1A9) inside a Unicorn 16-bit emulator and dumps the resulting object
array, cell map and (optionally) startup globals.  Running 0xC3E first is
required: it supplies gameplay defaults and the level-2 hub door cell IDs.  This settles all
questions about spawn counts, `[`-prefix handling and per-level prologues
without re-deriving the command interpreter by hand.

Memory model (derived from the binary, see notes/objects.md):

  CS = 0            code/image segment, image loaded at linear 0
  DS = 0x2CD3       data segment (startup: `mov dx,0x2CD3; mov ds,dx`)
  SS:SP = 0x370C:4000  from the MZ header

The loader takes one word argument, `0xFFFF - level`, and returns with
`ret 2`.  It jumps to 0xCEEC at the very end (draw map / play sound); at that
point all objects and cells are already written.

Spawn routine is 0x47D with 14 word args (13 values + no, see below) and
`retf 0x1c`.  Calling convention is Turbo Pascal "far" style: args pushed
right-to-left, then `push cs`, then `call 0x47D`.

Outputs:
  notes/oracle_objects.json   per-level dump of object records + spawn log
"""

import collections
import json
import os
import struct
import sys

from unicorn import (
    Uc, UC_ARCH_X86, UC_MODE_16,
    UC_HOOK_CODE, UC_HOOK_INTR, UC_HOOK_MEM_FETCH_UNMAPPED,
)
from unicorn.x86_const import (
    UC_X86_REG_AX, UC_X86_REG_BX, UC_X86_REG_CX, UC_X86_REG_DX,
    UC_X86_REG_SI, UC_X86_REG_DI, UC_X86_REG_BP, UC_X86_REG_SP,
    UC_X86_REG_CS, UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS,
    UC_X86_REG_IP, UC_X86_REG_FLAGS,
)
from capstone import Cs, CS_ARCH_X86, CS_MODE_16, CS_OP_IMM

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
EXE = os.path.join(ROOT, "extracted", "CC1-UNLZ.EXE")
HDR = 0x1E00
NOTE = os.path.join(ROOT, "notes")

LOADER = 0xB1A9
LOAD_END = 0xCEEC           # end-of-load / draw-map sequence
RET_SENTINEL = 0xFFF0

DATA_SEG = 0x2CD3
STACK_SEG = 0x370C
STACK_PTR = 0x4000

OBJ_BASE = 0x45DE
OBJ_STRIDE = 0x2D
OBJ_SLOTS = 60

CELL_BASE = 0x508C
CELL_COL_STRIDE = 500
CELL_ROW_STRIDE = 20
MAP_COLS = 40
MAP_ROWS = 25

SPAWN = 0x47D
# Two sprite draws inside the spawn routine (lcall 0x28B5:0x191F) are patched
# to `add sp,8; nop; nop` because they need the GFX tables.
DRAW_SITES = (0x52B, 0x560)

# Far segments whose routines we stub out entirely (video / sound / utility).
SKIP_SEGS = {0x28B5, 0x26C5, 0x0FC5}

# Boot-state seed written by the original startup path:
#   0x0DE0: mov word ptr [0x2b72], 0xffff   ("no player-start override")
# [0x2b74] is cleared to 0 at boot.  With these set, the Y-token player-start
# path at 0x6c54 is taken and the player spawns at the map Y cell.
BOOT_STATE = {
    0x2b72: 0xffff,
    0x2b74: 0x0000,
}

md = Cs(CS_ARCH_X86, CS_MODE_16)
md.detail = True


def load_module():
    return open(EXE, "rb").read()[HDR:]


def retf_argbytes(module, cs, ip, window=0x300):
    """Scan a routine for its first `retf imm16` and return the arg bytes."""
    lin = (cs << 4) + ip
    if lin >= len(module):
        return 0
    code = module[lin:lin + window]
    for ins in md.disasm(code, ip):
        if ins.mnemonic == "retf":
            if ins.op_str:
                try:
                    return int(ins.op_str, 16)
                except ValueError:
                    return 0
            return 0
        if ins.mnemonic in ("ret", "iret"):
            return 0
    return 0


def u16(mu, seg, off):
    return struct.unpack("<H", mu.mem_read((seg << 4) + off, 2))[0]


def s16(mu, seg, off):
    return struct.unpack("<h", mu.mem_read((seg << 4) + off, 2))[0]


def w16(mu, seg, off, v):
    mu.mem_write((seg << 4) + off, struct.pack("<H", v & 0xFFFF))


OBJ_FIELDS = [
    # (name, offset, signed)
    ("state", 0x00, False),
    ("p02", 0x02, False),
    ("p04", 0x04, False),
    ("x", 0x06, True),
    ("y", 0x08, True),
    ("x_prev", 0x0A, True),
    ("y_prev", 0x0C, True),
    ("tile", 0x0E, False),
    ("p10", 0x10, False),
    ("p12", 0x12, False),
    ("p14", 0x14, False),
    ("p16", 0x16, False),
    ("p18", 0x18, False),
    ("p1a", 0x1A, False),
    ("shape", 0x1C, False),
    ("p1e", 0x1E, False),
    ("p20", 0x20, False),
    ("p22", 0x22, False),
    ("p24", 0x24, False),
    ("kind", 0x26, False),
    ("ready", 0x28, "byte"),
    ("handler_off", 0x29, False),
    ("handler_seg", 0x2B, False),
]


def dump_objects(mu):
    objs = []
    for i in range(OBJ_SLOTS):
        base = OBJ_BASE + i * OBJ_STRIDE
        rec = {"index": i}
        for name, off, kind in OBJ_FIELDS:
            if kind == "byte":
                rec[name] = mu.mem_read((DATA_SEG << 4) + base + off, 1)[0]
            elif kind is True:
                rec[name] = s16(mu, DATA_SEG, base + off)
            else:
                rec[name] = u16(mu, DATA_SEG, base + off)
        objs.append(rec)
    return objs


def dump_cells(mu):
    grid = []
    for row in range(MAP_ROWS):
        line = []
        for col in range(MAP_COLS):
            off = CELL_BASE + (col + 1) * CELL_COL_STRIDE + (row + 1) * CELL_ROW_STRIDE
            b = bytes(mu.mem_read((DATA_SEG << 4) + off, 20))
            line.append(b.hex())
        grid.append(line)
    return grid


def run_level(level, boot_seed=True, verbose=False, startup=True):
    module = load_module()
    mu = Uc(UC_ARCH_X86, UC_MODE_16)
    mu.mem_map(0, 0x110000)
    mu.mem_write(0, module)
    # Exact new-game state (0xC3E) before the loader.  Without this the
    # loader-only image misses door IDs and gameplay defaults.
    if startup:
        import emulate_startup as _S
        ds0, _ = _S.run_startup()
        mu.mem_write(DATA_SEG << 4, ds0)
    for site in DRAW_SITES:
        mu.mem_write(site, b"\x83\xc4\x08\x90\x90")
    mu.mem_write(RET_SENTINEL, b"\xf4")

    mu.reg_write(UC_X86_REG_SS, STACK_SEG)
    mu.reg_write(UC_X86_REG_DS, DATA_SEG)
    mu.reg_write(UC_X86_REG_ES, DATA_SEG)
    mu.reg_write(UC_X86_REG_CS, 0)

    # Prologue chain compares the argument against 0xFFFF, 0xFFFE, 0, 1, ...
    # so levels 0 and 1 use sentinels and levels 2+ pass (level - 2).
    if level == 0:
        arg = 0xFFFF
    elif level == 1:
        arg = 0xFFFE
    else:
        arg = level - 2

    sp = STACK_PTR
    sp -= 2
    w16(mu, STACK_SEG, sp, arg & 0xFFFF)
    sp -= 2
    w16(mu, STACK_SEG, sp, RET_SENTINEL)
    mu.reg_write(UC_X86_REG_SP, sp)

    # Seed boot state: [0x2b72]=0xffff means "no player-start override" so the
    # Y-token start path (0x6c54) is taken. [0x2b74]=0.  Verified against the
    # real argument: the dump is identical whether [0xa0be] holds arg or 0;
    # setting it to arg keeps caller fidelity.
    if boot_seed:
        for addr, val in BOOT_STATE.items():
            w16(mu, DATA_SEG, addr, val)
    w16(mu, DATA_SEG, 0xa0be, arg & 0xFFFF)

    stats = {"steps": 0, "skipped": {}, "ints": {}, "stopped": None}
    spawns = []
    tail = collections.deque(maxlen=48)

    def read_stack_word(off):
        return u16(mu, STACK_SEG, (mu.reg_read(UC_X86_REG_SP) + off) & 0xFFFF)

    def hook_code(uc, address, size, user):
        stats["steps"] += 1
        tail.append((uc.reg_read(UC_X86_REG_CS), address))
        if address == LOAD_END:
            stats["stopped"] = "load_end"
            uc.emu_stop()
            return
        if address == RET_SENTINEL:
            stats["stopped"] = "ret"
            uc.emu_stop()
            return

        cs = uc.reg_read(UC_X86_REG_CS)
        if address == SPAWN and cs == 0:
            args = [read_stack_word(4 + 2 * i) for i in range(14)]
            ret_ip = read_stack_word(0)
            bp = uc.reg_read(UC_X86_REG_BP)
            col = u16(mu, STACK_SEG, (bp - 0x102) & 0xFFFF)
            row = u16(mu, STACK_SEG, (bp + 8) & 0xFFFF)
            ch = None
            if 0 <= col < 40:
                ch = mu.mem_read((STACK_SEG << 4) + ((bp - 0x204 + col) & 0xFFFF), 1)[0]
            spawns.append({
                "slot": args[13],
                "args": args[:13],
                "handler_off": args[0],
                "handler_seg": args[1],
                "ret_ip": ret_ip,
                "col": col,
                "row": row,
                "ch": ch,
            })

        if bytes(mu.mem_read(address, 1))[0] == 0x9A:
            raw = bytes(mu.mem_read(address, 5))
            off = int.from_bytes(raw[1:3], "little")
            seg = int.from_bytes(raw[3:5], "little")
            if seg in SKIP_SEGS:
                key = "%04X:%04X" % (seg, off)
                stats["skipped"][key] = stats["skipped"].get(key, 0) + 1
                n = retf_argbytes(module, seg, off)
                uc.reg_write(UC_X86_REG_SP, (uc.reg_read(UC_X86_REG_SP) + n) & 0xFFFF)
                cs = uc.reg_read(UC_X86_REG_CS)
                uc.reg_write(UC_X86_REG_IP,
                             ((address + 5) - (cs << 4)) & 0xFFFF)
                return

    def hook_intr(uc, intno, user):
        cs = uc.reg_read(UC_X86_REG_CS)
        ip = uc.reg_read(UC_X86_REG_IP)
        ax = uc.reg_read(UC_X86_REG_AX)
        key = "int%02X ah=%02X @%04X:%04X" % (intno, ax >> 8, cs, (ip - 2) & 0xFFFF)
        stats["ints"][key] = stats["ints"].get(key, 0) + 1
        if intno == 0x10 and (ax >> 8) == 0x0F:
            uc.reg_write(UC_X86_REG_AX, (ax & 0xFF00) | 0x13)
        elif intno == 0x16 and (ax >> 8) in (0x01, 0x11):
            uc.reg_write(UC_X86_REG_FLAGS, uc.reg_read(UC_X86_REG_FLAGS) | 0x40)
        uc.reg_write(UC_X86_REG_IP, (ip + 2) & 0xFFFF)

    def hook_bad_fetch(uc, access, address, size, value, user):
        stats["stopped"] = "bad_fetch %05X cs:ip=%04X:%04X" % (
            address, uc.reg_read(UC_X86_REG_CS), uc.reg_read(UC_X86_REG_IP))
        stats["tail"] = ["%04X:%04X" % t for t in tail]
        uc.emu_stop()

    mu.hook_add(UC_HOOK_CODE, hook_code)
    mu.hook_add(UC_HOOK_INTR, hook_intr)
    mu.hook_add(UC_HOOK_MEM_FETCH_UNMAPPED, hook_bad_fetch)

    try:
        mu.emu_start(LOADER, 0x110000, count=20_000_000)
    except Exception as e:
        stats["stopped"] = "error: %s" % e

    return {
        "level": level,
        "arg": arg & 0xFFFF,
        "boot_seed": boot_seed,
        "exe_sha256": sha256_file(EXE),
        "stats": stats,
        "spawns": spawns,
        "objects": dump_objects(mu),
        "cells": dump_cells(mu),
        "cursor": u16(mu, DATA_SEG, 0x5120),
    }


def sha256_file(path):
    import hashlib
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    import argparse
    ap = argparse.ArgumentParser(description="Regenerate per-level oracle dumps")
    ap.add_argument("levels", metavar="LVL", type=int, nargs="*",
                    help="levels to regenerate (default: all 19)")
    ap.add_argument("--out", default=os.path.join(NOTE, "oracle_objects.json"),
                    help="output JSON path")
    ap.add_argument("--only", action="store_true",
                    help="only rewrite the requested levels, keep others")
    args = ap.parse_args()

    levels = args.levels if args.levels else list(range(19))

    path = args.out
    out = {}
    if args.only and os.path.exists(path):
        try:
            out = json.load(open(path))
        except Exception:
            out = {}

    for lv in levels:
        r = run_level(lv)
        live = [o for o in r["objects"] if o["state"] not in (0x63, 0xFFFF)]
        out[str(lv)] = r
        print("level %2d: stop=%-10s steps=%-7d spawns=%-3d live=%-3d cursor=%d ints=%s"
              % (lv, r["stats"]["stopped"], r["stats"]["steps"], len(r["spawns"]),
                 len(live), r["cursor"], len(r["stats"]["ints"])))
        if r["stats"]["stopped"] not in ("ret", "load_end"):
            print("   tail:", " ".join(r["stats"].get("tail", [])[-20:]))
            print("   skipped:", dict(list(r["stats"]["skipped"].items())[:10]))

    # Atomic write: temp file then rename.
    tmp = path + ".tmp"
    with open(tmp, "w") as f:
        json.dump(out, f, indent=1)
    os.replace(tmp, path)
    print("wrote", path, "levels:", len(out))


if __name__ == "__main__":
    main()
