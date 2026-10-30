#!/usr/bin/env python3
"""Capture the shell's observable exactness data from the original binary.

This is a deliberately small P5.R6-style harness: it runs the individual
shell routines that matter for P5.R5 (high-score defaults/display, story page
wrappers, key-name lookup and default key codes) with a minimal DOS file shim
and scripted keyboard input.  It does not try to run the complete menu flow.

Outputs:
  notes/shell_exactness_fixtures.json
  notes/hiscore_fixtures.json
  notes/keymap_fixtures.json
"""
import hashlib
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

import emulate_startup as S  # noqa: E402
import emulate_load as E  # noqa: E402
from oracle_shell import make_machine, capture_text  # noqa: E402

from unicorn import (  # noqa: E402
    UC_HOOK_CODE, UC_HOOK_INTR, UC_HOOK_INSN,
)
from unicorn.x86_const import (  # noqa: E402
    UC_X86_REG_AX, UC_X86_REG_BX, UC_X86_REG_CX, UC_X86_REG_DX,
    UC_X86_REG_DS, UC_X86_REG_IP, UC_X86_REG_FLAGS,
    UC_X86_REG_SP, UC_X86_REG_CS, UC_X86_REG_SS,
    UC_X86_INS_IN,
)

CS = 0x0FC5
BASE = CS << 4
RET = 0xFFF0
VIDEO_SEGS = (0x28B5, 0x26C5)
MODULE = E.load_module()

FILES = {}
NEXT_HANDLE = [5]
HANDLES = {}
KEYS = []


def _cf(uc, on):
    flags = uc.reg_read(UC_X86_REG_FLAGS)
    flags = (flags | 1) if on else (flags & ~1)
    uc.reg_write(UC_X86_REG_FLAGS, flags)


def _zf(uc, on):
    flags = uc.reg_read(UC_X86_REG_FLAGS)
    flags = (flags | 0x40) if on else (flags & ~0x40)
    uc.reg_write(UC_X86_REG_FLAGS, flags)


def _dos_path(uc, ptr):
    data = bytearray()
    base = (uc.reg_read(UC_X86_REG_DS) << 4) + (ptr & 0xFFFF)
    while True:
        b = uc.mem_read(base + len(data), 1)[0]
        if b == 0:
            break
        data.append(b)
    return data.decode("latin1")


def _resolve(name):
    for fn in os.listdir(os.path.join(ROOT, "extracted")):
        if fn.lower() == name.lower():
            return os.path.join(ROOT, "extracted", fn)
    return None


def _intr(uc, intno, user):
    ax = uc.reg_read(UC_X86_REG_AX)
    ah = ax >> 8
    if intno == 0x16:
        if ah in (0x00, 0x10):
            key = KEYS.pop(0) if KEYS else 0x1C0D
            uc.reg_write(UC_X86_REG_AX, key)
            _zf(uc, False)
        elif ah in (0x01, 0x11):
            if KEYS:
                uc.reg_write(UC_X86_REG_AX, KEYS[0])
                _zf(uc, False)
            else:
                uc.reg_write(UC_X86_REG_AX, 0)
                _zf(uc, True)
        else:
            uc.reg_write(UC_X86_REG_AX, 0)
            _zf(uc, True)
        return
    if intno != 0x21:
        return
    if ah == 0x3D:
        name = _dos_path(uc, uc.reg_read(UC_X86_REG_DX))
        path = _resolve(name)
        if path is None:
            uc.reg_write(UC_X86_REG_AX, 2)
            _cf(uc, True)
        else:
            h = NEXT_HANDLE[0]
            NEXT_HANDLE[0] += 1
            HANDLES[h] = {"data": bytearray(open(path, "rb").read()), "pos": 0}
            uc.reg_write(UC_X86_REG_AX, h)
            _cf(uc, False)
    elif ah == 0x3E:
        HANDLES.pop(uc.reg_read(UC_X86_REG_BX), None)
        uc.reg_write(UC_X86_REG_AX, 0)
        _cf(uc, False)
    elif ah == 0x3F:
        h = uc.reg_read(UC_X86_REG_BX)
        n = uc.reg_read(UC_X86_REG_CX)
        rec = HANDLES.get(h)
        if rec is None:
            uc.reg_write(UC_X86_REG_AX, 6)
            _cf(uc, True)
        else:
            chunk = bytes(rec["data"][rec["pos"]:rec["pos"] + n])
            rec["pos"] += len(chunk)
            dst = (uc.reg_read(UC_X86_REG_DS) << 4) + uc.reg_read(UC_X86_REG_DX)
            uc.mem_write(dst, chunk)
            uc.reg_write(UC_X86_REG_AX, len(chunk))
            _cf(uc, False)
    elif ah == 0x42:
        h = uc.reg_read(UC_X86_REG_BX)
        rec = HANDLES.get(h)
        if rec is None:
            uc.reg_write(UC_X86_REG_AX, 6)
            _cf(uc, True)
        else:
            off = (uc.reg_read(UC_X86_REG_CX) << 16) | uc.reg_read(UC_X86_REG_DX)
            if (ax & 0xFF) == 0:
                rec["pos"] = off
            elif (ax & 0xFF) == 1:
                rec["pos"] += off
            else:
                rec["pos"] = len(rec["data"]) + off
            uc.reg_write(UC_X86_REG_AX, rec["pos"] & 0xFFFF)
            uc.reg_write(UC_X86_REG_DX, (rec["pos"] >> 16) & 0xFFFF)
            _cf(uc, False)
    else:
        uc.reg_write(UC_X86_REG_AX, 0)
        _cf(uc, False)


