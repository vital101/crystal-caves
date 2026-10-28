#!/usr/bin/env python3
"""Oracle frame harness (reusable).

Loads a level once into Unicorn, then runs the original player-physics /
camera / collision routines with controlled state.  This is the validation
foundation for Phase 3.

Example:
    import oracle_frame as OF
    o = OF.FrameOracle(0)          # load level 0 once (~20 s)
    print(o.coll(80, 80, 0, 0))    # collision helper -> 0/1
    o.vert(5)                      # run vertical movement 5 frames
    print(o.state(0x4613))         # player y after

Addresses are DS offsets under CS=0, DS=ES=0x2CD3.
"""
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import emulate_load as E  # noqa: E402

from unicorn import UC_HOOK_CODE
from unicorn.x86_const import (
    UC_X86_REG_AX, UC_X86_REG_BX, UC_X86_REG_CX, UC_X86_REG_DX,
    UC_X86_REG_DI, UC_X86_REG_SI, UC_X86_REG_BP, UC_X86_REG_SP,
    UC_X86_REG_CS, UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS,
    UC_X86_REG_IP, UC_X86_REG_FLAGS,
)

DATA_SEG = E.DATA_SEG
STACK_SEG = E.STACK_SEG
STACK_PTR = E.STACK_PTR
CELL_BASE = E.CELL_BASE
OBJ_BASE = E.OBJ_BASE
OBJ_STRIDE = E.OBJ_STRIDE
OBJ_SLOTS = E.OBJ_SLOTS
MAP_ROWS = E.MAP_ROWS
MAP_COLS = E.MAP_COLS
CELL_COL_STRIDE = E.CELL_COL_STRIDE
CELL_ROW_STRIDE = E.CELL_ROW_STRIDE

SKIP_SEGS = {0x28B5, 0x26C5, 0x0FC5, 0x1F9A}
COLL_HELPER_CS = 0x1F9A
COLL_HELPER_IP = 0x43  # load 0x1F9E3

# Globals the physics touches (DS offsets).
GLOBALS = [
    0x4611, 0x4613, 0x512E, 0x5130, 0x5132, 0x5134, 0x513D, 0x5142,
    0xA0FD, 0x2C8, 0x2CA, 0x2E0, 0x306, 0x30A, 0x4629, 0x461B,
    0x509C, 0x5118, 0x5128, 0x2B62, 0x2B64, 0xA0F1, 0xA0F2,
]


def u16(mu, seg, off):
    return int.from_bytes(mu.mem_read((seg << 4) + off, 2), "little")


def s16(mu, seg, off):
    return int.from_bytes(mu.mem_read((seg << 4) + off, 2), "little", signed=True)


def w16(mu, seg, off, v):
    mu.mem_write((seg << 4) + off, (v & 0xFFFF).to_bytes(2, "little"))


