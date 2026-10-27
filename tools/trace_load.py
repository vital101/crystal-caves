#!/usr/bin/env python3
"""Trace the loader execution inside Unicorn for debugging the oracle."""
import json, os, struct, sys
from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_CODE
from unicorn.x86_const import (
    UC_X86_REG_AX, UC_X86_REG_BX, UC_X86_REG_CX, UC_X86_REG_DX,
    UC_X86_REG_SI, UC_X86_REG_DI, UC_X86_REG_BP, UC_X86_REG_SP,
    UC_X86_REG_CS, UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS,
    UC_X86_REG_IP,
)
from capstone import Cs, CS_ARCH_X86, CS_MODE_16

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
EXE = os.path.join(ROOT, "extracted", "CC1-UNLZ.EXE")
module = open(EXE, "rb").read()[0x1E00:]
md = Cs(CS_ARCH_X86, CS_MODE_16)

level = int(sys.argv[1]) if len(sys.argv) > 1 else 0
limit = int(sys.argv[2]) if len(sys.argv) > 2 else 200
start_at = int(sys.argv[3], 0) if len(sys.argv) > 3 else 0

mu = Uc(UC_ARCH_X86, UC_MODE_16)
mu.mem_map(0, 0x100000)
mu.mem_write(0, module)
for site in (0x52B, 0x560):
    mu.mem_write(site, b"\x83\xc4\x08\x90\x90")
mu.mem_write(0xFFF0, b"\xf4")

ss, sp = 0x370C, 0x4000
def w16(off, v):
    mu.mem_write((ss << 4) + off, struct.pack("<H", v & 0xFFFF))
sp -= 2; w16(sp, (0x10000 - (level + 1)) & 0xFFFF)
sp -= 2; w16(sp, 0xFFF0)
mu.reg_write(UC_X86_REG_SS, ss)
mu.reg_write(UC_X86_REG_SP, sp)
mu.reg_write(UC_X86_REG_CS, 0)
mu.reg_write(UC_X86_REG_DS, 0)
mu.reg_write(UC_X86_REG_ES, 0)

n = 0
out = []
def hook(uc, address, size, user):
    global n
    n += 1
    if n < start_at:
        return
    cs = uc.reg_read(UC_X86_REG_CS)
    code = bytes(mu.mem_read(address, 16))
    ins = next(md.disasm(code, address), None)
    txt = "%s %s" % (ins.mnemonic, ins.op_str) if ins else "??"
    out.append("%3d %04X:%04X sp=%04X ax=%04X bx=%04X cx=%04X dx=%04X si=%04X di=%04X bp=%04X | %s"
               % (n, cs, address,
                  uc.reg_read(UC_X86_REG_SP), uc.reg_read(UC_X86_REG_AX),
                  uc.reg_read(UC_X86_REG_BX), uc.reg_read(UC_X86_REG_CX),
                  uc.reg_read(UC_X86_REG_DX), uc.reg_read(UC_X86_REG_SI),
                  uc.reg_read(UC_X86_REG_DI), uc.reg_read(UC_X86_REG_BP), txt))
    if len(out) >= limit:
        uc.emu_stop()

mu.hook_add(UC_HOOK_CODE, hook)
try:
    mu.emu_start(0xB1A9, 0, count=50_000_000)
except Exception as e:
    print("ERR", e)
print("\n".join(out))