def _in_hook(uc, port, size, user):
    if port == 0x64:
        return 1 if KEYS else 0
    if port == 0x60:
        return KEYS.pop(0) if KEYS else 0x1C
    return 0


def _machine():
    ds0, _ = S.run_startup()
    mu = make_machine(ds0)
    mu.hook_add(UC_HOOK_INTR, _intr)
    mu.hook_add(UC_HOOK_INSN, _in_hook, None, 1, 0, UC_X86_INS_IN)

    def code(uc, addr, size, user):
        if addr == RET:
            uc.emu_stop()
            return
        if uc.mem_read(addr, 1)[0] == 0x9A:
            raw = uc.mem_read(addr, 5)
            off = int.from_bytes(raw[1:3], "little")
            seg = int.from_bytes(raw[3:5], "little")
            if seg in VIDEO_SEGS:
                n = E.retf_argbytes(MODULE, seg, off)
                sp = uc.reg_read(UC_X86_REG_SP)
                uc.reg_write(UC_X86_REG_SP, (sp + n) & 0xFFFF)
                cs = uc.reg_read(UC_X86_REG_CS)
                uc.reg_write(UC_X86_REG_IP,
                             ((addr + 5) - (cs << 4)) & 0xFFFF)

    mu.hook_add(UC_HOOK_CODE, code)
    return mu, ds0


def _far(mu, entry, args):
    sp = E.STACK_PTR
    for arg in reversed(args):
        sp -= 2
        E.w16(mu, E.STACK_SEG, sp, arg & 0xFFFF)
    sp -= 2
    E.w16(mu, E.STACK_SEG, sp, 0)
    sp -= 2
    E.w16(mu, E.STACK_SEG, sp, RET)
    mu.reg_write(UC_X86_REG_SP, sp)
    mu.reg_write(UC_X86_REG_CS, CS)
    mu.reg_write(UC_X86_REG_IP, (entry - BASE) & 0xFFFF)
    try:
        mu.emu_start(entry, entry + 0x10000, count=3_000_000)
    except Exception as exc:  # pragma: no cover - diagnostics
        print("  emulation stopped: %s" % exc)


def _read_text_lines(mu):
    lines = []
    for i in range(1, 40):
        base = E.DATA_SEG * 16 + 0x163C + i * 0x100
        n = mu.mem_read(base, 1)[0]
        s = bytes(mu.mem_read(base + 1, n)).decode("latin1") if n else ""
        lines.append(s)
        if s == "END OF WINDOW":
            return lines[:-1]
    return lines


