"""LZEXE 0.90 / 0.91 unpacker.

Python port of the classic UNLZEXE by Mitugu (Kou) Kurizono, as maintained in
the public `unlzexe` sources (0.90/0.91 support).  Reads a packed DOS EXE and
writes the decompressed DOS EXE, reconstructing the relocation table.

Usage: python3 unlzexe.py packed.exe unpacked.exe
"""

import struct
import sys

# LZEXE 0.90 and 0.91 decompressor stubs share their first 20 bytes and then
# diverge.  Byte 20 is 0xB4 for 0.90 and 0xFD for 0.91.
STUB_PREFIX = bytes([
    0x06, 0x0E, 0x1F, 0x8B, 0x0E, 0x0C, 0x00, 0x8B,
    0xF1, 0x4E, 0x89, 0xF7, 0x8C, 0xDB, 0x03, 0x1E,
    0x0A, 0x00, 0x8E, 0xC3,
])


class UnpackError(Exception):
    pass


def u16(buf, off):
    return buf[off] | (buf[off + 1] << 8)


class BitStream:
    """Bit reader sharing a single cursor with the literal/match byte reader.

    In the original UNLZEXE both the bit stream and the raw bytes read from the
    same FILE*, so a cursor is shared; refills read 16-bit little-endian words.
    """

    def __init__(self, data, cursor):
        self.data = data
        self.cursor = cursor
        self.count = 0x10
        self.buf = self._getw()

    def _getw(self):
        data = self.data
        c = self.cursor[0]
        if c + 2 > len(data):
            self.cursor[0] = len(data)
            return 0
        w = data[c] | (data[c + 1] << 8)
        self.cursor[0] = c + 2
        return w

    def getbit(self):
        b = self.buf & 1
        self.count -= 1
        if self.count == 0:
            self.buf = self._getw()
            self.count = 0x10
        else:
            self.buf >>= 1
        return b


def read_head(data):
    ihead = list(struct.unpack("<16H", data[:0x20]))
    if ihead[0] not in (0x5A4D, 0x4D5A) or ihead[0x0D] != 0 or ihead[0x0C] != 0x1C:
        raise UnpackError("not an LZEXE file")
    entry = ((ihead[4] + ihead[0x0B]) << 4) + ihead[0x0A]
    sig = data[entry:entry + 21]
    if len(sig) < 21 or sig[:len(STUB_PREFIX)] != STUB_PREFIX:
        raise UnpackError("unknown LZEXE signature")
    if sig[20] == 0xFD:
        ver = 91
    elif sig[20] == 0xB4:
        ver = 90
    else:
        raise UnpackError("unknown LZEXE version byte %#x" % sig[20])
    return ihead, ver


def reloc91(data, fpos):
    out = bytearray()
    pos = fpos + 0x158
    rel_off = 0
    rel_seg = 0
    rel_count = 0
    while True:
        if pos >= len(data):
            raise UnpackError("reloc table overran input")
        span = data[pos]
        pos += 1
        if span == 0:
            if pos + 2 > len(data):
                raise UnpackError("truncated reloc span")
            span = u16(data, pos)
            pos += 2
            if span == 0:
                rel_seg = (rel_seg + 0x0FFF) & 0xFFFF
                continue
            if span == 1:
                break
        rel_off = (rel_off + span) & 0xFFFF
        rel_seg = (rel_seg + ((rel_off & ~0x0F) >> 4)) & 0xFFFF
        rel_off &= 0x0F
        out += struct.pack("<HH", rel_off, rel_seg)
        rel_count += 1
    return out, rel_count


def reloc90(data, fpos):
    out = bytearray()
    pos = fpos + 0x19D
    rel_seg = 0
    rel_count = 0
    while rel_seg != 0x10000:
        if pos + 2 > len(data):
            raise UnpackError("truncated reloc90 table")
        c = u16(data, pos)
        pos += 2
        for _ in range(c):
            if pos + 2 > len(data):
                raise UnpackError("truncated reloc90 entry")
            rel_off = u16(data, pos)
            pos += 2
            out += struct.pack("<HH", rel_off, rel_seg)
            rel_count += 1
        rel_seg += 0x1000
    return out, rel_count


def unpack(data, ihead, inf, out_start):
    fpos = (ihead[0x0B] - inf[4] + ihead[4]) << 4
    cursor = [fpos]
    bits = BitStream(data, cursor)

    win = bytearray(0x4500)
    p = 0
    out = bytearray()

    def getc():
        c = cursor[0]
        if c >= len(data):
            raise UnpackError("input exhausted while unpacking")
        cursor[0] = c + 1
        return data[c]

    while True:
        if p > 0x4000:
            out += win[0:0x2000]
            p -= 0x2000
            win[0:p] = win[0x2000:0x2000 + p]

        if bits.getbit():
            win[p] = getc()
            p += 1
            continue

        if not bits.getbit():
            length = bits.getbit() << 1
            length |= bits.getbit()
            length += 2
            span = getc() | 0xFF00
        else:
            span = getc()
            tmp = getc()
            span |= (((tmp & ~0x07) << 5) | 0xE000)
            length = (tmp & 0x07) + 2
            if length == 2:
                length = getc()
                if length == 0:
                    break
                if length == 1:
                    continue
                length += 1

        if span & 0x8000:
            span -= 0x10000

        for _ in range(length):
            win[p] = win[p + span]
            p += 1

    if p:
        out += win[0:p]
    return bytes(out), cursor[0]


def main(packed_path, out_path):
    data = open(packed_path, "rb").read()
    ihead, ver = read_head(data)

    cs0000 = (ihead[0x0B] + ihead[4]) << 4
    inf = list(struct.unpack("<8H", data[cs0000:cs0000 + 16]))

    ohead = list(ihead)
    ohead[0x0A] = inf[0]
    ohead[0x0B] = inf[1]
    ohead[0x08] = inf[2]
    ohead[0x07] = inf[3]
    ohead[0x0C] = 0x1C

    if ver == 91:
        reloc, rel_count = reloc91(data, cs0000)
    else:
        reloc, rel_count = reloc90(data, cs0000)
    ohead[3] = rel_count

    # Header bytes: original first 0x1c, then reloc table, padded to 0x200.
    header = bytearray(data[0:0x1C]) + reloc
    pad = (-len(header)) & 0x1FF
    header += b"\x00" * pad
    ohead[4] = len(header) >> 4

    code, _ = unpack(data, ihead, inf, ohead[4] << 4)

    allocsize = ((ihead[1] + 15) >> 4) + ((ihead[2] - 1) << 5) - ihead[4] + ihead[5]
    loadsize = len(code)
    if ihead[6] != 0:
        ohead[5] = (allocsize - ((loadsize + 15) >> 4)) & 0xFFFF
        if ihead[6] != 0xFFFF:
            ohead[6] = (ohead[6] - (ihead[5] - ohead[5])) & 0xFFFF
    ohead[1] = (loadsize + (ohead[4] << 4)) & 0x1FF
    ohead[2] = (loadsize + (ohead[4] << 4) + 0x1FF) >> 9

    head_word_bytes = struct.pack("<14H", *ohead[:14])
    out = bytearray(head_word_bytes) + header[0x1C:] + code

    with open(out_path, "wb") as fh:
        fh.write(out)

    print("LZEXE 0.%d: %s -> %s (%d bytes code, %d relocs, header %#x)"
          % (ver, packed_path, out_path, loadsize, rel_count, ohead[4] << 4))
    return out


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    main(sys.argv[1], sys.argv[2])
