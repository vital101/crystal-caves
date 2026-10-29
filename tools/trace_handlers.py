#!/usr/bin/env python3
"""Inventory the Crystal Caves object-type handlers.

The 46 handlers live in segment 0x1F9A.  A far pointer 0x1F9A:OFF therefore
maps to load-module offset 0x1F9A0+OFF.

Capstone is given flat load offsets, so its near-branch targets use the low 16
bits of the flat address as IP.  For code in segment 0x1F9A the actual IP is
`flat - 0x1F9A0`, so add DELTA (0x660) modulo 0x10000 to every near target.

Outputs:
  notes/handlers.json       machine-readable inventory
  notes/handlers.md         summary + per-handler notes
  notes/handlers_disasm.txt full annotated disassembly, reachable instructions
"""
import json
import os
import sys
from collections import Counter, defaultdict, deque

from capstone import Cs, CS_ARCH_X86, CS_MODE_16, CS_OP_IMM, CS_OP_MEM, CS_OP_REG

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
EXE = os.path.join(ROOT, "extracted", "CC1-UNLZ.EXE")
OBJ_JSON = os.path.join(ROOT, "out", "portable", "objects.json")
OUT_JSON = os.path.join(ROOT, "notes", "handlers.json")
OUT_MD = os.path.join(ROOT, "notes", "handlers.md")
OUT_ASM = os.path.join(ROOT, "notes", "handlers_disasm.txt")

HDR = 0x1E00
SEG = 0x1F9A
BASE = SEG << 4
DELTA = (0x10000 - (BASE & 0xFFFF)) & 0xFFFF
MAX_INS = 4000

FIELD_NAMES = {
    0x45DE: "state",
    0x45E0: "flag",
    0x45E2: "tile_ref",
    0x45E4: "x",
    0x45E6: "y",
    0x45E8: "x_prev",
    0x45EA: "y_prev",
    0x45EC: "block",
    0x45EE: "p10",
    0x45F0: "p12",
    0x45F2: "p14",
    0x45F4: "p16",
    0x45F6: "p18",
    0x45F8: "p1a",
    0x45FA: "shape",
    0x45FC: "p1e",
    0x45FE: "p20",
    0x4600: "p22",
    0x4602: "p24",
    0x4604: "p26",
    0x4606: "ready",
    0x4607: "handler_off",
    0x4609: "handler_seg",
}

GLOBAL_NAMES = {
    0x2B5E: "scancode",
    0x2B6C: "key_left",
    0x2B6E: "key_right",
    0x2B70: "key_up",
    0x2B72: "key_down",
    0x2B7A: "key_fire",
    0x338B: "in_left",
    0x338D: "in_right",
    0x338F: "in_up",
    0x3391: "in_down",
    0x3393: "in_fire",
    0x4611: "player_x",
    0x4613: "player_y",
    0x508C: "map_cells",
    0x5120: "spawn_cursor",
    0x5122: "base_shape",
    0x5134: "jump_phase",
    0x5136: "anim_counter",
    0x513E: "sound_enable",
    0x513F: "sound_busy",
    0x5140: "toggle_5140",
    0xA0B4: "phase",
    0xA0B6: "scroll",
    0xA0BC: "anim_phase",
    0xA10A: "suppress_tile",
}

HELPER_NAMES = {
    0x1FCB0: "anim_next(+0x10, max +0x16)",
    0x1FCF7: "anim_select_shape(+0x12/+0x10 by +0x1E)",
    0x1FBBF: "play_sound_or_effect(id)",
    0x2177B: "player_proximity_test(x, y, radius?)",
}

WRITE_DEST = {
    "mov", "movzx", "movsx", "lea", "pop", "inc", "dec", "neg", "not",
    "add", "sub", "and", "or", "xor", "shl", "shr", "sar", "sal", "rol",
    "ror", "rcl", "rcr", "xchg",
}
READ_DEST = {
    "cmp", "test", "push", "inc", "dec", "neg", "not", "add", "sub", "and",
    "or", "xor", "shl", "shr", "sar", "sal", "rol", "ror", "rcl", "rcr",
    "xchg",
}


def load_module():
    data = open(EXE, "rb").read()
    return data[HDR:]


def reg_name(ins, reg):
    try:
        return ins.reg_name(reg)
    except Exception:
        return None


def mem_info(ins, op):
    m = op.mem
    base = reg_name(ins, m.base) if m.base else None
    index = reg_name(ins, m.index) if m.index else None
    disp = m.disp & 0xFFFF
    return base, index, disp