class FrameOracle:
    def __init__(self, level):
        self.level = level
        r = E.run_level(level, boot_seed=True)
        self.dump = r
        self.objs = r["objects"]
        self.cells = r["cells"]
        self.cursor = r["cursor"]

        module = E.load_module()
        self.mu = Uc = E.Uc if False else None
        from unicorn import Uc as _Uc
        self.mu = _Uc(E.UC_ARCH_X86, E.UC_MODE_16)
        mu = self.mu
        mu.mem_map(0, 0x110000)
        mu.mem_write(0, module)
        mu.mem_write(E.RET_SENTINEL, b"\xf4")
        for site in E.DRAW_SITES:
            mu.mem_write(site, b"\x83\xc4\x08\x90\x90")
        mu.reg_write(UC_X86_REG_SS, STACK_SEG)
        mu.reg_write(UC_X86_REG_DS, DATA_SEG)
        mu.reg_write(UC_X86_REG_ES, DATA_SEG)
        mu.reg_write(UC_X86_REG_CS, 0)
        mu.reg_write(UC_X86_REG_SP, STACK_PTR - 2)
        w16(mu, STACK_SEG, STACK_PTR - 2, 0xFFF0)

        w16(mu, DATA_SEG, 0x2B72, 0xFFFF)
        w16(mu, DATA_SEG, 0x2B74, 0x0000)
        arg = 0xFFFF if level == 0 else (0xFFFE if level == 1 else level - 2)
        w16(mu, DATA_SEG, 0xA0BE, arg & 0xFFFF)

        from emulate_load import OBJ_FIELDS as _F
        for o in self.objs:
            base = OBJ_BASE + o["index"] * OBJ_STRIDE
            for name, off, kind in _F:
                if kind == "byte":
                    mu.mem_write((DATA_SEG << 4) + base + off,
                                 bytes([o[name] & 0xFF]))
                elif kind is True:
                    w16(mu, DATA_SEG, base + off, o[name])
                else:
                    w16(mu, DATA_SEG, base + off, o[name] & 0xFFFF)
        w16(mu, DATA_SEG, 0x5120, self.cursor)

        for row in range(MAP_ROWS):
            for col in range(MAP_COLS):
                off = CELL_BASE + (col + 1) * CELL_COL_STRIDE + (row + 1) * CELL_ROW_STRIDE
                mu.mem_write((DATA_SEG << 4) + off, bytes.fromhex(self.cells[row][col]))

        # Hooks.
        self.steps = 0
        self.stopped = None
        self.coll_result = None
        self._install_hooks()

        self.reset_player()

    def _install_hooks(self):
        mu = self.mu

        def hook_code(uc, address, size, user):
            self.steps += 1
            cs = uc.reg_read(UC_X86_REG_CS)
            if address == E.RET_SENTINEL:
                if self.coll_result is None:
                    self.coll_result = uc.reg_read(UC_X86_REG_AX) & 0xFF
                    self.stopped = "coll_done"
                    uc.emu_stop()
                    return
                self.stopped = "sentinel"
                uc.emu_stop()
                return
            ins = next(E.md.disasm(bytes(uc.mem_read(address, 16)), address & 0xFFFF), None)
            if ins is None:
                return
            if ins.mnemonic == "lcall" and len(ins.operands) >= 2 \
                    and ins.operands[0].type == E.CS_OP_IMM \
                    and ins.operands[1].type == E.CS_OP_IMM:
                seg = ins.operands[0].imm
                off = ins.operands[1].imm
                if seg in SKIP_SEGS:
                    n = E.retf_argbytes(E.load_module(), seg, off)
                    sp = uc.reg_read(UC_X86_REG_SP)
                    uc.reg_write(UC_X86_REG_SP, (sp + n) & 0xFFFF)
                    uc.reg_write(UC_X86_REG_IP, (address + ins.size) & 0xFFFF)
                    return

        mu.hook_add(UC_HOOK_CODE, hook_code)

    def reset_player(self, x=None, y=None):
        """Set player + camera + physics globals to defaults."""
        mu = self.mu
        o1 = next((o for o in self.objs if o["index"] == 1), None)
        x = x if x is not None else (o1["x"] if o1 else 80)
        y = y if y is not None else (o1["y"] if o1 else 80)
        w16(mu, DATA_SEG, 0x4611, x & 0xFFFF)
        w16(mu, DATA_SEG, 0x4613, y & 0xFFFF)
        w16(mu, DATA_SEG, 0x512E, 0)
        w16(mu, DATA_SEG, 0x5130, 0)
        w16(mu, DATA_SEG, 0x5132, 1)
        w16(mu, DATA_SEG, 0x5134, 0)
        w16(mu, DATA_SEG, 0x513D, 0)
        w16(mu, DATA_SEG, 0x5142, 0)
        w16(mu, DATA_SEG, 0xA0FD, 0)
        w16(mu, DATA_SEG, 0x2C8, 0)
        w16(mu, DATA_SEG, 0x2CA, 0)
        w16(mu, DATA_SEG, 0x2E0, 0)
        w16(mu, DATA_SEG, 0x306, 1)
        w16(mu, DATA_SEG, 0x30A, 0)
        w16(mu, DATA_SEG, 0x4629, 0)
        w16(mu, DATA_SEG, 0x461B, 1)
        w16(mu, DATA_SEG, 0x509C, 0)
        w16(mu, DATA_SEG, 0x5118, 0)
        w16(mu, DATA_SEG, 0x5128, 0)
        w16(mu, DATA_SEG, 0x2B62, 0)
        w16(mu, DATA_SEG, 0x2B64, 0)
        w16(mu, DATA_SEG, 0xA0F1, 0)
        w16(mu, DATA_SEG, 0xA0F2, 0)

    def set_global(self, off, val):
        w16(self.mu, DATA_SEG, off, val)

    def state(self, off):
        return s16(self.mu, DATA_SEG, off)

    def _run_near(self, entry, frames):
        mu = self.mu
        for _ in range(frames):
            mu.reg_write(UC_X86_REG_CS, 0)
            mu.reg_write(UC_X86_REG_SP, STACK_PTR - 2)
            self.coll_result = None
            self.stopped = None
            try:
                mu.emu_start(entry, 0x110000, count=500_000)
            except Exception as ex:
                self.stopped = "error: %s" % ex
                break
            if self.stopped:
                break

    def coll(self, x, y, xoff=0, yoff=0):
        """Run collision helper 0x1F9A:0x43 at player (x,y) with corner offsets.
        Returns AL: nonzero=collision, 0=clear.  Sets player x/y first."""
        self.reset_player(x, y)
        mu = self.mu
        sp = STACK_PTR
        w16(mu, STACK_SEG, sp, E.RET_SENTINEL)      # return IP
        w16(mu, STACK_SEG, sp + 2, 0)                # return CS
        w16(mu, STACK_SEG, sp + 4, yoff & 0xFFFF)    # bp+6 (y offset)
        w16(mu, STACK_SEG, sp + 6, xoff & 0xFFFF)    # bp+8 (x offset)
        mu.reg_write(UC_X86_REG_SP, sp)
        mu.reg_write(UC_X86_REG_CS, COLL_HELPER_CS)
        mu.reg_write(UC_X86_REG_IP, COLL_HELPER_IP)
        self.coll_result = None
        self.stopped = None
        try:
            mu.emu_start(COLL_HELPER_CS << 4 | COLL_HELPER_IP, 0x110000,
                         count=50_000)
        except Exception as ex:
            self.stopped = "error: %s" % ex
        return self.coll_result

    def vert(self, frames=1):
        self._run_near(0xDCB9, frames)

    def horiz(self, frames=1):
        self._run_near(0xE011, frames)

    def camy(self, frames=1):
        self._run_near(0xE2FD, frames)

    def state_dict(self):
        return {g: self.state(g) for g in GLOBALS}


