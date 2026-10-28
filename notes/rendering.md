# Crystal Caves Ep.1 — Level Rendering Pipeline (Phase C4 notes)

All addresses are **load-module offsets** (`file = load + 0x1E00`); the game
runs with `DS = 0x2CD3`. Everything below is confirmed against the original
machine code and validated **bit-exact** by the toolchain in
`tools/` (see "Validation" at the end).

## Pipeline overview

```
loader setup_level 0xB1A9
  └─ command tables (regular CS:0xFBF / special CS:0x24DC)
       └─ SetCell CS:0x59  → cell array DS:0x508C (25 rows x 40 cols x 20 B)
            └─ RedrawCell 0x26C5:0x0
                 ├─ draw helper CS:0x0000 (les di,[di+block*4+0x5234])
                 │    ├─ layer1 → opaque blitter   0x28B5:0x29C5
                 │    ├─ layer2 → transparent blitter 0x28B5:0x39B (if shape≠0)
                 │    └─ layer3 → transparent blitter 0x28B5:0x39B (if shape≠0)
                 └─ 0x28B5:0x2EA1 = VRAM rect copy (write mode 1)
```

- Cell geometry: cell *(col,row)* paints pixels `(col*16 .. col*16+15,
  row*16 .. row*16+15)`; full level = **640×400**. Cell address =
  `0x508C + (col+1)*500 + (row+1)*20`.
- Draw order per cell: layer 1, 2, 3 (later layers overpaint earlier ones).

## Cell layout (20 bytes = 10 little-endian words w0..w9)

| layer | block | shape | blitter | condition |
|-------|-------|-------|---------|-----------|
| 1     | w4    | w2    | 0x29C5 (opaque)   | always drawn |
| 2     | w5    | w3    | 0x39B (transparent) | skipped when w3 = 0 |
| 3     | w6    | w7    | 0x39B (transparent) | skipped when w7 = 0 |

`tools/build_cells.py` exports cells as `[b1,s1,b2,s2,b3,s3]` in this order.

### Block 0 → block 20 substitution

The two-wide-object commands (`0xFFD`/`0xFFF`) pass block `[0x2d6]`, which the
binary sets exactly once — level 2's prologue, value **20** — and which then
persists across level loads. A cold boot would leave it 0 and read shape data
through `table[0] = 0:0` (undefined behaviour). Sequential play always yields
20, so all exporters substitute block 0 → 20 (`BLOCK0_SUB` in
`tools/build_cells.py`).

## GFX shape format (CC1.GFX)

- File = 23 blocks × 8003 B; block & shape indices are **1-based**.
- Block = 3-byte header + 50 shapes × 160 B. Shape offset inside the block =
  `shape*160 − 157` (i.e. skip the header, 0-based shape index).
- Block pointers: far-pointer table at **DS:0x5234**, 24 entries × 4 B;
  `table[0] = 0:0`, entries 1..23 → block buffers. The draw helper does
  `les di,[di + block*4 + 0x5234]` then adds the shape offset.
- Shape = **16 rows × 2 groups × 5 bytes** `[mask][plane0 plane1 plane2 plane3]`:
  - mask bit **set** = opaque pixel (EGA bitmask 3CE/idx8);
  - plane bytes go to the map mask planes (3C4/idx2 → 3C5, one byte OUT per
    plane: 1/2/4/8);
  - colour index = `p0 + 2*p1 + 4*p2 + 8*p3` (p0=blue … p3=intensity);
  - **MSB = leftmost** pixel within each group of 8.
- *(The earlier "5 planes × 16 rows × 2 bytes" reading was byte-count
  arithmetic that coincidentally sums to 160 — wrong layout.)*

## The two blitters (0x28B5)

- **Transparent 0x39B**: per 5-byte group, sets the EGA bitmask with a
  **word OUT** `out dx,ax` to 0x3CE (AL=8=index, AH=mask value → lands in
  0x3CF), then four plane-byte writes. Restores `map mask=0xF` and
  `bitmask=0xFF` on exit. `retf 8`.
- **Opaque 0x29C5**: same shape format but **discards the mask byte** (never
  sets the bitmask in its loop — relies on the reset-to-0xFF from the previous
  blitter exit); also restores `0xF/0xFF` on exit. `retf 8`.
- **0x2EA1**: VRAM rectangle copy (EGA write mode 1); only called from
  RedrawCell.

## Palette

The standard **BIOS EGA 16-colour** palette; the game never reprograms it
(the loader path only touches the sequencer map mask and the graphics
bitmask). `out/portable/palette.json` carries exactly these 16 entries.

## Validation chain (all bit-exact)

1. `tools/emulate_render.py` — full-fidelity Unicorn emulation of the real
   loader + real blitters + EGA port/write semantics → PNG.
2. `tools/render_oracle.py` — Python re-render from the emulator's cell dumps
   (`notes/oracle_objects.json`) with the format above.
3. `wasm/render.c` — the clean-room C renderer fed by
   `out/portable/{gfx.bin,levels.bin,palette.json}`.

1 ↔ 2 verified on **all 19 levels** (`tools/png_diff.py`: 0 differing pixels
everywhere); 3 ↔ 2 verified on **all 19 levels** (`wasm/compare_png.js`: 0
differing pixels everywhere). Level 0's emulator-vs-oracle match is what
pinned the shape/cell semantics down; the full sweeps then prove the format
model and the exported intermediate carry them faithfully for every level.

### Emulator gotcha worth remembering

Unicorn reports a 16-bit `out dx,ax` as **one** hook event
(`port=DX, size=2, value=AX`); the AH half targets `port+1` and must be
applied separately. `emulate_render.py`'s first `hook_out` dropped it, which
left the EGA bitmask stuck at 0xFF and punched transparent-shaped holes over
layer-1 paint — 80 px on level 0, exactly where block 20's overlay colours
differ from the block beneath.
