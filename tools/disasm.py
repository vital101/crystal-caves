"""16-bit x86 disassembler / xref helper for the unpacked Crystal Caves EXE.

Address model: the MZ load module (file offset 0x1E00) is segment-relative, so a
far target SEG:OFF maps to load offset (SEG<<4)+OFF, and file offset 0x1E00+that.

CLI:
  py disasm.py asm   <seg:off|off> [count] [file_offset]
  py disasm.py xref  <off>          (load-module offsets; finds direct refs)
  py disasm.py blob  <start> <end>   (auto-decode a code range, following calls)
"""

import sys
import os
from capstone import Cs, CS_ARCH_X86, CS_MODE_16, CS_OP_IMM, CS_OP_MEM

EXE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "extracted",
                   "CC1-UNLZ.EXE")
HDR = None
_mod = None


def load():
    global HDR, _mod
    if _mod is None:
        data = open(EXE, "rb").read()
        HDR = int.from_bytes(data[8:10], "little") * 16
        _mod = data[HDR:]
    return _mod


def to_abs(tok):
    if ":" in tok:
        s, o = tok.split(":")
        return int(s, 16) * 16 + int(o, 16)
    try:
        return int(tok, 16)
    except ValueError:
        return int(tok, 0)


def md():
    m = Cs(CS_ARCH_X86, CS_MODE_16)
    m.detail = True
    return m


def _fmt_addr(load_off):
    seg = load_off >> 4
    return "seg%04x:%04x" % (seg, load_off & 0xF)


def asm(tok, count=64, as_file_off=False):
    mod = load()
    start = to_abs(tok)
    if as_file_off:
        start -= HDR
    m = md()
    print("== disassembly @ %s (file %#x) ==" % (_fmt_addr(start), start + HDR))
    off = start
    n = 0
    for ins in m.disasm(mod[off:], off):
        extra = ""
        shown = ins.op_str
        if ins.mnemonic in ("call", "jmp") and ":" not in ins.op_str:
            tgt = (ins.operands[0].imm) & 0xFFFF
            shown = "0x%x" % tgt
            extra = "   ; -> %s" % _fmt_addr(tgt)
        for op in ins.operands:
            if op.type == CS_OP_IMM:
                tgt = op.imm & 0xFFFF
                if tgt < len(mod):
                    if not extra:
                        extra = "   ; -> %s" % _fmt_addr(tgt)
                    if 0x6EE0 <= tgt < 0x6EE0 + 17097:
                        extra += " [map blob row %#x]" % ((tgt - 0x6EE0) // 41)
        print("  %08x: %-24s %s %s%s"
              % (ins.address, ins.bytes.hex(" "), ins.mnemonic, shown, extra))
        off = ins.address + ins.size
        n += 1
        if n >= count:
            break
    return off


def xref(target, limit=60):
    mod = load()
    m = md()
    target &= 0xFFFF
    hits = []
    for ins in m.disasm(mod, 0):
        for op in ins.operands:
            if op.type == CS_OP_IMM and (op.imm & 0xFFFF) == target:
                hits.append((ins, _fmt_addr(ins.address)))
            elif op.type == CS_OP_MEM and (op.mem.disp & 0xFFFF) == target:
                hits.append((ins, _fmt_addr(ins.address)))
        if ins.mnemonic == "call" or ins.mnemonic == "jmp":
            try:
                parts = ins.op_str.split(":")
                if len(parts) == 2 and int(parts[1], 16) == target:
                    hits.append((ins, _fmt_addr(ins.address)))
            except ValueError:
                pass
        if len(hits) >= limit:
            break
    print("xrefs to %s: %d found (limit %d)" % (_fmt_addr(target), len(hits), limit))
    for ins, a in hits[:limit]:
        print("  %08x: %-22s %s %s" % (ins.address, ins.bytes.hex(" "),
                                       ins.mnemonic, ins.op_str))
    return hits


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return
    cmd = argv[1]
    if cmd == "asm":
        asm(argv[2], int(argv[3]) if len(argv) > 3 else 64,
            as_file_off=(len(argv) > 4 and argv[4] == "file"))
    elif cmd == "xref":
        xref(int(argv[2], 16) if argv[2].startswith("0x") else int(argv[2], 0))
    else:
        print(__doc__)


if __name__ == "__main__":
    main(sys.argv)
