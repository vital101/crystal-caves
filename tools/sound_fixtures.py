#!/usr/bin/env python3
"""Generate the exact sound-driver oracle trace.

Runs the real 0x1FBBF play_sound routine and the real 0x0A3F INT 1Ch ISR in
Unicorn.  Captures, for every one of the 36 sound IDs, the per-tick PIT
divisor and speaker-gate state, plus a selection/preemption matrix.

Outputs notes/sound_fixtures.json.  This is the reference for
tools/export_sound.py and wasm/test_sound.js.

Usage:
    python3 tools/sound_fixtures.py
"""
import hashlib
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

import emulate_load as E                      # noqa: E402
from oracle_physics import PhysicsOracle      # noqa: E402
import sound_banks as SB                      # noqa: E402

from unicorn import UC_HOOK_INSN              # noqa: E402
from unicorn.x86_const import (               # noqa: E402
    UC_X86_INS_IN, UC_X86_INS_OUT,
    UC_X86_REG_CS, UC_X86_REG_IP, UC_X86_REG_SP,
)

OUT = os.path.join(ROOT, "notes", "sound_fixtures.json")
EXE = os.path.join(ROOT, "extracted", "CC1-UNLZ.EXE")

PLAY_SOUND = 0x1FBBF
SOUND_ISR = 0x0A3F
RET = 0xFFF0

DS = E.DATA_SEG
DS_SEG = DS << 4
BANK_BASE = 0x40000          # three 16 KiB slots, all below the 1 MiB map
BANK_STRIDE = 0x4000

OFF_ENABLED = 0x513E
OFF_PLAYING = 0x513F
OFF_SLOT = 0x5136
OFF_NOTE = 0x5138
OFF_BANK = 0xA0BC


class SoundOracle:
    def __init__(self):
        self.o = PhysicsOracle(2)
        self.mu = self.o.mu
        self.banks = {}
        self.shadow = {"port61": 0, "lsb": 0, "msb": 0, "toggle": 0,
                       "divisor": 0}
        self._install_banks()
        self._install_port_hooks()

    # ---------------------------------------------------------------- setup
    def _install_banks(self):
        for bank in range(1, SB.BANK_COUNT + 1):
            data = SB.load_bank(bank)
            base = BANK_BASE + (bank - 1) * BANK_STRIDE
            self.mu.mem_write(base, data)
            self.banks[bank] = data
            # The loader stores entry 1 at DS:0x510C; play_sound indexes
            # [bank*4 + 0x5108].
            self.mu.mem_write(DS_SEG + 0x5108 + bank * 4,
                              struct.pack("<HH", 0, (base >> 4) & 0xFFFF))

    def _install_port_hooks(self):
        def hook_out(uc, port, size, value, user):
            v = value & 0xFF
            if port == 0x61:
                self.shadow["port61"] = v
            elif port == 0x43:
                pass
            elif port == 0x42:
                if self.shadow["toggle"] == 0:
                    self.shadow["lsb"] = v
                    self.shadow["toggle"] = 1
                else:
                    self.shadow["msb"] = v
                    self.shadow["toggle"] = 0
                    self.shadow["divisor"] = (
                        self.shadow["lsb"] | (self.shadow["msb"] << 8))

        def hook_in(uc, port, size, user):
            if port == 0x61:
                return self.shadow["port61"]
            return 0xFF

        self.mu.hook_add(UC_HOOK_INSN, hook_out, aux1=UC_X86_INS_OUT)
        self.mu.hook_add(UC_HOOK_INSN, hook_in, aux1=UC_X86_INS_IN)

    # ---------------------------------------------------------------- state
    def reset_driver(self):
        self.shadow.update({"port61": 0, "lsb": 0, "msb": 0, "toggle": 0,
                            "divisor": 0})
        self.o.set_global(OFF_ENABLED, 1, 1)
        self.o.set_global(OFF_PLAYING, 0, 1)
        self.o.set_global(OFF_SLOT, 1, 2)
        self.o.set_global(OFF_NOTE, 1, 2)
        self.o.set_global(OFF_BANK, 1, 2)

    def state(self):
        return {
            "enabled": self.o.state(OFF_ENABLED) & 0xFF,
            "playing": self.o.state(OFF_PLAYING) & 0xFF,
            "bank": self.o.state(OFF_BANK) & 0xFFFF,
            "slot": self.o.state(OFF_SLOT) & 0xFFFF,
            "note": self.o.state(OFF_NOTE) & 0xFFFF,
        }

    def play(self, sound_id):
        self.o.call_far(PLAY_SOUND, [sound_id])

    def tick(self):
        """Invoke the real INT 1Ch handler once and capture port state."""
        sp = E.STACK_PTR
        for value in (0x0202, 0, RET):     # flags, CS, return IP for iret
            sp = (sp - 2) & 0xFFFF
            E.w16(self.mu, E.STACK_SEG, sp, value)
        self.mu.reg_write(UC_X86_REG_SP, sp)
        self.mu.reg_write(UC_X86_REG_CS, 0)
        self.mu.reg_write(UC_X86_REG_IP, SOUND_ISR)
        self.o.stopped = None
        self.mu.emu_start(SOUND_ISR, 0x110000, count=200000)
        return [self.shadow["divisor"], 1 if (self.shadow["port61"] & 3) else 0]

    # ---------------------------------------------------------------- traces
    def trace(self, sound_id, max_ticks=400):
        self.reset_driver()
        self.play(sound_id)
        trace = []
        for _ in range(max_ticks):
            trace.append(self.tick())
            if (self.o.state(OFF_PLAYING) & 0xFF) == 0:
                break
        else:
            raise RuntimeError("sound %d did not stop" % sound_id)
        return trace