def annotate_mem(ins, op):
    base, index, disp = mem_info(ins, op)
    if base == "di" and not index:
        if disp in FIELD_NAMES:
            return "obj." + FIELD_NAMES[disp]
        if 0x508C <= disp < 0x50A0:
            return "map_cell+0x%02X" % (disp - 0x508C)
        if disp in GLOBAL_NAMES:
            return GLOBAL_NAMES[disp]
        return "di+0x%04X" % disp
    if base is None and index is None:
        if disp in GLOBAL_NAMES:
            return GLOBAL_NAMES[disp]
        if 0x45DE <= disp < 0x460B:
            return "obj." + FIELD_NAMES.get(disp, "?")
        return "ds:0x%04X" % disp
    if base == "bp":
        return "stack%+d" % (disp if disp < 0x8000 else disp - 0x10000)
    return "%s%s+0x%04X" % (base or "", ("+" + index) if index else "", disp)


def ins_text(ins):
    return ("%s %s" % (ins.mnemonic, ins.op_str)).strip()


def translate_near(imm):
    return BASE + ((imm + DELTA) & 0xFFFF)


def trace_one(mod, md, entry_off):
    start = BASE + entry_off
    q = deque([start])
    seen = set()
    rows = []
    fields_read, fields_written = set(), set()
    globals_read, globals_written = set(), set()
    calls = []
    indirect = []
    immediates = Counter()
    bad = []

    while q and len(rows) < MAX_INS:
        a = q.popleft()
        if a in seen or a < 0 or a >= len(mod):
            continue
        seen.add(a)
        gen = md.disasm(mod[a:a + 16], a)
        ins = next(gen, None)
        if ins is None:
            bad.append(a)
            continue

        rec = {
            "addr": a,
            "offset": a - BASE,
            "mnemonic": ins.mnemonic,
            "op_str": ins.op_str,
            "text": ins_text(ins),
        }
        rows.append(rec)

        # ----- operands / memory annotation -----
        for oi, op in enumerate(ins.operands):
            if op.type == CS_OP_MEM:
                label = annotate_mem(ins, op)
                if oi == 0 and ins.mnemonic in WRITE_DEST:
                    if label.startswith("obj."):
                        fields_written.add(label[4:])
                    elif not label.startswith(("stack", "di+", "ds:")):
                        globals_written.add(label)
                if (oi == 0 and ins.mnemonic in READ_DEST) or oi > 0:
                    if label.startswith("obj."):
                        fields_read.add(label[4:])
                    elif not label.startswith(("stack", "di+", "ds:")):
                        globals_read.add(label)
            elif op.type == CS_OP_IMM:
                immediates[op.imm & 0xFFFF] += 1

        # ----- control flow -----
        mn = ins.mnemonic
        term = mn in ("ret", "retf", "iret", "iretd")
        if term:
            continue

        if mn == "jmp":
            if ins.operands and ins.operands[0].type == CS_OP_IMM:
                q.append(translate_near(ins.operands[0].imm))
            else:
                indirect.append({"at": "0x%04X" % (a - BASE), "text": ins_text(ins)})
            continue

        if mn.startswith("j") and mn not in ("jmp",):
            if ins.operands and ins.operands[0].type == CS_OP_IMM:
                q.append(translate_near(ins.operands[0].imm))
            else:
                indirect.append({"at": "0x%04X" % (a - BASE), "text": ins_text(ins)})
            q.append(a + ins.size)
            continue

        if mn == "lcall":
            ops = ins.operands
            if len(ops) >= 2 and ops[0].type == CS_OP_IMM and ops[1].type == CS_OP_IMM:
                s = ops[0].imm & 0xFFFF
                o = ops[1].imm & 0xFFFF
                lin = (s << 4) + o
                calls.append({"kind": "far", "target": "%04X:%04X" % (s, o),
                              "linear": "0x%05X" % lin,
                              "helper": HELPER_NAMES.get(lin, "?")})
            else:
                indirect.append({"at": "0x%04X" % (a - BASE), "text": ins_text(ins)})
            q.append(a + ins.size)
            continue

        if mn == "call":
            if ins.operands and ins.operands[0].type == CS_OP_IMM:
                tgt = translate_near(ins.operands[0].imm)
                # The game's far-call convention is `push cs; call near`.
                prev = rows[-2] if len(rows) >= 2 else None
                kind = "local_far" if prev and prev["text"] == "push cs" else "near"
                calls.append({"kind": kind, "target": "0x%04X" % (tgt - BASE),
                              "linear": "0x%05X" % tgt,
                              "helper": HELPER_NAMES.get(tgt, "?")})
            else:
                indirect.append({"at": "0x%04X" % (a - BASE), "text": ins_text(ins)})
            # calls return; keep decoding fallthrough
            q.append(a + ins.size)
            continue

        # normal instruction: fall through
        q.append(a + ins.size)

    # sort rows by address for a readable listing
    rows.sort(key=lambda r: r["addr"])

    flags = []
    body = [r for r in rows if r["text"] not in ("push bp", "mov bp, sp", "mov sp, bp", "pop bp")]
    if len(body) <= 1:
        flags.append("noop")
    if "player_proximity_test(x, y, radius?)" in [c.get("helper") for c in calls]:
        flags.append("proximity")
    if any(c.get("helper", "").startswith("play_sound") for c in calls):
        flags.append("sound")
    if any(c.get("helper", "").startswith("anim_") for c in calls):
        flags.append("anim")
    if "state" in fields_written:
        flags.append("writes_state")
    if "shape" in fields_written:
        flags.append("writes_shape")
    if any(k.startswith("map_cell") for k in globals_read | globals_written):
        flags.append("map_cell")
    if any(k.startswith("in_") for k in globals_read):
        flags.append("reads_input")
    if "scroll" in globals_read or "scroll" in globals_written:
        flags.append("scroll")
    if indirect:
        flags.append("indirect_cf")

    return {
        "offset": "0x%04X" % entry_off,
        "entry_load": "0x%05X" % start,
        "instruction_count": len(rows),
        "end_offsets": sorted({"0x%04X" % (r["addr"] - BASE) for r in rows
                               if r["mnemonic"] in ("ret", "retf", "iret")}),
        "fields_read": sorted(fields_read),
        "fields_written": sorted(fields_written),
        "globals_read": sorted(globals_read),
        "globals_written": sorted(globals_written),
        "calls": calls,
        "indirect": indirect,
        "immediates": ["0x%04X" % v for v, _ in immediates.most_common(20)],
        "flags": flags,
        "bad_decode": ["0x%05X" % a for a in bad],
        "instructions": rows,
    }


