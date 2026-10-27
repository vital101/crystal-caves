#!/usr/bin/env python3
"""Log writes into the object/data area during loader emulation."""
import collections
import os
import struct
import sys

from unicorn import (Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_CODE, UC_HOOK_INTR,
                     UC_HOOK_MEM_WRITE, UC_HOOK_MEM_FETCH_UNMAPPED)
from unicorn.x86_const import (
    UC_X86_REG_AX, UC_X86_REG_BX, UC_X86_REG_CX, UC_X86_REG_DX,
    UC_X86_REG_SI, UC_X86_REG_DI, UC_X86_REG_BP, UC_X86_REG_SP,
    UC_X86_REG_CS, UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS,
    UC_X86_REG_IP,
)
from capstone import Cs, CS_ARCH_X86, CS_MODE_16, CS_OP_IMM

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
module = open(os.path.join(ROOT, "extracted", "CC1-UNLZ.EXE"), "rb").read()[0x1E00:]
md = Cs(CS_ARCH_X86, CS_MODE_16)
md.detail = True

SKIP = {0x28B5, 0x26C5, 0x0FC5}
level = int(sys.argv[1]) if len(sys.argv) > 1 else 0

mu = Uc(UC_ARCH_X86, UC_MODE_16)
mu.mem_map(0, 0x100000)
mu.mem_write(0, module)
mu.mem_write(0x52B, b"\x83\xc4\x08\x90\x90")
mu.mem_write(0x560, b"\x83\xc4\x08\x90\x90")
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
mu.reg_write(UC_X86_REG_DS, 0)
mu.reg_write(UC_X86_REG_ES, 0)


def retf_argbytes(cs, ip, window=0x300):
    lin = (cs << 4) + ip
    for ins in md.disasm(module[lin:lin + window], ip):
        if ins.mnemonic == "retf":
            return int(ins.op_str, 16) if ins.op_str else 0
        if ins.mnemonic in ("ret", "iret"):
            return 0
    return 0


n = 0
writes = collections.Counter()
first = {}
stop = None
LA, HA = 0x45DE, 0x5100


def hook(uc, address, size, user):
    global n
    n += 1
    ins = next(md.disasm(bytes(mu.mem_read(address, 16)), address & 0xFFFF), None)
    if ins and ins.mnemonic == "lcall" and len(ins.operands) >= 2 \
            and ins.operands[0].type == CS_OP_IMM and ins.operands[0].imm in SKIP:
        seg = ins.operands[0].imm
        off = ins.operands[1].imm
        nb = retf_argbytes(seg, off)
        uc.reg_write(UC_X86_REG_SP, (uc.reg_read(UC_X86_REG_SP) + nb) & 0xFFFF)
        uc.reg_write(UC_X86_REG_IP, (address + ins.size) & 0xFFFF)


def hook_w(uc, access, address, size, value, user):
    if LA <= address < HA:
        cs = uc.reg_read(UC_X86_REG_CS)
        ip = uc.reg_read(UC_X86_REG_IP)
        key = (cs, ip)
        writes[key] += 1
        if key not in first:
            first[key] = (address, size, value)


def hook_intr(uc, intno, user):
    ip = uc.reg_read(UC_X86_REG_IP)
    ax = uc.reg_read(UC_X86_REG_AX)
    if intno == 0x10 and (ax >> 8) == 0x0F:
        uc.reg_write(UC_X86_REG_AX, (ax & 0xFF00) | 0x13)
    uc.reg_write(UC_X86_REG_IP, (ip + 2) & 0xFFFF)


def hook_bad(uc, access, address, size, value, user):
    global stop
    stop = "bad fetch %05X" % address
    uc.emu_stop()


mu.hook_add(UC_HOOK_CODE, hook)
mu.hook_add(UC_HOOK_MEM_WRITE, hook_w)
mu.hook_add(UC_HOOK_INTR, hook_intr)
mu.hook_add(UC_HOOK_MEM_FETCH_UNMAPPED, hook_bad)
try:
    mu.emu_start(0xB1A9, 0, count=20_000_000)
except Exception as e:
    stop = str(e)
print("steps", n, "stop", stop)
for (cs, ip), cnt in writes.most_common(30):
    a, s, v = first[(cs, ip)]
    print("%04X:%04X  writes=%-6d first @%05X size=%d val=%04X" % (cs, ip, cnt, a, s, v))
