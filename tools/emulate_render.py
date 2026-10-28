#!/usr/bin/env python3
"""Full-fidelity level-load render: real blitters + emulated EGA registers.

Runs the original loader (0xB1A9) in Unicorn with

  * the GFX far-pointer table populated exactly like the game's own GFX
    loader (GetMem(table[i], 8003) + block read, i = 1..23; table[0] = 0:0),
  * the REAL blitter code in segment 0x28B5 executing (not stubbed), and
  * ports 3C4/3C5 (sequencer map mask) and 3CE/3CF (graphics bitmask)
    hooked so that every A000: write is fanned out into four plane arrays
    with EGA bitmask semantics  (plane[p][a] = (v & bm) | (old & ~bm)).

The result is the exact framebuffer a real EGA machine would hold after the
loader's SetCell pass, which we can then diff against tools/render_oracle.py
bit for bit.

Only the map area (A000:0 .. A000:0x7D00, 640x400 at 80 bytes/row) is
reconstructed; the +400 copy and the variant-cache strips live beyond that.
"""

import json
import os
import struct
import sys
import zlib

from unicorn import (
    Uc, UC_ARCH_X86, UC_MODE_16,
    UC_HOOK_CODE, UC_HOOK_INTR, UC_HOOK_MEM_FETCH_UNMAPPED,
    UC_HOOK_MEM_WRITE, UC_HOOK_INSN,
)
from unicorn.x86_const import (
    UC_X86_REG_CS, UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS, UC_X86_REG_SP,
    UC_X86_REG_IP, UC_X86_REG_AX, UC_X86_REG_FLAGS,
    UC_X86_INS_OUT, UC_X86_INS_IN,
)
from capstone import Cs, CS_ARCH_X86, CS_MODE_16, CS_OP_IMM

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

import emulate_load as E          # noqa: E402  (reuses its constants/helpers)

EXE = E.EXE
GFX = os.path.join(ROOT, "extracted", "CC1.GFX")

GFX_BUF_SEG = 0x4000              # first segment used for the 23 block buffers
GFX_BLOCK_PARAS = (8003 + 15) // 16   # one paragraph-aligned segment per block
STRIDE_ADDR = (0x28B5 << 4) + 0x60    # cs:[0x60] bytes-per-row
BANK_ADDR = (0x28B5 << 4) + 0xB8      # cs:[0xb8] bank selector

# Calls into SKIP_SEGS segments are stubbed out -- except these three
# blitters, which must run for real (that is the whole point of this
# harness).  Everything else in 0x28B5 is video-mode / palette / sprite
# machinery we do not model (e.g. 0x25D does `int 10h` and is called at
# loader entry, 0xB1B4).
KEEP_REAL = {(0x28B5, 0x039B), (0x28B5, 0x29C5), (0x28B5, 0x2EA1)}

VGA_BASE = 0xA0000
VGA_SIZE = 0x10000

MAP_BYTES = 80 * 400              # visible map: 640 px x 400 rows, 4 planes

# ---- optional debugging (env-gated, zero cost when unset) ----------------
# CC_DEBUG_PORTS=1   log every OUT to 3CE/3CF with the emitting cs:ip
# CC_DEBUG_OFF=N     log every A000 write to byte offset N (decimal)
# CC_DEBUG_BLIT=1    log every opaque-blitter (0x29C5) entry: bitmask + args
DEBUG_PORTS = bool(os.environ.get("CC_DEBUG_PORTS"))
DEBUG_OFF = int(os.environ.get("CC_DEBUG_OFF", "-1"))
DEBUG_BLIT = bool(os.environ.get("CC_DEBUG_BLIT"))


def write_png(path, w, h, rgb):
    def chunk(typ, data):
        c = struct.pack(">I", len(data)) + typ + data
        return c + struct.pack(">I", zlib.crc32(typ + data) & 0xFFFFFFFF)
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        raw += rgb[y * w * 3:(y + 1) * w * 3]
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    open(path, "wb").write(png)


