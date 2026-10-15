"""Extract the members of a PC-Shrink (.SHR) installer archive.

Format (PC-Install "new" variant, used by Apogee/id shareware):
  archive header : 58 bytes
  per member     : 168-byte header + compressed payload
      +0x00  16 bytes  filename (NUL padded)
      +0x88   4 bytes  compressed size (little endian)
      +0x8C  28 bytes  padding
      +0xA8  ...       PKWARE DCL Implode payload

Usage: python3 extract_shr.py <archive.shr> <outdir>
"""

import os
import sys

from pkware import explode

ARCHIVE_HEADER = 0x3A
MEMBER_HEADER = 0xA8
SIZE_FIELD = 0x88
NAME_FIELD = 0x10


def extract(archive_path, outdir):
    data = open(archive_path, "rb").read()
    if data[14:16] != b"\x74\x00":
        print("warning: unexpected archive magic %s" % data[14:16].hex())

    num_files = int.from_bytes(data[16:18], "little")
    print("archive: %s  members declared: %d" % (archive_path, num_files))

    os.makedirs(outdir, exist_ok=True)
    pos = ARCHIVE_HEADER
    written = []
    for index in range(num_files):
        name = data[pos:pos + NAME_FIELD].split(b"\x00", 1)[0].decode("ascii", "replace")
        comp_size = int.from_bytes(data[pos + SIZE_FIELD:pos + SIZE_FIELD + 4], "little")
        payload = data[pos + MEMBER_HEADER:pos + MEMBER_HEADER + comp_size]

        if len(payload) != comp_size:
            raise SystemExit("truncated payload for %s" % name)
        try:
            raw = explode(payload)
        except Exception as exc:  # noqa: BLE001
            raise SystemExit("failed to decompress %s: %s" % (name, exc))

        out_path = os.path.join(outdir, name)
        with open(out_path, "wb") as fh:
            fh.write(raw)
        written.append((name, comp_size, len(raw)))
        pos += MEMBER_HEADER + comp_size

    print("%-16s %10s %10s" % ("name", "packed", "unpacked"))
    for name, c, u in written:
        print("%-16s %10d %10d" % (name, c, u))
    return written


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    extract(sys.argv[1], sys.argv[2])
