#!/usr/bin/env python3
"""Oracle captures for the Phase 5 HUD and text windows.

Runs the real HUD routine (0x0FC5:0xD1E0, linear 0x1CE30) and the real window
renderer (0x0FC5:0x0877, linear 0x104C7) in Unicorn with the original MNI font
buffers and EGA plane hooks.  Writes notes/hud_fixtures.json (RLE rows) and
notes/text_fixtures.json (zlib/base64 framebuffers captured from the original
message wrappers and line tables).
"""
import base64
import hashlib
import json
import os
import struct
import sys
import zlib

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


def capture(mu, ip=0xD1E0, wrapper=False):
    state = {"seq": 0, "mm": 0, "gfx": 0, "bm": 0xFF}
    planes = [bytearray(0x10000) for _ in range(4)]
    render_ret = [None]

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
        if wrapper:
            if addr == 0x104C7:
                sp = uc.reg_read(UC_X86_REG_SP)
                ss = uc.reg_read(UC_X86_REG_SS)
                base = (ss << 4) + sp
                ret_ip = int.from_bytes(uc.mem_read(base, 2), "little")
                ret_cs = int.from_bytes(uc.mem_read(base + 2, 2), "little")
                render_ret[0] = (ret_cs, ret_ip)
            elif render_ret[0] is not None:
                cur = (uc.reg_read(UC_X86_REG_CS), uc.reg_read(UC_X86_REG_IP))
                if cur == render_ret[0]:
                    uc.emu_stop()
                    return
        if addr == RET:
            uc.emu_stop()
    mu.hook_add(UC_HOOK_CODE, code)

    sp = E.STACK_PTR
    sp -= 2
    E.w16(mu, E.STACK_SEG, sp, RET)
    mu.reg_write(UC_X86_REG_SP, sp)
    mu.reg_write(UC_X86_REG_IP, ip & 0xFFFF)
    try:
        mu.emu_start((0x0FC5 << 4) + (ip & 0xFFFF),
                     (0x0FC5 << 4) + (ip & 0xFFFF) + 0x10000, count=3_000_000)
    except Exception as exc:
        raise RuntimeError("shell emulation failed: %s" % exc)

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


TEXT_JSON = os.path.join(ROOT, "out", "portable", "shell_text.json")

# Real wrapper entry points (linear addresses) for message windows.  Each
# wrapper copies its table into DS:0x163C and calls the renderer at
# 0x0FC5:0x0877; we stop as soon as that renderer returns.
WRAPPER_BLOCKS = {
    "message_pause": 0x13035,
    "message_last_crystal": 0x157DE,
    "message_stunned": 0x15915,
    "message_more_crystals": 0x15A56,
    "message_red_mushroom": 0x16327,
    "message_lever": 0x163F3,
    "message_power_shot": 0x1659D,
    "message_poison": 0x166A8,
    "message_air_generator": 0x16B6A,
    "message_gravity": 0x1A0B7,
    "message_perfect_health": 0x1A1ED,
    "message_file_error": 0x15B44,
    "message_saved": 0x16868,
    "message_save_hub_only": 0x16A4D,
}

# Blocks whose original window is not reached through a single message
# wrapper (menus, story pages, instructions).  They are still captured with
# the real renderer, using the exact block lines.
GENERIC_BLOCKS = [
    "story_twibbles",
    "instructions_1",
    "menu_main",
    "menu_help",
    "about",
    "message_joystick_calibrate",
    "menu_warp",
]

# Camera/scroll variants exercise the original positioning arithmetic.
GEOMETRY_CASES = [
    ("message_perfect_health", 8, 8, 8),
    ("message_perfect_health", -8, 0, -8),
]


def load_text_blocks():
    return json.load(open(TEXT_JSON))["blocks"]


def write_text_lines(mu, lines):
    for i, s in enumerate(lines, start=1):
        raw = s.encode("latin1")
        mu.mem_write(E.DATA_SEG * 16 + 0x163C + i * 0x100,
                     bytes([len(raw)]) + raw)
    sentinel = b"\rEND OF WINDOW"
    i = len(lines) + 1
    mu.mem_write(E.DATA_SEG * 16 + 0x163C + i * 0x100, sentinel)