EGA = [(0x00, 0x00, 0x00), (0x00, 0x00, 0xAA), (0x00, 0xAA, 0x00),
       (0x00, 0xAA, 0xAA), (0xAA, 0x00, 0x00), (0xAA, 0x00, 0xAA),
       (0xAA, 0x55, 0x00), (0xAA, 0xAA, 0xAA), (0x55, 0x55, 0x55),
       (0x55, 0x55, 0xFF), (0x55, 0xFF, 0x55), (0x55, 0xFF, 0xFF),
       (0xFF, 0x55, 0x55), (0xFF, 0x55, 0xFF), (0xFF, 0xFF, 0x55),
       (0xFF, 0xFF, 0xFF)]


def planes_to_rgb(planes):
    """4 plane arrays -> 640x400 RGB; colour index = p0 + 2*p1 + 4*p2 + 8*p3."""
    w, h = 640, 400
    rgb = bytearray(w * h * 3)
    stride = 80
    for y in range(h):
        rowbase = y * stride
        for x in range(w):
            byte_i = rowbase + (x >> 3)
            bit = 7 - (x & 7)
            idx = 0
            for p in range(4):
                if planes[p][byte_i] >> bit & 1:
                    idx |= 1 << p
            k = (y * w + x) * 3
            rgb[k:k + 3] = bytes(EGA[idx])
    return rgb


