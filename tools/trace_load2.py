#!/usr/bin/env python3
"""Trace the loader, skipping video/sound/utility far calls, printing a ring
buffer of the last N instructions before the emulator stops."""
import collections
import os
import struct
import sys

from unicorn import (Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_CODE, UC_HOOK_INTR,
                     UC_HOOK_MEM_FETCH_UNMAPPED, UC_HOOK_INSN_INVALID)
from unicorn.x86_const import (
    UC_X86_REG_AX, UC_X86_REG_BX, UC_X86_REG_CX, UC_X86_REG_DX,
    UC_X86_REG_SI, UC_X86_REG_DI, UC_X86_REG_BP, UC_X86_REG_SP,
    UC_X86_REG_CS, UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS,
    UC_X86_REG_IP, UC_X86_REG_FLAGS,
)
from capstone import Cs, CS_ARCH_X86, CS_MODE_16, CS_OP_IMM

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
module = open(os.path.join(ROOT, "extracted", "CC1-UNLZ.EXE"), "rb").read()[0x1E00:]
md = Cs(CS_ARCH_X86, CS_MODE_16)
md.detail = True

SKIP = {0x28B5, 0x26C5, 0x0FC5}
level = int(sys.argv[1]) if len(sys.argv) > 1 else 0
ring_n = int(sys.argv[2]) if len(sys.argv) > 2 else 60
watch = int(sys.argv[3], 0) if len(sys.argv) > 3 else 0

mu = Uc(UC_ARCH_X86, UC_MODE_16)
mu.mem_map(0, 0x100000)
mu.mem_write(0, module)
mu.write = None
for site in (0x52B, 0x560):
    mu.mem_write(site, b"\x83\xc4\x08\x90\x90")
mu.mem_write(0xFFF0, b"\xf4")

ss, sp = 0x370C, 0x4000


def w16(off, v):
    mu.mem_write((ss << 4) + off, struct.pack("<H", v & 0xFFFF))


sp -= 2
w16(sp, (0x10000 - (level + 1)) & 0xFFFF)
sp -= 2
w16(sp, 0xFFF0)
mu.reg_write(UC_X86_REG_SS, ss)
mu.reg_write(UC_X86_REG_SP, sp)
mu.reg_write(UC_X86_REG_CS, 0)
mu.reg_write(UC_X86_REG_DS, 0x2CD3)
mu.reg_write(UC_X86_REG_ES, 0x2CD3)


def retf_argbytes(cs, ip, window=0x300):
    lin = (cs << 4) + ip
    for ins in md.disasm(module[lin:lin + window], ip):
        if ins.mnemonic == "retf":
            return int(ins.op_str, 16) if ins.op_str else 0
        if ins.mnemonic in ("ret", "iret"):
            return 0
    return 0


ring = collections.deque(maxlen=ring_n)
n = 0
stop_reason = None


def dbg(uc, txt):
    cs = uc.reg_read(UC_X86_REG_CS)
    return ("%04X:%04X sp=%04X ax=%04X bx=%04X cx=%04X dx=%04X si=%04X di=%04X bp=%04X | %s"
            % (cs, uc.reg_read(UC_X86_REG_IP), uc.reg_read(UC_X86_REG_SP),
               uc.reg_read(UC_X86_REG_AX), uc.reg_read(UC_X86_REG_BX),
               uc.reg_read(UC_X86_REG_CX), uc.reg_read(UC_X86_REG_DX),
               uc.reg_read(UC_X86_REG_SI), uc.reg_read(UC_X86_REG_DI),
               uc.reg_read(UC_X86_REG_BP), txt))


def hook(uc, address, size, user):
    global n, stop_reason
    n += 1
    ins = next(md.disasm(bytes(mu.mem_read(address, 16)), address & 0xFFFF), None)
    txt = "%s %s" % (ins.mnemonic, ins.op_str) if ins else "??"
    if ins and ins.mnemonic == "lcall" and len(ins.operands) >= 2 \
            and ins.operands[0].type == CS_OP_IMM:
        seg = ins.operands[0].imm
        if seg in SKIP:
            off = ins.operands[1].imm
            nb = retf_argbytes(seg, off)
            ring.append(dbg(uc, txt + "  [stub %04X:%04X retf %d]" % (seg, off, nb)))
            uc.reg_write(UC_X86_REG_SP, (uc.reg_read(UC_X86_REG_SP) + nb) & 0xFFFF)
            uc.reg_write(UC_X86_REG_IP, (address + ins.size) & 0xFFFF)
            return
    ring.append(dbg(uc, txt))
    if watch and address == watch:
        stop_reason = "watch"
        uc.emu_stop()
    if address == 0xCEEC:
        stop_reason = "load_end"
        uc.emu_stop()
    if address == 0xFFF0:
        stop_reason = "ret"
        uc.emu_stop()


def hook_intr(uc, intno, user):
    global stop_reason
    cs = uc.reg_read(UC_X86_REG_CS)
    ip = uc.reg_read(UC_X86_REG_IP)
    ring.append(dbg(uc, "INT %02X" % intno))
    ax = uc.reg_read(UC_X86_REG_AX)
    if intno == 0x10:
        ah = ax >> 8
        if ah == 0x0F:
            uc.reg_write(UC_X86_REG_AX, (ax & 0xFF00) | 0x13)
    elif intno == 0x16:
        if (ax >> 8) in (0x01, 0x11):
            uc.reg_write(UC_X86_REG_FLAGS, uc.reg_read(UC_X86_REG_FLAGS) | 0x40)
    uc.reg_write(UC_X86_REG_IP, (ip + 2) & 0xFFFF)


def hook_bad(uc, access, address, size, value, user):
    global stop_reason
    stop_reason = "bad fetch at %05X" % address
    uc.emu_stop()

def hook_invalid(uc, user):
    global stop_reason
    stop_reason = "invalid instruction at %04X:%04X" % (
        uc.reg_read(UC_X86_REG_CS), uc.reg_read(UC_X86_REG_IP))
    uc.emu_stop()


mu.hook_add(UC_HOOK_CODE, hook)
mu.hook_add(UC_HOOK_INTR, hook_intr)
mu.hook_add(UC_HOOK_MEM_FETCH_UNMAPPED, hook_bad)
mu.hook_add(UC_HOOK_INSN_INVALID, hook_invalid)
try:
    mu.emu_start(0xB1A9, 0x110000, count=20_000_000)
except Exception as e:
    stop_reason = "ERR %s" % e
print("steps", n, "stop:", stop_reason)
print("\n".join(ring))