def main():
    if len(sys.argv) < 3:
        print("usage: oracle_frame.py <level> <routine> [frames] [x] [y]")
        sys.exit(2)
    level = int(sys.argv[1])
    routine = sys.argv[2]
    frames = int(sys.argv[3]) if len(sys.argv) > 3 else 1
    x = int(sys.argv[4]) if len(sys.argv) > 4 else None
    y = int(sys.argv[5]) if len(sys.argv) > 5 else None
    o = FrameOracle(level)
    if routine == "coll":
        x = x if x is not None else 80
        y = y if y is not None else 80
        print("coll_result=%d" % o.coll(x, y))
    elif routine == "vert":
        o.vert(frames)
        print("player=(%d,%d) cam=(%d,%d) 2ca=%d 5134=%d 306=%d" % (
            o.state(0x4611), o.state(0x4613), o.state(0x512E), o.state(0x5130),
            o.state(0x2CA), o.state(0x5134), o.state(0x306)))
    elif routine == "horiz":
        o.horiz(frames)
        print("player=(%d,%d) cam=(%d,%d) 4629=%d 512e=%d" % (
            o.state(0x4611), o.state(0x4613), o.state(0x512E), o.state(0x5130),
            o.state(0x4629), o.state(0x512E)))
    elif routine == "camy":
        o.camy(frames)
        print("player=(%d,%d) cam=(%d,%d)" % (
            o.state(0x4611), o.state(0x4613), o.state(0x512E), o.state(0x5130)))
    else:
        print("unknown routine", routine)


if __name__ == "__main__":
    main()
