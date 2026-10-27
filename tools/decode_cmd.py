#!/usr/bin/env python3
"""Decode Crystal Caves map-command handlers.

Two interpreters operate on a map row string:
  * 0x1037  "PlaceRegularTile" -- 2-byte-entry table at cs:0xFBF (60 cmds)
  * 0x25F0  "LoadMapRow"       -- case chain over table     at cs:0x24DC (137 cmds)

For each command we extract the argument pushes reaching PlaceTile (call 0x59)
and object spawns (call 0x47d).
"""
import json
from capstone import Cs, CS_ARCH_X86, CS_MODE_16

LOAD = 0x1E00
EXE = "extracted/CC1-UNLZ.EXE"

md = Cs(CS_ARCH_X86, CS_MODE_16)
md.detail = True
md.skipdata = True


def load():
    return open(EXE, "rb").read()[LOAD:]


def disasm(mod, a, b):
    return list(md.disasm(mod[a:b], a))


def sstr(mod, addr):
    n = mod[addr]
    return mod[addr + 1:addr + 1 + n].decode("latin1")


def find_compares(mod, a, b, table_lo, table_hi, stride=2):
    ins = disasm(mod, a, b)
    out = []
    for k, x in enumerate(ins):
        if x.mnemonic == "lcall" and x.op_str.endswith("0x724"):
            di = None
            for j in range(k - 1, max(k - 6, 0), -1):
                if ins[j].mnemonic == "mov" and ins[j].op_str.startswith("di,"):
                    di = int(ins[j].op_str.split(",")[1], 16)
                    break
            nxt = ins[k + 1] if k + 1 < len(ins) else None
            aft = ins[k + 2] if k + 2 < len(ins) else None
            if di is not None and table_lo <= di < table_hi and nxt and nxt.mnemonic in ("je", "jne"):
                out.append({"label": sstr(mod, di), "mn": nxt.mnemonic,
                            "tgt": int(nxt.op_str, 16), "cmp": x.address,
                            "aft": aft.address if aft else b})
    return out


def branch_calls(mod, start, end, terms):
    """Linear scan [start,end); collect pushes; emit on SetCell/object call.

    Object spawns use the pattern::

        push obj_index
        push param1 .. paramN
        mov ax, handler_off
        mov dx, 0x1f9a
        push dx
        push ax
        push cs
        call 0x47d

    so the last three pushes are (handler_seg, handler_off, cs).
    """
    ins = disasm(mod, start, min(end, start + 0x500))
    calls = []
    pushes = []
    ax = None
    dx = None
    for x in ins:
        o = x.op_str
        if x.mnemonic == "mov" and o.startswith("ax, "):
            v = o.split(", ", 1)[1]
            try:
                ax = int(v, 16)
            except ValueError:
                ax = v
        elif x.mnemonic == "mov" and o.startswith("dx, "):
            v = o.split(", ", 1)[1]
            try:
                dx = int(v, 16)
            except ValueError:
                dx = v
        elif x.mnemonic == "xor" and o in ("ax, ax", "eax, eax"):
            ax = 0
        elif x.mnemonic == "xor" and o in ("dx, dx", "edx, edx"):
            dx = 0
        elif x.mnemonic == "add" and o.startswith("ax, "):
            ax = "%s+%s" % (ax, o.split(", ", 1)[1])
        elif x.mnemonic == "sub" and o.startswith("ax, "):
            ax = "%s-%s" % (ax, o.split(", ", 1)[1])
        elif x.mnemonic == "add" and o.startswith("dx, "):
            dx = "%s+%s" % (dx, o.split(", ", 1)[1])
        elif x.mnemonic == "sub" and o.startswith("dx, "):
            dx = "%s-%s" % (dx, o.split(", ", 1)[1])
        elif x.mnemonic == "dec" and o == "ax":
            ax = "%s-1" % ax
        elif x.mnemonic == "inc" and o == "ax":
            ax = "%s+1" % ax
        elif x.mnemonic == "dec" and o == "dx":
            dx = "%s-1" % dx
        elif x.mnemonic == "inc" and o == "dx":
            dx = "%s+1" % dx
        elif x.mnemonic == "push":
            if o == "ax":
                pushes.append(("ax", ax))
            elif o == "dx":
                pushes.append(("dx", dx))
            elif o.startswith("word ptr"):
                pushes.append(("mem", o))
            else:
                pushes.append(("reg", o))
        elif x.mnemonic == "pop":
            pushes = []
        elif x.mnemonic in ("call", "lcall"):
            if o == "0x59":
                p = pushes[:-1] if pushes and pushes[-1][1] == "cs" else pushes
                calls.append(("SetCell", [q[1] for q in p]))
            elif o == "0x47d":
                if len(pushes) >= 3 and pushes[-1][1] == "cs":
                    p = pushes[:-3]
                    seg = pushes[-3][1]
                    off = pushes[-2][1]
                else:
                    p = pushes[:-2]
                    seg = pushes[-2][1] if len(pushes) >= 2 else dx
                    off = pushes[-1][1] if pushes else ax
                calls.append(("Object", [q[1] for q in p] + [off, seg]))
            pushes = []
        elif x.mnemonic in ("ret", "retf") or (x.mnemonic == "jmp" and o in terms):
            break
    return calls


SLOTS = ["P18/0x5094(block1)", "P16/0x5096(block2)", "P14/0x5098(block3)",
         "P12=y(col)", "P10=x(row)", "P0e/shape0(0x5090)", "P0c/0x5092(shape2)",
         "P0a/0x509a(shape3)", "P08->byte0x509d", "P06->byte0x509e"]


def decode(mod, a, b, tlo, thi, terms, maxn=400):
    comp = find_compares(mod, a, b, tlo, thi)
    rows = {}
    for i, c in enumerate(comp):
        nxt = comp[i + 1]["cmp"] if i + 1 < len(comp) else b
        if c["mn"] == "je":
            start, end = c["tgt"], nxt
        else:
            start, end = c["aft"], c["tgt"]
        rows[c["label"]] = branch_calls(mod, start, end, terms)
    return rows


def main():
    mod = load()
    reg = decode(mod, 0x1037, 0x24d9, 0xFBF, 0x1037, {"0x24d3"})
    spec = decode(mod, 0x25f0, 0x6edd, 0x24DC, 0x25EE, {"0x6e05", "0x6ed0", "0x24d3"})
    json.dump(reg, open("notes/regular_cmds.json", "w"), indent=1)
    json.dump(spec, open("notes/special_cmds.json", "w"), indent=1)
    print("regular: %d cmds; special: %d cmds" % (len(reg), len(spec)))
    print("special labels:", "".join(sorted(spec, key=lambda s: (len(s), s))))
    for lab, calls in list(spec.items()):
        print("%r -> %s" % (lab, [(k, [p if not isinstance(p, str) else p for p in v]) for k, v in calls]))


if __name__ == "__main__":
    main()
