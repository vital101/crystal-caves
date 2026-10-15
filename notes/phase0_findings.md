# Phase 0 Findings — Crystal Caves Episode 1 (v1.0a shareware)

Scope: understand and validate the source data well enough to plan a clean-room
port to WebAssembly. No third-party engine code consulted.

## 1. Provenance / packaging

- Source: `CCSW10A.SHR` — a **PC-Shrink** archive using **PKWARE DCL "Implode"** (v1.03).
  - Archive header 58 bytes (magic `0x3A`); member headers 168 bytes (`0xA8`).
  - Member header layout (as observed): name 16 bytes @+0x00, compressed size
    LE uint32 @**+0x88**, 28 bytes padding, payload @**+0xA8**.
- Version is **1.0a** (Oct 2005 = patched re-release fixing the XP clock/timing bug).
- 15 members extracted (see `../extracted/`):

| Member | Size | Kind |
|---|---|---|
| CC1.EXE | 73256 | LZEXE 0.91 packed executable |
| CC1.GFX | 184069 | ProGraphx 16x16 tileset |
| CC1.TTL | 18328 | PCX 320x200 16-colour (title) |
| CC1.APG | 16304 | PCX 320x200 16-colour (Apogee logo) |
| CC1.CDT | 26717 | PCX 320x200 16-colour (credits) |
| CC1.END | 19625 | PCX 320x200 16-colour (ending) |
| CC1-1.SND, CC1-2.SND, CC1-3.SND | — | Crystal Caves Sound (PC speaker) |
| CC1-F1.MNI, CC1-F2.MNI, CC1-SPL.MNI | — | ProGraphx 8x8 sprites |
| ORDER.FRM, ORDERFRM.TXT, VENDOR.DOC | — | shareware docs |

Tooling written & working: `pkware.py` (DCL decompressor), `extract_shr.py`
(container), `unlzexe.py` (LZEXE 0.90/0.91), `cc_assets.py` (recon).

## 2. Executable

- `CC1.EXE` is **LZEXE 0.91** (sig byte 0xFD at stub+20; stub at `cs:0000` = file 0x114E0).
- UNLZEXE result: **184,288-byte load module**, 1867 relocations, header 0x1E00 bytes.
- MZ header (unpacked): entry **CS:IP = 0000:EF9B**, SS:SP = 370C:4000, minalloc 0x1099.
- **Runtime→file mapping: `file_offset = seg000:offset + 0x1E00`** (load module base).

### Toolchain (identified)
- **Borland Turbo Pascal (DOS).** Decisive evidence: the byte string at file
  `0x2DB4C` is exactly the Turbo Pascal SYSTEM-unit runtime-error string:
  `"Runtime error " 00 " at " 00 "." 0D 0A 00` (cf. TP SYS:024C).
  Its handler prints via `mov ah,6 / int 21h`.
- **Genus ProGraphx EGA/VGA Toolbox** for graphics — string:
  `"Version 1.5 320x200 PAN EGA/VGAProGraphx EGA/VGA Toolbox"` +
  `"Copyright (c) Genus Microprogramming, Inc. 1988-89"`.
- Exact TP version & memory model: TBD via Ghidra in Phase 1 (near code — CS=0,
  single ≤64K code segment; data appears far/segmented).

## 3. Assets (formats validated)

- **PCX** (`.APG/.CDT/.END/.TTL`): v5, RLE (enc=1), 1 bpp, **planes=4**, 320x200,
  bytes-per-line = 40, **16 colours**. ✓
- **`CC1.GFX`**: ProGraphx tileset, **1150 tiles** of **160 bytes** (5 planes x 16
  rows x 2 bytes) + **69-byte** header = 184069. ✓
- **`*.MNI`**: ProGraphx 8x8 sprite chunk — **25 sprites** of 80 bytes
  (5 planes x 8 rows x 2 bytes) + 48-byte header, then 48 bytes trailing junk. ✓
- **`*.SND`**: Crystal Caves Sound (modified Inverse-Frequency, PC speaker).

## 4. Levels (embedded in EXE)

- Map blob at **file 0x8CE0** (seg000:0x6EE0), length **17,097 bytes**, followed
  immediately by `setup_level` code.
- Structure: flat run of **417 Pascal-style rows**; each row = length byte `0x28`
  (=40) + 40 tile-code bytes (tile byte = 16x16 tile referenced from CC1.GFX).
- **19 levels**, row counts total 417 exactly:
  - Intro (story) = 5, Finale (story) = 6, Main map = 25
  - Levels 1–16 = 24 rows each, **except L7, L8, L14 = 23 rows** (13x24 + 3x23 = 381).
- **Level boundaries are NOT a data table.** They are encoded as machine
  instructions — an unrolled `load_map_row(row, string*)` sequence inside
  **`setup_level` at seg000:B1A9 (file 0xCFA9)**. Changing map heights requires
  editing code (this shaped the original design and the existing modding tools).

### Level quirks to replicate (from community RE + to confirm in Phase 1)
- Tile codes `0x5B` = extended object (next byte = object type); `0x6E` often
  companions a `0x5B` to give full size/structure.
- The same raw map data is drawn with **per-level tile-index offsets**
  (background/block tile base variables set at top of `setup_level`).
- Some rows are **shared between maps** (compiler optimisation), usually the first row.
- Episode 2 level 2 has two **overlapping rows** (enemy behind a tile).

## 5. Pipeline status

Extract (SHR/DCL) → UNLZEXE → asset parse → level parse: **all validated
end-to-end on Episode 1 data.** Reproduce with:
`python3 tools/extract_shr.py`, `python3 tools/unlzexe.py`, `python3 tools/cc_assets.py`.

## 6. Open items for Phase 1 (Ghidra)

1. Confirm TP version + memory model; recover `setup_level` fully (per-level
   attributes: background/block tile bases, palette, object lists).
2. Decode `0x5B`/`0x6E` extended-object tables and all tile-code semantics.
3. Locate and characterise the main game loop, player/physics, enemies, HUD.
4. Decode ProGraphx header (69-byte GFX / 48-byte MNI) and the Sound format.
5. Establish the DOSBox reference oracle for regression tests (see Phase 0 note
   on oracle setup — pending decision).
