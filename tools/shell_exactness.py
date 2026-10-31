#!/usr/bin/env python3
"""Capture the shell's observable exactness data from the original binary.

This is the P5.R6/R1 harness: it runs the individual shell routines that
matter (high-score defaults/display, story page wrappers, key-name lookup,
default key codes) with a minimal DOS file shim and scripted keyboard input.
`--flow` runs the real title loop (0x0FC5:0x8265) with the image/CRTC helpers
stubbed, injects the 140.06 Hz counter into the hold spins and records the
hold length, row order, direction flips, key-to-menu paths and sound log.

Outputs:
  notes/shell_exactness_fixtures.json
  notes/hiscore_fixtures.json
  notes/keymap_fixtures.json
  notes/shell_flow_fixtures.json   (--flow)
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
    UC_X86_REG_DS, UC_X86_REG_IP, UC_X86_REG_FLAGS, UC_X86_REG_BP,
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


# ---- R7: menu information screens (ordering/foreign/BBS/video/quit) --------
#
# Each main-menu information option copies its text into the DS:0x163C line
# buffer and then renders it.  The static text tables do not contain the blank
# spacer rows (the handlers insert them with `mov byte [di+0x163C],0`), so the
# only exact source is the real handler's buffer.  These entry points and the
# render call sites are the ones the main menu dispatches to (0x0FC5:0x7914).

MENU_ENTRY = 0x17564
MENU_INPUT = 0x17729          # menu key read (after the text is built)

MENU_PAGE_SITES = {
    # ordering: episodes -> offer -> disk
    0x13AEE: "ordering_episodes",
    0x1377A: "ordering_offer",
    0x13415: "ordering_disk",
    # foreign: intro -> Australia -> Germany -> UK
    0x19329: "foreign",
    0x19526: "foreign_au",
    0x19714: "foreign_de",
    0x19902: "foreign_uk",
    # single-page screens
    0x19C81: "bbs",
    0x1A018: "about",
    0x17BDE: "menu_help",
    0x15EFE: "menu_quit",
}
MENU_PAGE_ENTRY = {
    "ordering_episodes": 0x1390A, "ordering_offer": 0x1390A,
    "ordering_disk": 0x1390A,
    "foreign": 0x19130, "foreign_au": 0x19130, "foreign_de": 0x19130,
    "foreign_uk": 0x19130,
    "bbs": 0x19A88, "about": 0x19E1F,
    "menu_help": 0x17978, "menu_quit": 0x15E01,
}
VIDEO_SETUP = 0x1A688          # text + Y/N prompt; sets [0xA0EC]
QUIT_CONFIRM = 0x114A1         # "Quit to DOS (Y-N)"; sets [0x3396]


def _capture_menu_pages():
    """Run each information handler and snapshot its line buffer per page."""
    pages = {}

    def run(entry, sites):
        mu, _ = _machine()
        KEYS.clear()
        KEYS.extend([0x1C] * 80)   # Enter advances every "Press Any Key"
        seen = set()

        def code(uc, addr, size, user):
            ip = uc.reg_read(UC_X86_REG_IP)
            load = (uc.reg_read(UC_X86_REG_CS) << 4) + ip
            name = sites.get(load)
            if name is None:
                return
            if name not in pages:
                pages[name] = _read_text_lines(mu)
            seen.add(name)
            if len(seen) >= len(sites):
                uc.emu_stop()
                return
            # Skip the render/wait call so the handler continues to the next
            # page instead of blocking on a key.
            uc.reg_write(UC_X86_REG_IP, (ip + 3) & 0xFFFF)

        mu.hook_add(UC_HOOK_CODE, code)
        _far(mu, entry, [])

    # Ordering pages are reached through one handler chain.
    run(MENU_PAGE_ENTRY["ordering_episodes"],
        {s: n for s, n in MENU_PAGE_SITES.items()
         if n.startswith("ordering")})
    # Foreign pages are inline in one handler.
    run(MENU_PAGE_ENTRY["foreign"],
        {s: n for s, n in MENU_PAGE_SITES.items()
         if n.startswith("foreign")})
    for name in ("bbs", "about", "menu_help", "menu_quit"):
        run(MENU_PAGE_ENTRY[name],
            {s: n for s, n in MENU_PAGE_SITES.items() if n == name})

    # Main menu text (built before the first key read).
    mu, _ = _machine()
    KEYS.clear()
    KEYS.extend([0x01, 0x01])
    grabbed = [False]

    def menu_code(uc, addr, size, user):
        ip = uc.reg_read(UC_X86_REG_IP)
        load = (uc.reg_read(UC_X86_REG_CS) << 4) + ip
        if load == MENU_INPUT and not grabbed[0]:
            grabbed[0] = True
            pages["menu_main"] = _read_text_lines(mu)
            uc.emu_stop()

    mu.hook_add(UC_HOOK_CODE, menu_code)
    _far(mu, MENU_ENTRY, [])

    # Video compatibility screen (Y/N prompt) and the quit-to-DOS confirm.
    # The page is complete just before the input call; snapshot there instead
    # of running the modal wait loop.
    for name, entry, site in (("menu_video", VIDEO_SETUP, 0x1A7C0),
                              ("menu_quit_dos", QUIT_CONFIRM, 0x114E4)):
        mu, _ = _machine()
        KEYS.clear()
        grabbed = [False]

        def stop_code(uc, addr, size, user, site=site, name=name,
                      grabbed=grabbed, mu=mu):
            ip = uc.reg_read(UC_X86_REG_IP)
            load = (uc.reg_read(UC_X86_REG_CS) << 4) + ip
            if load == site and not grabbed[0]:
                grabbed[0] = True
                pages[name] = _read_text_lines(mu)
                uc.emu_stop()

        mu.hook_add(UC_HOOK_CODE, stop_code)
        _far(mu, entry, [])

    return pages


def menu_fixture():
    pages = _capture_menu_pages()

    return {
        "source": "original menu information handlers (0x0FC5 segment); "
                  "line buffers captured after each page is built",
        "entry_points": {
            "menu": MENU_ENTRY,
            "ordering": 0x1390A,
            "foreign": 0x19130,
            "bbs": 0x19A88,
            "about": 0x19E1F,
            "help": 0x17978,
            "quit_menu": 0x15E01,
            "video": VIDEO_SETUP,
            "quit": QUIT_CONFIRM,
        },
        "pages": pages,
        "video_prompt": {
            "text": "Alternate video setting (Y-N)",
            "keys": {"yes": 0x15, "no": 0x31, "escape": 0x01},
            "effect": "Y sets [0xA0EC]=1, N/Esc set [0xA0EC]=0",
        },
        "quit_prompt": {
            "text": "Quit to DOS (Y-N)",
            "keys": {"yes": 0x15, "no": 0x31, "escape": 0x01},
            "effect": "Y sets [0x3396]=1, N/Esc set [0x3396]=0",
        },
    }


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


# ---- R1: title/credits scroll flow ----------------------------------------
#
# The title routine (0x0FC5:0x8265, linear 0x17EB5) loads cc1.cdt into rows
# 0..199 of the 320x400 VRAM buffer, copies it to rows 200..399, then loads
# cc1.ttl over rows 0..199 and bounces the CRTC start row 0..200..0.  The
# image loads and the CRTC/EGA helpers are stubbed here (their behaviour is
# covered by export_shell_assets.py and render.c); what this harness verifies
# is the real control flow: hold lengths, row order, direction flips and the
# key path into the menu.

TITLE_ENTRY = 0x8265        # 0x0FC5:0x8265, linear 0x17EB5
TITLE_HOLD = 0x17F74        # initial hold spin
TITLE_DRAW = 0x18024        # direction test + 0x28B5:0x11F call
TITLE_FLIP_HOLD = 0x18074   # hold spin after a direction flip
TITLE_MENU = 0x17564        # main menu entry (near call at 0x17F8A)
HOLD_LIMIT = 0x1B0          # 432; loop exits when [0x2B60] > 432
SCROLL_ROWS = 0xC8          # 200
TITLE_KEY = 0x1C            # Enter (a make code in the {1..128} set)


def _skip_far(uc, addr, seg, off):
    n = E.retf_argbytes(MODULE, seg, off)
    sp = uc.reg_read(UC_X86_REG_SP)
    uc.reg_write(UC_X86_REG_SP, (sp + n) & 0xFFFF)
    cs = uc.reg_read(UC_X86_REG_CS)
    uc.reg_write(UC_X86_REG_IP, ((addr + 5) - (cs << 4)) & 0xFFFF)


def _flow_machine():
    ds0, _ = S.run_startup()
    mu = make_machine(ds0)
    mu.hook_add(UC_HOOK_INTR, _intr)
    mu.hook_add(UC_HOOK_INSN, _in_hook, None, 1, 0, UC_X86_INS_IN)
    state = {
        "draws": [], "sound": [], "display": [], "menu": False,
        "returned": False, "hold_exits": [], "key": None, "hold_iters": 0,
    }

    def code(uc, addr, size, user):
        if addr == RET:
            state["returned"] = True
            uc.emu_stop()
            return
        if addr == TITLE_MENU:
            state["menu"] = True
            uc.emu_stop()
            return
        raw = uc.mem_read(addr, 1)[0]
        if raw == 0x9A:
            b = uc.mem_read(addr, 5)
            off = int.from_bytes(b[1:3], "little")
            seg = int.from_bytes(b[3:5], "little")
            if seg == 0x1F9A and off == 0x21F:
                sp = uc.reg_read(UC_X86_REG_SP)
                state["sound"].append(E.u16(mu, E.STACK_SEG, sp))
                _skip_far(uc, addr, seg, off)
                return
            if seg in (0x28B5, 0x26C5, 0x1D6E):
                sp = uc.reg_read(UC_X86_REG_SP)
                args = [E.u16(mu, E.STACK_SEG, (sp + 2 * i) & 0xFFFF)
                        for i in range(4)]
                if seg == 0x28B5:
                    state["display"].append({"off": off, "args": args[:2]})
                _skip_far(uc, addr, seg, off)
                return
        if addr in (TITLE_HOLD, TITLE_FLIP_HOLD):
            tick = E.u16(mu, E.DATA_SEG, 0x2B60)
            state["hold_iters"] += 1
            if state["key"] == ["hold", state["hold_iters"]]:
                E.w16(mu, E.DATA_SEG, 0x2B5E, TITLE_KEY)
            E.w16(mu, E.DATA_SEG, 0x2B60, (tick + 1) & 0xFFFF)
            return
        if addr == TITLE_DRAW:
            bp = uc.reg_read(UC_X86_REG_BP)
            direction = E.u16(mu, E.STACK_SEG, (bp - 2) & 0xFFFF)
            offset = E.u16(mu, E.STACK_SEG, (bp - 4) & 0xFFFF)
            tick = E.u16(mu, E.DATA_SEG, 0x2B60)
            if direction >= 0x8000:
                direction -= 0x10000
            row = offset if direction == 1 else (SCROLL_ROWS - offset)
            if not state["draws"] or state["draws"][-1]["dir"] != direction:
                state["hold_exits"].append(tick)
            state["draws"].append({"dir": direction, "offset": offset,
                                   "row": row, "counter": tick})
            if state["key"] == ["draw", len(state["draws"]) - 1]:
                E.w16(mu, E.DATA_SEG, 0x2B5E, TITLE_KEY)
            # One row per vertical retrace: ~2 PIT ticks at 140.06 Hz.
            E.w16(mu, E.DATA_SEG, 0x2B60, (tick + 2) & 0xFFFF)
            if len(state["draws"]) >= state.get("max_draws", 460):
                uc.emu_stop()
            return

    mu.hook_add(UC_HOOK_CODE, code)
    return mu, state


def title_flow(key=None, max_draws=460):
    """Run the real title loop; `key` is ("hold", n) or ("draw", n)."""
    mu, state = _flow_machine()
    state["key"] = list(key) if key else None
    state["max_draws"] = max_draws
    sp = E.STACK_PTR
    sp -= 2
    E.w16(mu, E.STACK_SEG, sp, 0)
    sp -= 2
    E.w16(mu, E.STACK_SEG, sp, RET)
    mu.reg_write(UC_X86_REG_SP, sp)
    mu.reg_write(UC_X86_REG_CS, CS)
    mu.reg_write(UC_X86_REG_IP, TITLE_ENTRY)
    try:
        mu.emu_start(BASE + TITLE_ENTRY, BASE + TITLE_ENTRY + 0x10000,
                     count=6_000_000)
    except Exception as exc:  # pragma: no cover - diagnostics
        print("  title emulation stopped: %s" % exc)
    state["draws"] = state["draws"][:max_draws]
    return state


def _screen_rows():
    """Decode rows of the exported screens.bin for the composite fixture."""
    import struct
    src = os.path.join(ROOT, "out", "portable", "screens.bin")
    data = open(src, "rb").read()
    size = 320 * 200
    title = data[1 * size:2 * size]
    credits = data[2 * size:3 * size]
    stack = title + credits

    def row_sha(row):
        return hashlib.sha256(stack[row * 320:(row + 1) * 320]).hexdigest()

    return {
        "source": "out/portable/screens.bin (CC1.TTL rows 0..199, "
                  "CC1.CDT rows 200..399)",
        "title_sha256": hashlib.sha256(title).hexdigest(),
        "credits_sha256": hashlib.sha256(credits).hexdigest(),
        "row_sha256": [row_sha(r) for r in range(SCROLL_ROWS + 1)],
    }


def title_flow_fixture():
    state = title_flow()
    draws = state["draws"]
    if not draws:
        raise RuntimeError("title flow produced no draws")
    forward = [d for d in draws if d["dir"] == 1]
    backward = [d for d in draws if d["dir"] == -1]
    hold = draws[0]["counter"]
    # Fold the observed draws into one clean cycle: forward 0..200 then
    # backward 199..0 (the first forward squeeze repeats row 0).
    fwd_rows = [d["row"] for d in forward[:SCROLL_ROWS + 1]]
    bwd_rows = [d["row"] for d in backward[:SCROLL_ROWS + 1]]

    def key_case(key):
        st = title_flow(key=key)
        return {
            "key": key,
            "menu": bool(st["menu"]),
            "draws": len(st["draws"]),
            "last_row": st["draws"][-1]["row"] if st["draws"] else None,
        }

    return {
        "source": "original title routine 0x0FC5:0x8265 "
                  "(linear 0x17EB5) with IMG/CRTC helpers stubbed",
        "timing": {
            "logo_hold": 0x2D0,
            "hold_ticks": hold,
            "hold_limit": HOLD_LIMIT,
            "scroll_rows": SCROLL_ROWS,
            "ticks_per_row": 2,
            "pit_hz": 140.061,
            "vblank_hz": 70.086,
            "stride": 0x28,
        },
        "forward_rows": fwd_rows,
        "backward_rows": bwd_rows,
        "hold_exits": state["hold_exits"][:2],
        "sound_events": state["sound"],
        "display_calls": len(state["display"]),
        "composite": _screen_rows(),
        "key_cases": [key_case(("hold", 50)), key_case(("draw", 40)),
                      key_case(("draw", 250))],
        "notes": [
            "forward rows repeat row 0 because the loop draws at off=0 "
            "before incrementing; the original holds the same frame first.",
            "one scroll row per vertical retrace; ~2 PIT ticks at 140.06 Hz.",
            "the title routine issues no play_sound calls.",
        ],
    }


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
    if "--menus" in sys.argv:
        print("menu information screens")
        fixture = menu_fixture()
        for name, lines in fixture["pages"].items():
            print("  %-20s %d lines" % (name, len(lines)))
        print("  video prompt", fixture["video_prompt"])
        print("  quit prompt", fixture["quit_prompt"])
        out = os.path.join(ROOT, "notes", "menu_fixtures.json")
        with open(out, "w") as f:
            json.dump(fixture, f, indent=1)
        print("wrote", out)
        return
    if "--flow" in sys.argv:
        print("title/credits flow")
        flow = title_flow_fixture()
        out = os.path.join(ROOT, "notes", "shell_flow_fixtures.json")
        with open(out, "w") as f:
            json.dump(flow, f, indent=1)
        print("  hold", flow["timing"]["hold_ticks"],
              "forward", len(flow["forward_rows"]),
              "backward", len(flow["backward_rows"]),
              "sound", flow["sound_events"])
        print("  key cases", flow["key_cases"])
        print("wrote", out)
        print("flow digest", hashlib.sha256(
            json.dumps(flow, sort_keys=True).encode()).hexdigest())
        return
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
            "attract_hold": 433,
            "scroll_rows": 200,
            "ticks_per_row": 2,
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