def run_selection(oracle, steps):
    oracle.reset_driver()
    want = []
    for step in steps:
        op = step["op"]
        if op == "play":
            oracle.play(step["id"])
        elif op == "enable":
            oracle.o.set_global(OFF_ENABLED, 1, 1)
        elif op == "disable":
            oracle.o.set_global(OFF_ENABLED, 0, 1)
        elif op == "stop":
            oracle.o.set_global(OFF_PLAYING, 0, 1)
        elif op == "ticks":
            for _ in range(step["n"]):
                oracle.tick()
        else:
            raise ValueError("unknown op %r" % op)
        want.append(oracle.state())
    return want


SELECTION = [
    {"name": "idle_1", "steps": [{"op": "play", "id": 1}]},
    {"name": "idle_13", "steps": [{"op": "play", "id": 13}]},
    {"name": "idle_36", "steps": [{"op": "play", "id": 36}]},
    {"name": "low_then_high",
     "steps": [{"op": "play", "id": 1}, {"op": "play", "id": 3}]},
    {"name": "high_then_low",
     "steps": [{"op": "play", "id": 3}, {"op": "play", "id": 1}]},
    {"name": "equal_priority",
     "steps": [{"op": "play", "id": 3}, {"op": "play", "id": 4}]},
    {"name": "same_id_retrigger",
     "steps": [{"op": "play", "id": 3}, {"op": "play", "id": 3}]},
    {"name": "disabled_idle",
     "steps": [{"op": "disable"}, {"op": "play", "id": 5}]},
    {"name": "disabled_while_playing",
     "steps": [{"op": "play", "id": 5}, {"op": "disable"},
               {"op": "play", "id": 3}]},
    {"name": "stop_then_play",
     "steps": [{"op": "play", "id": 5}, {"op": "stop"},
               {"op": "play", "id": 1}]},
    {"name": "tick_to_end",
     "steps": [{"op": "play", "id": 8}, {"op": "ticks", "n": 15}]},
    {"name": "tick_partial",
     "steps": [{"op": "play", "id": 8}, {"op": "ticks", "n": 3}]},
]


def main():
    oracle = SoundOracle()
    records = SB.load_records()
    ids = {}
    bad = []
    for rec in records:
        trace = oracle.trace(rec["id"])
        model = SB.record_trace(rec)
        if trace != model:
            bad.append(rec["id"])
            first = next((i for i, (a, b) in enumerate(zip(trace, model))
                          if a != b), min(len(trace), len(model)))
            print("MISMATCH id %d at tick %d: oracle=%r model=%r"
                  % (rec["id"], first,
                     trace[first:first + 1], model[first:first + 1]))
        ids[str(rec["id"])] = {
            "bank": rec["bank"],
            "slot": rec["slot"],
            "priority": rec["priority"],
            "gate": rec["gate"],
            "ticks": len(trace),
            "trace": trace,
        }
    if bad:
        raise SystemExit("sound oracle/model mismatch: %r" % bad)

    selection = []
    for case in SELECTION:
        selection.append({
            "name": case["name"],
            "steps": case["steps"],
            "want": run_selection(oracle, case["steps"]),
        })

    exe_sha = hashlib.sha256(open(EXE, "rb").read()).hexdigest()
    doc = {
        "exe_sha256": exe_sha,
        "pit_hz": SB.PIT_HZ,
        "tick_divisor": SB.TICK_DIVISOR,
        "tick_hz": round(SB.PIT_HZ / SB.TICK_DIVISOR, 6),
        "count": len(records),
        "ids": ids,
        "selection": selection,
    }
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w") as fh:
        json.dump(doc, fh, indent=1, sort_keys=True)
        fh.write("\n")
    print("wrote %s (%d ids, %d selection cases)"
          % (OUT, len(ids), len(selection)))


if __name__ == "__main__":
    main()
