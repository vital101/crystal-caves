#!/usr/bin/env python3
"""Crystal Caves Sound bank parser and tick-trace model.

The three CC1-*.SND files are 12 fixed-size 610-byte records each:

    0x000..0x254  note sequence (uint16 Hz, terminated by 0xFFFF)
    0x256         unused / second terminator
    0x258 (600)   priority
    0x25a (602)   metadata (not read by the original driver)
    0x25c (604)   gate period; every Nth note is silenced
    0x25e (606)   unused metadata
    0x260 (608)   unused / uninitialised in the last slot

Playback is driven by the INT 1Ch ISR at 0x0A3F.  Each 140.06 Hz tick sets
the next note with 0x287F6 (divisor = 1193181 / Hz, frequencies <= 18 are
ignored), increments the note index, silences the speaker when
(note_index % gate) == 0, and stops on the 0xFFFF terminator.

The model below is validated against the real driver by
tools/sound_fixtures.py; all 36 records match tick-for-tick.
"""
import os
import struct

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXTRACT = os.path.join(HERE, "extracted")

BANK_COUNT = 3
RECORDS_PER_BANK = 12
RECORD_SIZE = 610
NOTE_WORDS = 299          # 0..596; 0x256 is the second terminator
PIT_HZ = 1193181          # 0x1234DD, as used by 0x287F6
TICK_DIVISOR = 0x2147     # PIT channel 0 divisor -> 140.06 Hz
SOUND_COUNT = BANK_COUNT * RECORDS_PER_BANK
UNUSED_FREQ = 18          # setfreq returns without changing state for <= 18


def bank_path(bank):
    return os.path.join(EXTRACT, "CC1-%d.SND" % bank)


def load_bank(bank):
    with open(bank_path(bank), "rb") as fh:
        data = fh.read()
    if len(data) != RECORDS_PER_BANK * RECORD_SIZE:
        raise ValueError("CC1-%d.SND: expected %d bytes, got %d"
                         % (bank, RECORDS_PER_BANK * RECORD_SIZE, len(data)))
    return data


def parse_record(data, bank, slot):
    """Parse one 610-byte record.  bank/slot are 1-based."""
    if not 1 <= bank <= BANK_COUNT:
        raise ValueError("bank out of range: %d" % bank)
    if not 1 <= slot <= RECORDS_PER_BANK:
        raise ValueError("slot out of range: %d" % slot)
    base = (slot - 1) * RECORD_SIZE
    notes = []
    for i in range(NOTE_WORDS):
        w = struct.unpack_from("<H", data, base + i * 2)[0]
        if w == 0xFFFF:
            break
        notes.append(w)
    if not notes:
        raise ValueError("empty note list in bank %d slot %d" % (bank, slot))
    if len(notes) >= NOTE_WORDS:
        raise ValueError("unterminated note list in bank %d slot %d"
                         % (bank, slot))
    return {
        "id": (bank - 1) * RECORDS_PER_BANK + slot,
        "bank": bank,
        "slot": slot,
        "notes": notes,
        "priority": struct.unpack_from("<H", data, base + 600)[0],
        "meta_602": struct.unpack_from("<H", data, base + 602)[0],
        "gate": struct.unpack_from("<H", data, base + 604)[0],
        "meta_606": struct.unpack_from("<H", data, base + 606)[0],
        "meta_608": struct.unpack_from("<H", data, base + 608)[0],
    }


def load_records():
    records = []
    for bank in range(1, BANK_COUNT + 1):
        data = load_bank(bank)
        for slot in range(1, RECORDS_PER_BANK + 1):
            records.append(parse_record(data, bank, slot))
    return records


def model_trace(notes, gate):
    """Return [(divisor, speaker_on), ...] for every playback tick.

    Mirrors 0x0A3F: set note, increment index, silence on
    (index % gate) == 0, and silence + stop at the terminator.
    """
    if gate == 0:
        raise ValueError("gate period must be nonzero")
    divisor = 0
    speaker = 0
    trace = []
    n = len(notes)
    for k, freq in enumerate(notes, start=1):
        if freq > UNUSED_FREQ:
            divisor = PIT_HZ // freq
            speaker = 1
        if (k + 1) % gate == 0:
            speaker = 0
        if k == n:
            speaker = 0
        trace.append([divisor, speaker])
    return trace


def record_trace(record):
    return model_trace(record["notes"], record["gate"])


def all_traces():
    return {r["id"]: record_trace(r) for r in load_records()}


# ------------------------------------------------------------------ synth
def synth_reference(trace, sample_rate):
    """Deterministic reference rendering used by the JS synth golden test.

    Uses the same integer phase/boundary math as wasm/sound_synth.js:
      * one 32-bit phase accumulator, increment =
        floor(PIT_HZ * 2^32 / (divisor * sample_rate));
      * tick k covers samples [floor(k*sr*D/PIT_HZ), floor((k+1)*sr*D/PIT_HZ));
      * speaker on renders a square wave, speaker off renders zero.
    Returns signed 16-bit samples (the JS side uses Float32 +/-1).
    """
    if not trace:
        return []
    total = (len(trace) * sample_rate * TICK_DIVISOR) // PIT_HZ
    out = [0] * total
    phase = 0
    divisor = 0
    speaker = 0
    for k, (d, s) in enumerate(trace):
        start = (k * sample_rate * TICK_DIVISOR) // PIT_HZ
        end = ((k + 1) * sample_rate * TICK_DIVISOR) // PIT_HZ
        if end > total:
            end = total
        divisor = d
        speaker = s
        inc = 0
        if divisor:
            inc = (PIT_HZ << 32) // (divisor * sample_rate)
        for i in range(start, end):
            if divisor:
                phase = (phase + inc) & 0xFFFFFFFF
            if speaker:
                out[i] = 12000 if phase < 0x80000000 else -12000
    return out


def synth_sha256(trace, sample_rate):
    import hashlib
    pcm = synth_reference(trace, sample_rate)
    h = hashlib.sha256()
    # Pack as little-endian signed 16-bit so both Python and JS hash the same
    # bytes (JS converts its Float32 output to int16 before hashing).
    h.update(struct.pack("<%dh" % len(pcm), *pcm))
    return h.hexdigest()


def main():
    for r in load_records():
        tr = record_trace(r)
        print("id=%2d bank=%d slot=%2d pri=%3d gate=%d ticks=%3d"
              % (r["id"], r["bank"], r["slot"], r["priority"], r["gate"],
                 len(tr)))


if __name__ == "__main__":
    main()
