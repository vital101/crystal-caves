#!/usr/bin/env python3
"""Oracle captures for the Phase 5 HUD.

Runs the real HUD routine (0x0FC5:0xD1E0, linear 0x1CE30) in Unicorn with the
original MNI font buffers and the EGA plane hooks, then writes
notes/hud_fixtures.json.  Each fixture stores the seeded globals and an RLE
encoding of the 320x200 indexed framebuffer.
"""
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import emulate_startup as S  # noqa: E402
import emulate_load as E  # noqa: E402

from unicorn import (Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_CODE, UC_HOOK_INTR,
                     UC_HOOK_MEM_WRITE, UC_HOOK_INSN)  # noqa: E402
from unicorn.x86_const import (  # noqa: E402
    UC_X86_REG_CS, UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS, UC_X86_REG_SP,
    UC_X86_REG_IP, UC_X86_REG_FLAGS, UC_X86_INS_OUT, UC_X86_INS_IN,
)

VGA = 0xA0000
RET = 0xFFF0
FONT_BASE = 0x50000
STRIDE_ADDR = (0x28B5 << 4) + 0x60
BANK_ADDR = (0x28B5 << 4) + 0xB8

CASES = [
    {"name": "empty", "globals": {0x509E: 3, 0x509C: 0, 0x50A2: 0,
                                  0x50A5: 0, 0x50A7: 0, 0xA0C4: 0xFFFF,
                                  0xA0C6: 0, 0xA0F9: 0, 0xA0FB: 0,
                                  0x512E: 0}},
    {"name": "play", "globals": {0x509E: 2, 0x509C: 27, 0x50A2: 12,
                                 0x50A5: 2407, 0x50A7: 0, 0xA0C4: 12,
                                 0xA0C6: 0, 0xA0F9: 1, 0xA0FB: 0,
                                 0x512E: 5}},
    {"name": "max", "globals": {0x509E: 8, 0x509C: 999, 0x50A2: 9999,
                                0x50A5: 0xFFFF, 0x50A7: 0x00FF,
                                0xA0C4: 0, 0xA0C6: 17, 0xA0F9: 1,
                                0xA0FB: 0, 0x512E: 7}},
    {"name": "all_caves", "globals": {0x509E: 3, 0x509C: 12, 0x50A2: 1234,
                                      0x50A5: 1234567, 0x50A7: 0,
                                      0xA0C4: 0xFFFF, 0xA0C6: 0,
                                      0xA0F9: 1, 0xA0FB: 1, 0x512E: 0}},
]


def make_machine(ds0):
    module = E.load_module()
    mu = Uc(UC_ARCH_X86, UC_MODE_16)
    mu.mem_map(0, 0x110000)
    mu.mem_write(0, module)
    mu.mem_write(E.DATA_SEG << 4, ds0)
    for i, fn in enumerate(("CC1-F1.MNI", "CC1-F2.MNI", "CC1-SPL.MNI")):
        raw = open(os.path.join(ROOT, "extracted", fn), "rb").read()
        base = FONT_BASE + i * 0x2000
        mu.mem_write(base, raw)
        E.w16(mu, E.DATA_SEG, 0x337C + i * 4, base & 0xF)
        E.w16(mu, E.DATA_SEG, 0x337C + i * 4 + 2, base >> 4)
    mu.mem_write(STRIDE_ADDR, struct.pack("<H", 0x50))
    mu.mem_write(BANK_ADDR, struct.pack("<H", 0))
    mu.reg_write(UC_X86_REG_SS, E.STACK_SEG)
    mu.reg_write(UC_X86_REG_DS, E.DATA_SEG)
    mu.reg_write(UC_X86_REG_ES, E.DATA_SEG)
    mu.reg_write(UC_X86_REG_CS, 0x0FC5)
    return mu