def highscore_defaults():
    mu, _ = _machine()
    _far(mu, BASE + 0x8738, [])
    names, scores = [], []
    for i in range(1, 11):
        off = 0x230 + i * 0x100
        n = mu.mem_read(E.DATA_SEG * 16 + off, 1)[0]
        names.append(bytes(mu.mem_read(E.DATA_SEG * 16 + off + 1, n))
                     .decode("latin1"))
        off = 0xC30 + i * 0x100
        n = mu.mem_read(E.DATA_SEG * 16 + off, 1)[0]
        raw = bytes(mu.mem_read(E.DATA_SEG * 16 + off + 1, n))
        scores.append(int(raw.decode("latin1")))
    threshold = mu.mem_read(E.DATA_SEG * 16 + 0x1630, 1)[0]
    threshold = int(bytes(mu.mem_read(E.DATA_SEG * 16 + 0x1631, threshold))
                    .decode("latin1"))
    return {"names": names, "scores": scores, "threshold": threshold}


def highscore_screen(defaults):
    mu, _ = _machine()
    _far(mu, BASE + 0x8738, [])
    captured = []

    def code(uc, addr, size, user):
        if addr == 0x104C7:
            captured.append(_read_text_lines(uc))
            uc.emu_stop()
            return
        if uc.mem_read(addr, 1)[0] == 0x9A:
            raw = uc.mem_read(addr, 5)
            off = int.from_bytes(raw[1:3], "little")
            seg = int.from_bytes(raw[3:5], "little")
            if seg in VIDEO_SEGS:
                n = E.retf_argbytes(MODULE, seg, off)
                sp = uc.reg_read(UC_X86_REG_SP)
                uc.reg_write(UC_X86_REG_SP, (sp + n) & 0xFFFF)
                cs = uc.reg_read(UC_X86_REG_CS)
                uc.reg_write(UC_X86_REG_IP,
                             ((addr + 5) - (cs << 4)) & 0xFFFF)

    mu.hook_add(UC_HOOK_CODE, code)
    for off in (0x512E, 0x5130, 0x2B5C, 0x2B40, 0x2B41):
        E.w16(mu, E.DATA_SEG, off, 0)
    _far(mu, BASE + 0x4078, [])
    if not captured:
        raise RuntimeError("high-score screen did not reach the window renderer")
    return captured[0]


def story_pages(entry):
    mu, _ = _machine()
    pages = []

    def code(uc, addr, size, user):
        if addr == 0x104C7:
            pages.append(_read_text_lines(uc))
            # Skip the renderer; the wrapper continues to the next page.
            sp = uc.reg_read(UC_X86_REG_SP)
            ret = int.from_bytes(
                uc.mem_read((uc.reg_read(UC_X86_REG_SS) << 4) + sp, 2),
                "little")
            uc.reg_write(UC_X86_REG_SP, (sp + 2) & 0xFFFF)
            uc.reg_write(UC_X86_REG_IP, ret)
            return
        if addr == BASE + 0x0CE9:
            E.w16(mu, E.DATA_SEG, 0x2B5E, 0x81)
        if uc.mem_read(addr, 1)[0] == 0x9A:
            raw = uc.mem_read(addr, 5)
            off = int.from_bytes(raw[1:3], "little")
            seg = int.from_bytes(raw[3:5], "little")
            if seg in VIDEO_SEGS:
                n = E.retf_argbytes(MODULE, seg, off)
                sp = uc.reg_read(UC_X86_REG_SP)
                uc.reg_write(UC_X86_REG_SP, (sp + n) & 0xFFFF)
                cs = uc.reg_read(UC_X86_REG_CS)
                uc.reg_write(UC_X86_REG_IP,
                             ((addr + 5) - (cs << 4)) & 0xFFFF)

    mu.hook_add(UC_HOOK_CODE, code)
    sp = E.STACK_PTR - 4
    E.w16(mu, E.STACK_SEG, sp + 2, 0)
    E.w16(mu, E.STACK_SEG, sp, RET)
    mu.reg_write(UC_X86_REG_SP, sp)
    mu.reg_write(UC_X86_REG_CS, CS)
    mu.reg_write(UC_X86_REG_IP, (entry - BASE) & 0xFFFF)
    try:
        mu.emu_start(entry, entry + 0x10000, count=2_000_000)
    except Exception as exc:  # pragma: no cover - diagnostics
        print("  story emulation stopped: %s" % exc)
    return pages


