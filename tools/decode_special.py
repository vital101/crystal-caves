#!/usr/bin/env python3
"""Decode the map-character -> handler dispatch chain in the level loader.

The loader at 0x25F0 processes one 40-character row.  Each character is
looked up first in the *regular* command table (`0x1037`); if that returns 0
the loader runs a chain of length-prefixed string compares against the
labels stored at 0x24DC.., jumping to an inline handler for a match.

Dispatch block shape::

    BLOCK:
      lea  di, [bp-0x204]      ; one-character string
      push ss / push di
      mov  di, LABEL_ADDR      ; Pascal string: length byte + chars
      push cs / push di
      lcall 0x2BB5:0x724       ; length-aware strcmp
      jne  NEXT_BLOCK          ; single-entry block: fallthrough is handler
      ; --- or ---
      ... second compare with `je HANDLER`
      jmp  NEXT_BLOCK

Handlers are inline; a handler is shared when several `je` entries point at
the same address.  Handler extent is [handler_start, NEXT_BLOCK).

Outputs:
  notes/special_labels.json  label -> handler range + decoded commands
  notes/special_cmds.json    label -> calls (compatible with the old format)
"""
import json
import os

from capstone import Cs, CS_ARCH_X86, CS_MODE_16, CS_OP_IMM

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
EXE = os.path.join(ROOT, "extracted", "CC1-UNLZ.EXE")
HDR = 0x1E00
STRCMP = (0x2BB5 << 4) + 0x724
LOADER_START = 0x25F0
LOADER_END = 0x25F0 + 0x5000
CHAIN_START = 0x2662
EXIT = 0x6E05


def load():
    return open(EXE, "rb").read()[HDR:]


def pascal(data, addr):
    n = data[addr]
    if n == 0 or n > 32 or addr + 1 + n > len(data):
        return None
    return data[addr + 1: addr + 1 + n].decode("latin1")


def disasm_all(data):
    md = Cs(CS_ARCH_X86, CS_MODE_16)
    md.detail = True
    md.skipdata = True
    ins = list(md.disasm(data[LOADER_START:LOADER_END], LOADER_START))
    return ins


def parse_compare(ins, i):
    """Parse one `lea/push/mov/push/lcall/jcc` compare at index i.

    Returns (label_addr, jcc, target, fallthrough, next_index) or None.
    """
    if i + 8 >= len(ins):
        return None
    a = ins[i]
    if a.mnemonic != "lea" or "bp - 0x204" not in a.op_str:
        return None
    if ins[i + 1].mnemonic != "push" or ins[i + 1].op_str != "ss":
        return None
    if ins[i + 2].mnemonic != "push" or ins[i + 2].op_str != "di":
        return None
    m = ins[i + 3]
    if m.mnemonic != "mov" or not m.op_str.startswith("di, "):
        return None
    try:
        label_addr = int(m.op_str.split(", ", 1)[1], 16)
    except ValueError:
        return None
    if ins[i + 4].mnemonic != "push" or ins[i + 4].op_str != "cs":
        return None
    if ins[i + 5].mnemonic != "push" or ins[i + 5].op_str != "di":
        return None
    c = ins[i + 6]
    if c.mnemonic != "lcall" or len(c.operands) < 2:
        return None
    if (c.operands[0].imm << 4) + c.operands[1].imm != STRCMP:
        return None
    j = ins[i + 7]
    if not j.mnemonic.startswith("j") or not j.operands or j.operands[0].type != CS_OP_IMM:
        return None
    return (label_addr, j.mnemonic, j.operands[0].imm & 0xFFFF,
            (j.address + j.size) & 0xFFFF, i + 8)


def build_dispatch(ins):
    """Walk the dispatch chain, returning blocks of compare entries."""
    idx = {x.address: k for k, x in enumerate(ins)}
    blocks = []
    cur = CHAIN_START
    seen = set()
    while cur is not None and cur in idx and cur not in seen:
        seen.add(cur)
        i = idx[cur]
        compares = []
        next_block = None
        while True:
            r = parse_compare(ins, i)
            if r is None:
                break
            label_addr, jcc, target, fall, i = r
            compares.append((label_addr, jcc, target))
            if jcc == "jne":
                next_block = target
                break
            # `je`: another compare may follow in the same block
            if i < len(ins) and ins[i].mnemonic == "lea" and "bp - 0x204" in ins[i].op_str:
                continue
            if i < len(ins) and ins[i].mnemonic == "jmp" and ins[i].operands \
                    and ins[i].operands[0].type == CS_OP_IMM:
                next_block = ins[i].operands[0].imm & 0xFFFF
            break
        blocks.append({"start": cur, "compares": compares, "next": next_block})
        cur = next_block
    return blocks


# ---------------------------------------------------------------------------
# Stack-expression evaluator (same conventions as decode_cmd.py)
# ---------------------------------------------------------------------------