def read_text_lines(mu):
    out = []
    for i in range(1, 21):
        base = E.DATA_SEG * 16 + 0x163C + i * 0x100
        n = int.from_bytes(mu.mem_read(base, 1), "little")
        s = bytes(mu.mem_read(base + 1, n)).decode("latin1") if n else ""
        out.append(s)
        if s == "END OF WINDOW":
            break
    return out


def capture_text(block, cam_x=0, cam_y=0, scroll=0, wrapper=None):
    ds0, _ = S.run_startup()
    mu = make_machine(ds0)
    lines = block["lines"]
    write_text_lines(mu, lines)
    for off, val in ((0x2B3C, 0), (0x2B3E, 0xA000), (0x2B5C, scroll),
                     (0x512E, cam_x), (0x5130, cam_y), (0x2B40, 1),
                     (0x2B5E, 0), (0x2B60, 0)):
        E.w16(mu, E.DATA_SEG, off, val & 0xFFFF)
    ip = (wrapper - 0x0FC5 * 16) if wrapper is not None else 0x877
    idx = capture(mu, ip=ip, wrapper=wrapper is not None)
    got = read_text_lines(mu)
    want = [s for s in lines] + ["END OF WINDOW"]
    if got != want:
        raise RuntimeError("text line mismatch for %s:\n got  %r\n want %r"
                           % (block["id"], got, want))
    return idx


def text_case(name, block, block_index, idx, wrapper=None,
              cam_x=0, cam_y=0, scroll=0):
    raw = bytes(idx)
    xs = [i % 320 for i, v in enumerate(idx) if v]
    ys = [i // 320 for i, v in enumerate(idx) if v]
    bbox = [min(xs), min(ys), max(xs), max(ys)] if xs else None
    return {
        "name": name,
        "block": block["id"],
        "block_index": block_index,
        "lines": block["lines"],
        "mode": "wrapper" if wrapper is not None else "generic",
        "wrapper": wrapper,
        "camera": {"x": cam_x, "y": cam_y, "scroll": scroll},
        "globals": {0x512E: cam_x & 0xFFFF, 0x5130: cam_y & 0xFFFF,
                    0x2B5C: scroll & 0xFFFF},
        "bbox": bbox,
        "fb_len": len(raw),
        "fb_sha256": hashlib.sha256(raw).hexdigest(),
        "fb_b64": base64.b64encode(zlib.compress(raw, 9)).decode("ascii"),
    }


def capture_text_fixtures():
    blocks = load_text_blocks()
    by_id = {b["id"]: b for b in blocks}
    index = {b["id"]: i for i, b in enumerate(blocks)}
    cases = []
    for bid, entry in WRAPPER_BLOCKS.items():
        b = by_id[bid]
        idx = capture_text(b, wrapper=entry)
        cases.append(text_case(bid + "_wrapper", b, index[bid], idx,
                               wrapper=entry))
        print("captured text", bid, "wrapper", hex(entry))
    for bid in GENERIC_BLOCKS:
        b = by_id[bid]
        idx = capture_text(b)
        cases.append(text_case(bid + "_generic", b, index[bid], idx))
        print("captured text", bid, "generic")
    for bid, cam_x, cam_y, scroll in GEOMETRY_CASES:
        b = by_id[bid]
        idx = capture_text(b, cam_x=cam_x, cam_y=cam_y, scroll=scroll)
        name = "%s_cam%d_%d_scroll%d" % (bid, cam_x, cam_y, scroll)
        cases.append(text_case(name, b, index[bid], idx,
                               cam_x=cam_x, cam_y=cam_y, scroll=scroll))
        print("captured text", name)
    return cases


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

    text_out = os.path.join(ROOT, "notes", "text_fixtures.json")
    text_cases = capture_text_fixtures()
    with open(text_out, "w") as f:
        json.dump({"cases": text_cases}, f)
    print("wrote", text_out)


if __name__ == "__main__":
    main()