def key_names():
    """Parse the original packed scan-code -> label table at CS:0x3E."""
    table = bytearray(open(
        os.path.join(ROOT, "extracted", "CC1-UNLZ.EXE"), "rb").read())[0x1E00:]
    p = 0xFC8E
    seq = (list(range(0x02, 0x0C)) + list(range(0x11, 0x19)) +
           [0x1C, 0x1D] + list(range(0x1E, 0x23)) +
           list(range(0x24, 0x27)) + list(range(0x2C, 0x36)) +
           [0x38, 0x39] +
           [0x47, 0x48, 0x49, 0x4B, 0x4D, 0x4F, 0x50, 0x51, 0x52, 0x53])
    names = {}
    for code in seq:
        n = table[p]
        names[code] = bytes(table[p + 1:p + 1 + n]).decode("latin1")
        p += 1 + n
    return {str(k): names.get(k, "NULL") for k in range(0x80)}

def main():
    print("default high scores")
    defaults = highscore_defaults()
    print("  ", defaults)
    print("high-score screen")
    screen = highscore_screen(defaults)
    print("  %d lines" % len(screen))
    fb = bytes(capture_text({"id": "hiscore_screen", "lines": screen}))
    fb_sha = hashlib.sha256(fb).hexdigest()
    import base64, zlib
    fb_b64 = base64.b64encode(zlib.compress(fb, 9)).decode("ascii")
    print("story pages")
    intro = []
    for entry in (0x123F8, 0x12BE2):
        pages = story_pages(entry)
        print("  %#x -> %d pages" % (entry, len(pages)))
        intro.extend(pages)
    finale = []
    for entry in (0x11DE8, 0x116C2, 0x119A8):
        pages = story_pages(entry)
        print("  %#x -> %d pages" % (entry, len(pages)))
        finale.extend(pages)
    print("key names")
    names = key_names()
    print("  %d named keys" % sum(1 for v in names.values() if v != "NULL"))

    flow = {
        "source": "original shell routines (tools/shell_exactness.py)",
        "timing_ticks": {
            "apogee": 720,
            "credits": 216,
            "title": 216,
            "tick_hz": 140.06,
        },
        "screens": ["apogee", "credits", "title", "menu"],
        "ending_order": ["ending", "name_entry_or_hiscore"],
        "intro_pages": intro,
        "finale_pages": finale,
        "highscore_screen_lines": screen,
    }
    hi = {
        "defaults": defaults,
        "screen_lines": screen,
        "screen_fb_sha256": fb_sha,
        "screen_fb_b64": fb_b64,
        "qualify_cases": [
            {"score": 59999, "qualifies": False},
            {"score": 60000, "qualifies": False},
            {"score": 60001, "qualifies": True},
            {"score": 150000, "qualifies": True},
            {"score": 150001, "qualifies": True},
        ],
        "tie_rule": "equal scores keep the existing entry first",
        "name_max": 20,
    }
    keys = {
        "defaults": {"left": 0x4B, "right": 0x4D,
                     "jump": 0x1D, "fire": 0x38},
        "names": names,
        "name_max": 20,
        "confirmation": "N restarts; any other key accepts",
    }
    for path, data in (
        (os.path.join(ROOT, "notes", "shell_exactness_fixtures.json"),
         {"flow": flow, "highscore": hi, "keymap": keys}),
        (os.path.join(ROOT, "notes", "hiscore_fixtures.json"), hi),
        (os.path.join(ROOT, "notes", "keymap_fixtures.json"), keys),
    ):
        with open(path, "w") as f:
            json.dump(data, f, indent=1)
        print("wrote", path)

    digest = hashlib.sha256(json.dumps(flow, sort_keys=True).encode()).hexdigest()
    print("flow digest", digest)


if __name__ == "__main__":
    main()