def eval_handler(ins, start, end):
    rows = [x for x in ins if start <= x.address < end]
    calls, pushes = [], []
    ax = dx = None

    def load_src(r, src):
        nonlocal ax, dx
        src = src.strip()
        if src.startswith("word ptr [bp - 0x102]"):
            v = "col"
        elif src.startswith("word ptr [bp + 8]"):
            v = "row"
        elif src.startswith("word ptr [0x5120]"):
            v = "idx"
        elif src.startswith("word ptr [bp - 0x104]"):
            v = "flag"
        else:
            try:
                v = int(src, 16)
            except ValueError:
                v = "?" + src
        if r == "ax":
            ax = v
        elif r == "dx":
            dx = v

    def combine(r, op, v):
        nonlocal ax, dx
        cur = ax if r == "ax" else dx
        if isinstance(cur, int) and isinstance(v, int):
            nv = cur + v if op == "+" else cur - v
        else:
            nv = "%s%s%s" % (cur, op, v)
        if r == "ax":
            ax = nv
        else:
            dx = nv

    for x in rows:
        o = x.op_str
        mn = x.mnemonic
        if mn == "mov":
            parts = o.split(", ", 1)
            if len(parts) == 2:
                dst, src = parts
                if dst in ("ax", "dx"):
                    load_src(dst, src)
        elif mn == "xor" and o in ("ax, ax", "dx, dx"):
            if o[0:2] == "ax":
                ax = 0
            else:
                dx = 0
        elif mn in ("add", "sub"):
            parts = o.split(", ", 1)
            if len(parts) == 2 and parts[0] in ("ax", "dx"):
                try:
                    v = int(parts[1], 16)
                except ValueError:
                    v = parts[1]
                combine(parts[0], "+" if mn == "add" else "-", v)
        elif mn in ("inc", "dec") and o in ("ax", "dx"):
            combine(o, "+" if mn == "inc" else "-", 1)
        elif mn == "shl" and o == "ax, 1":
            ax = "(%s)*2" % ax
        elif mn == "push":
            if o == "ax":
                pushes.append(ax)
            elif o == "dx":
                pushes.append(dx)
            elif o.startswith("word ptr [bp - 0x102]"):
                pushes.append("col")
            elif o.startswith("word ptr [bp + 8]"):
                pushes.append("row")
            elif o.startswith("word ptr [0x5120]"):
                pushes.append("idx")
            elif o.startswith("word ptr [bp - 0x104]"):
                pushes.append("flag")
            elif o.startswith("word ptr"):
                pushes.append("mem:" + o)
            else:
                pushes.append(o)
        elif mn == "pop":
            pushes = []
        elif mn in ("call", "lcall"):
            if mn == "call" and o == "0x59":
                p = pushes[:-1] if pushes and pushes[-1] == "cs" else pushes
                calls.append(("SetCell", p))
            elif mn == "call" and o == "0x47d":
                if len(pushes) >= 3 and pushes[-1] == "cs":
                    p = pushes[:-3]
                    seg, off = pushes[-3], pushes[-2]
                else:
                    p, seg, off = pushes[:-2], (pushes[-2] if len(pushes) >= 2 else dx), (pushes[-1] if pushes else ax)
                calls.append(("Object", p + [off, seg]))
            pushes = []
        elif mn in ("ret", "retf"):
            break
        elif mn == "jmp" and x.operands and x.operands[0].type == CS_OP_IMM:
            tgt = x.operands[0].imm & 0xFFFF
            if tgt == EXIT or not (start <= tgt < end):
                break
    return calls


def main():
    data = load()
    ins = disasm_all(data)
    blocks = build_dispatch(ins)
    print("dispatch blocks:", len(blocks))

    labels = []
    for b in blocks:
        for label_addr, jcc, target in b["compares"]:
            label = pascal(data, label_addr) if label_addr else None
            handler = target if jcc == "je" else (b["compares"][0] and None)
            if jcc == "jne":
                # fallthrough of the jne instruction
                handler = None
                # recompute: handler starts right after the jne
                # (parse_compare does not preserve it; recover from block)
            labels.append({"label": label, "label_addr": label_addr,
                           "jcc": jcc, "target": target,
                           "block": b["start"], "next": b["next"],
                           "handler": handler})

    # For jne entries the handler is the fallthrough: find the jne insn.
    idx = {x.address: k for k, x in enumerate(ins)}
    for ent in labels:
        b = next(b for b in blocks if b["start"] == ent["block"])
        if ent["jcc"] == "jne":
            # last compare in the block
            r = None
            i = idx[ent["block"]]
            while True:
                r = parse_compare(ins, i)
                if r is None:
                    break
                label_addr, jcc, target, fall, i = r
                if label_addr == ent["label_addr"]:
                    ent["handler"] = fall
                    break
                if jcc == "jne":
                    break

    out, compat = {}, {}
    for ent in labels:
        hs, he = ent["handler"], ent["next"]
        if hs is None or he is None or he <= hs:
            continue
        calls = eval_handler(ins, hs, he)
        key = ent["label"]
        # keep first occurrence of duplicate re-checks
        if key in out:
            continue
        out[repr(key)] = {"label": key, "handler": "0x%04X" % hs,
                          "end": "0x%04X" % he, "calls": calls}
        compat[key] = calls
        objs = [c for c in calls if c[0] == "Object"]
        if objs:
            print("%-6r %04X..%04X objects=%d" % (key, hs, he, len(objs)))
            for c in objs:
                print("      ", c[1])

    json.dump(out, open(os.path.join(ROOT, "notes", "special_labels.json"), "w"),
              indent=1, ensure_ascii=False)
    json.dump(compat, open(os.path.join(ROOT, "notes", "special_cmds.json"), "w"),
              indent=1, ensure_ascii=False)
    print("labels:", len(out))
    print("wrote notes/special_labels.json and notes/special_cmds.json")


if __name__ == "__main__":
    main()