def run_level(level, verbose=True):
    module = E.load_module()
    mu = Uc(UC_ARCH_X86, UC_MODE_16)
    mu.mem_map(0, 0x110000)
    mu.mem_write(0, module)

    # GFX table exactly as the game builds it: GetMem(table[i], 8003) then a
    # block read, for i = 1..23.  table[0] stays 0:0 (the startup init).
    # Each block gets its own paragraph-aligned segment: a far pointer's
    # offset field cannot hold (i-1)*8003 once i >= 9, so a single contiguous
    # run would corrupt the high-numbered table entries.
    gfx = open(GFX, "rb").read()
    assert len(gfx) == 23 * 8003, len(gfx)
    for i in range(1, 24):
        seg = GFX_BUF_SEG + (i - 1) * GFX_BLOCK_PARAS
        mu.mem_write(seg << 4, gfx[(i - 1) * 8003: i * 8003])
        E.w16(mu, E.DATA_SEG, 0x5234 + i * 4, 0)
        E.w16(mu, E.DATA_SEG, 0x5236 + i * 4, seg)
    E.w16(mu, E.DATA_SEG, 0x5234, 0)      # table[0] = 0:0 (fresh boot)
    E.w16(mu, E.DATA_SEG, 0x5236, 0)

    # The caller of the loader sets the map geometry before calling
    # (SetBytesPerRow(0x50) at CS:0xF05C); seed the blitter globals.
    mu.mem_write(STRIDE_ADDR, struct.pack("<H", 0x50))
    mu.mem_write(BANK_ADDR, struct.pack("<H", 0))

    for site in E.DRAW_SITES:
        mu.mem_write(site, b"\x83\xc4\x08\x90\x90")
    mu.mem_write(E.RET_SENTINEL, b"\xf4")

    mu.reg_write(UC_X86_REG_SS, E.STACK_SEG)
    mu.reg_write(UC_X86_REG_DS, E.DATA_SEG)
    mu.reg_write(UC_X86_REG_ES, E.DATA_SEG)
    mu.reg_write(UC_X86_REG_CS, 0)

    arg = 0xFFFF if level == 0 else (0xFFFE if level == 1 else level - 2)
    sp = E.STACK_PTR
    sp -= 2
    E.w16(mu, E.STACK_SEG, sp, arg & 0xFFFF)
    sp -= 2
    E.w16(mu, E.STACK_SEG, sp, E.RET_SENTINEL)
    mu.reg_write(UC_X86_REG_SP, sp)
    for addr, val in E.BOOT_STATE.items():
        E.w16(mu, E.DATA_SEG, addr, val)
    E.w16(mu, E.DATA_SEG, 0xA0BE, arg & 0xFFFF)
    # Sequential-play model: level 2's prologue sets [0x2d6] = 20 (the block
    # used for the second layer of two-wide objects) and nothing changes it
    # afterwards.  On a fresh boot L0/L1 would instead dereference
    # table[0] = 0:0 -- undefined behaviour on real hardware -- and
    # render_oracle.py substitutes block 20 for that case, so mirror the same
    # choice here to get a bit-exact comparison target.
    E.w16(mu, E.DATA_SEG, 0x2D6, 20)

    # ---- EGA register state -------------------------------------------
    state = {"seq_idx": 0, "map_mask": 0, "gfx_idx": 0, "bitmask": 0xFF}
    planes = [bytearray(VGA_SIZE) for _ in range(4)]

    def apply_out(port, v):
        if port == 0x3C4:
            state["seq_idx"] = v & 0xFF
        elif port == 0x3C5 and state["seq_idx"] == 2:
            state["map_mask"] = v & 0xFF
        elif port == 0x3CE:
            state["gfx_idx"] = v & 0xFF
        elif port == 0x3CF and state["gfx_idx"] == 8:
            state["bitmask"] = v & 0xFF

    def hook_out(uc, port, size, value, user):
        # Unicorn reports a 16-bit `out dx,ax` as ONE event at port=DX with
        # value=AX; the AH half goes to port+1 and must be applied here too
        # (the blitters set the EGA bitmask with `out dx,ax` to 3CE/3CF).
        if DEBUG_PORTS and (port == 0x3CE or port == 0x3CF
                            or (size == 2 and port == 0x3CE)):
            cs = uc.reg_read(UC_X86_REG_CS)
            ip = (uc.reg_read(UC_X86_REG_IP) - 2) & 0xFFFF
            key = ("out %04X sz=%d val=%02X @%04X:%04X"
                   % (port, size, value & 0xFFFF, cs, ip))
            plog[key] = plog.get(key, 0) + 1
        if size == 2:
            apply_out(port, value & 0xFF)
            apply_out(port + 1, (value >> 8) & 0xFF)
        else:
            apply_out(port, value & 0xFF)
        # (the blitters never read ports; other OUTs are stubbed-segment code)

    def hook_in(uc, port, size, user):
        return 0

    def hook_write(uc, access, address, size, value, user):
        if not (VGA_BASE <= address < VGA_BASE + VGA_SIZE):
            return
        mm = state["map_mask"]
        bm = state["bitmask"]
        off = address - VGA_BASE
        if off == DEBUG_OFF:
            cs = uc.reg_read(UC_X86_REG_CS)
            ip = uc.reg_read(UC_X86_REG_IP)
            print("  A000[%04X] <- %02X mm=%02X bm=%02X @%04X:%04X"
                  % (off, value & 0xFF, mm, bm, cs, ip))
        if mm == 0:
            return
        v = value & 0xFF
        for p in range(4):
            if mm >> p & 1:
                pl = planes[p]
                pl[off] = (v & bm) | (pl[off] & ~bm & 0xFF)

    md = Cs(CS_ARCH_X86, CS_MODE_16)
    md.detail = True
    stats = {"steps": 0, "stopped": None}
    plog = {}

    def read_stack_word(off):
        return E.u16(mu, E.STACK_SEG, (mu.reg_read(UC_X86_REG_SP) + off) & 0xFFFF)

    tail = []

    def hook_code(uc, address, size, user):
        stats["steps"] += 1
        if DEBUG_BLIT and address == (0x28B5 << 4) + 0x29C5:
            y = E.u16(mu, E.STACK_SEG, (uc.reg_read(UC_X86_REG_SP) + 6) & 0xFFFF)
            x = E.u16(mu, E.STACK_SEG, (uc.reg_read(UC_X86_REG_SP) + 8) & 0xFFFF)
            print("  opaque blit y=%3d x=%3d bm=%02X mm=%02X"
                  % (y, x, state["bitmask"], state["map_mask"]))
        if len(tail) < 64:
            tail.append(address)
        else:
            tail.pop(0)
            tail.append(address)
        if address in (E.LOAD_END, E.RET_SENTINEL):
            stats["stopped"] = "load_end" if address == E.LOAD_END else "ret"
            uc.emu_stop()
            return
        ins = next(md.disasm(bytes(uc.mem_read(address, 16)), address & 0xFFFF), None)
        if ins is None:
            return
        if ins.mnemonic == "lcall" and len(ins.operands) >= 2 \
                and ins.operands[0].type == CS_OP_IMM and ins.operands[1].type == CS_OP_IMM:
            seg, off = ins.operands[0].imm, ins.operands[1].imm
            if seg in E.SKIP_SEGS and (seg, off) not in KEEP_REAL:
                key = "%04X:%04X" % (seg, off)
                skipped = stats.setdefault("skipped", {})
                skipped[key] = skipped.get(key, 0) + 1
                n = E.retf_argbytes(module, seg, off)
                uc.reg_write(UC_X86_REG_SP, (uc.reg_read(UC_X86_REG_SP) + n) & 0xFFFF)
                uc.reg_write(UC_X86_REG_IP, (address + ins.size) & 0xFFFF)

    def hook_intr(uc, intno, user):
        cs = uc.reg_read(UC_X86_REG_CS)
        ip = uc.reg_read(UC_X86_REG_IP)
        ax = uc.reg_read(UC_X86_REG_AX)
        if intno == 0x10 and (ax >> 8) == 0x0F:
            uc.reg_write(UC_X86_REG_AX, (ax & 0xFF00) | 0x13)
        elif intno == 0x16 and (ax >> 8) in (0x01, 0x11):
            uc.reg_write(UC_X86_REG_FLAGS, uc.reg_read(UC_X86_REG_FLAGS) | 0x40)
        uc.reg_write(UC_X86_REG_IP, (ip + 2) & 0xFFFF)

    mu.hook_add(UC_HOOK_CODE, hook_code)
    mu.hook_add(UC_HOOK_INTR, hook_intr)
    mu.hook_add(UC_HOOK_MEM_WRITE, hook_write)
    mu.hook_add(UC_HOOK_INSN, hook_out, aux1=UC_X86_INS_OUT)
    mu.hook_add(UC_HOOK_INSN, hook_in, aux1=UC_X86_INS_IN)
    mu.hook_add(UC_HOOK_MEM_FETCH_UNMAPPED,
                lambda uc, a, addr, s, v, u: (stats.update(stopped="bad_fetch %x" % addr),
                                              uc.emu_stop()))

    try:
        mu.emu_start(E.LOADER, 0x110000, count=50_000_000)
    except Exception as exc:
        stats["stopped"] = "error: %r" % exc
        stats["tail"] = ["%05x" % a for a in tail[-24:]]
        stats["regs"] = "cs=%04x ip=%04x" % (mu.reg_read(UC_X86_REG_CS),
                                             mu.reg_read(UC_X86_REG_IP))

    if verbose:
        print("level %d: stop=%s steps=%d map_mask_final=%d bitmask_final=%d"
              % (level, stats["stopped"], stats["steps"],
                 state["map_mask"], state["bitmask"]))
        if "tail" in stats:
            print("  tail:", " ".join(stats["tail"]))
            print("  regs:", stats.get("regs"))
        if stats.get("skipped"):
            print("  skipped:", dict(sorted(stats["skipped"].items())))
        if DEBUG_PORTS and plog:
            print("  3CE/3CF OUT sites:")
            for key, n in sorted(plog.items()):
                print("    %-38s x%d" % (key, n))
    return planes, stats


def main():
    levels = [int(a) for a in sys.argv[1:] or ["0"]]
    for level in levels:
        planes, stats = run_level(level)
        rgb = planes_to_rgb([bytes(p[:MAP_BYTES]) for p in planes])
        out = os.path.join(ROOT, "out", "emu_L%02d.png" % level)
        write_png(out, 640, 400, rgb)
        print("wrote", out)
        if stats["stopped"] not in ("load_end", "ret"):
            print("  WARNING: unexpected stop:", stats["stopped"])


if __name__ == "__main__":
    main()