def capture(mu):
    state = {"seq": 0, "mm": 0, "gfx": 0, "bm": 0xFF}
    planes = [bytearray(0x10000) for _ in range(4)]

    def apply_out(port, v):
        if port == 0x3C4:
            state["seq"] = v & 0xFF
        elif port == 0x3C5 and state["seq"] == 2:
            state["mm"] = v & 0xFF
        elif port == 0x3CE:
            state["gfx"] = v & 0xFF
        elif port == 0x3CF and state["gfx"] == 8:
            state["bm"] = v & 0xFF

    def out_hook(uc, port, size, value, user):
        if size == 2:
            apply_out(port, value & 0xFF)
            apply_out(port + 1, (value >> 8) & 0xFF)
        else:
            apply_out(port, value & 0xFF)

    def in_hook(uc, port, size, user):
        return 0

    def wr_hook(uc, access, addr, size, value, user):
        if not (VGA <= addr < VGA + 0x10000):
            return
        off = addr - VGA
        mm, bm, v = state["mm"], state["bm"], value & 0xFF
        if mm == 0:
            return
        for p in range(4):
            if mm >> p & 1:
                planes[p][off] = (v & bm) | (planes[p][off] & ~bm & 0xFF)

    mu.hook_add(UC_HOOK_INSN, out_hook, None, 1, 0, UC_X86_INS_OUT)
    mu.hook_add(UC_HOOK_INSN, in_hook, None, 1, 0, UC_X86_INS_IN)
    mu.hook_add(UC_HOOK_MEM_WRITE, wr_hook)

    def intr(uc, intno, user):
        uc.reg_write(UC_X86_REG_IP, (uc.reg_read(UC_X86_REG_IP) + 2) & 0xFFFF)
    mu.hook_add(UC_HOOK_INTR, intr)

    def code(uc, addr, size, user):
        if addr == RET:
            uc.emu_stop()
    mu.hook_add(UC_HOOK_CODE, code)

    sp = E.STACK_PTR
    sp -= 2
    E.w16(mu, E.STACK_SEG, sp, RET)
    mu.reg_write(UC_X86_REG_SP, sp)
    mu.reg_write(UC_X86_REG_IP, 0xD1E0)
    try:
        mu.emu_start((0x0FC5 << 4) + 0xD1E0,
                     (0x0FC5 << 4) + 0xD1E0 + 0x10000, count=2_000_000)
    except Exception as exc:
        raise RuntimeError("HUD emulation failed: %s" % exc)

    idx = bytearray(320 * 200)
    for y in range(200):
        for x in range(320):
            b = y * 80 + (x >> 3)
            bit = 7 - (x & 7)
            v = 0
            for p in range(4):
                if planes[p][b] >> bit & 1:
                    v |= 1 << p
            idx[y * 320 + x] = v
    return idx


def rle_rows(idx):
    rows = []
    for y in range(200):
        row = []
        x = 0
        while x < 320:
            c = idx[y * 320 + x]
            n = 1
            while x + n < 320 and idx[y * 320 + x + n] == c:
                n += 1
            row.append([n, c])
            x += n
        rows.append(row)
    return rows


def main():
    ds0, _ = S.run_startup()
    cases = []
    for case in CASES:
        mu = make_machine(ds0)
        for off, val in case["globals"].items():
            E.w16(mu, E.DATA_SEG, off, val & 0xFFFF)
        idx = capture(mu)
        # Read back the timers after the HUD draw (the original mutates them).
        after = {
            "a0c4": E.u16(mu, E.DATA_SEG, 0xA0C4),
            "a0c6": E.u16(mu, E.DATA_SEG, 0xA0C6),
        }
        cases.append({"name": case["name"], "globals": case["globals"],
                      "after": after, "rows": rle_rows(idx)})
        print("captured", case["name"])
    out = os.path.join(ROOT, "notes", "hud_fixtures.json")
    with open(out, "w") as f:
        json.dump({"cases": cases}, f)
    print("wrote", out)


if __name__ == "__main__":
    main()