def main():
    mod = load_module()
    md = Cs(CS_ARCH_X86, CS_MODE_16)
    md.detail = True
    md.skipdata = True

    table = json.load(open(OBJ_JSON))["handler_table"]
    # preserve order, dedupe
    offs = []
    for x in table:
        if x not in offs:
            offs.append(x)

    entries = []
    helper_hits = Counter()
    for off in offs:
        e = trace_one(mod, md, off)
        entries.append(e)
        for c in e["calls"]:
            helper_hits[c.get("linear", c.get("target"))] += 1

    json.dump(entries, open(OUT_JSON, "w"), indent=1)

    # full disassembly listing
    with open(OUT_ASM, "w") as f:
        for e in entries:
            f.write("\n===== handler 0x%s (entry %s, %d ins) =====\n"
                    % (e["offset"][2:], e["entry_load"], e["instruction_count"]))
            for r in e["instructions"]:
                f.write("  %04X: %-28s ; %s\n" % (r["offset"], r["text"], r["addr"]))

    # markdown summary
    with open(OUT_MD, "w") as f:
        f.write("# Object handler inventory (segment 0x1F9A)\n\n")
        f.write("Generated by `tools/trace_handlers.py`.  Entry addresses are\n")
        f.write("segment offsets; load offset = `0x1F9A0 + offset`.\n\n")
        f.write("## Summary\n\n")
        f.write("| # | off | ins | flags | fields written | calls |\n")
        f.write("|---|-----|-----|-------|----------------|-------|\n")
        for i, e in enumerate(entries):
            callstr = ", ".join(
                ("%s%s" % (c.get("target"), (" " + c["helper"]) if c.get("helper", "?") != "?" else ""))
                for c in e["calls"][:4]) or "-"
            f.write("| %d | %s | %d | %s | %s | %s |\n" % (
                i, e["offset"], e["instruction_count"], ",".join(e["flags"]) or "-",
                ",".join(e["fields_written"]) or "-", callstr))
        f.write("\n## Helper call frequency\n\n")
        for h, n in helper_hits.most_common(30):
            f.write("- `%s` x%d\n" % (h, n))
        f.write("\n## Per-handler detail\n\n")
        for e in entries:
            f.write("### %s (%d instructions)\n\n" % (e["offset"], e["instruction_count"]))
            f.write("- entry: `%s`\n" % e["entry_load"])
            f.write("- flags: %s\n" % (", ".join(e["flags"]) or "-"))
            f.write("- fields read: %s\n" % (", ".join(e["fields_read"]) or "-"))
            f.write("- fields written: %s\n" % (", ".join(e["fields_written"]) or "-"))
            f.write("- globals read: %s\n" % (", ".join(e["globals_read"]) or "-"))
            f.write("- globals written: %s\n" % (", ".join(e["globals_written"]) or "-"))
            if e["calls"]:
                f.write("- calls: %s\n" % ", ".join(
                    "%s%s" % (c.get("target"), (" (%s)" % c["helper"]) if c.get("helper", "?") != "?" else "")
                    for c in e["calls"]))
            if e["indirect"]:
                f.write("- indirect control flow: %s\n" % ", ".join(
                    "%s %s" % (x["at"], x["text"]) for x in e["indirect"]))
            f.write("\n")

    print("handlers:", len(entries))
    print("wrote", OUT_JSON)
    print("wrote", OUT_MD)
    print("wrote", OUT_ASM)
    print("top helpers:")
    for h, n in helper_hits.most_common(15):
        print("  %s x%d" % (h, n))
    for e in entries:
        if e["bad_decode"]:
            print("bad decode in", e["offset"], e["bad_decode"])


if __name__ == "__main__":
    main()
