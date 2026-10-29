# Crystal Caves Ep.1 — Object / Entity System (Phase C notes)

All addresses are **load-module offsets** (`file = load + 0x1E00`).
`DS` note: the game runs with `DS = 0x2CD3` (set by the keyboard ISR @0x732),
so data offsets below (0x2xxx, 0x4xxx, 0x5xxx, 0xAxxx) are DS-relative.

## Object record array

- **Base `DS:0x45DE`**, **stride `0x2D` (45 bytes)**, record count **~61**
  (init loop covers indices `4..0x3B` plus index `0`).
- Spawn cursor / next free slot: `[0x5120]` (started at 4 by `setup_level`,
  incremented after each spawn).
- `setup_level` init loop @`0xB1A9`: `[0x2C4]` from `4`; when `[0x2C4]==0x3C`
  it wraps the record index to `0` and then stops → records `4..59` and `0`.

### Record field map (offset = addr − 0x45DE)

| off | addr | sz | meaning | init default |
|-----|------|----|---------|--------------|
| 0x00 | 45DE | w | **state** — `0x63`=free/inactive, `2`=active | 2 |
| 0x02 | 45E0 | w | flag | 0 |
| 0x04 | 45E2 | w | tile/blob ref (spawn callers write `0x7D00`) | 1 |
| 0x06 | 45E4 | w | x (pixels) | −41 (0xFFD7) |
| 0x08 | 45E6 | w | y (pixels) | 0 |
| 0x0A | 45E8 | w | x (prev/velocity base) | −41 |
| 0x0C | 45EA | w | y (prev/velocity base) | 0 |
| 0x0E | 45EC | w | **block** (GFX block; spawn param c) | — |
| 0x10 | 45EE | w | spawn param b (role TBD) | 1 |
| 0x12 | 45F0 | w | spawn param | — |
| 0x14 | 45F2 | w | spawn param | — |
| 0x16 | 45F4 | w | spawn param | — |
| 0x18 | 45F6 | w | spawn param a | 0 |
| 0x1A | 45F8 | w | (cleared on spawn) | 0 |
| 0x1C | 45FA | w | **shape** (GFX shape; spawn param) | 1 |
| 0x1E | 45FC | w | spawn param | 1 |
| 0x20 | 45FE | w | spawn param | 0 |
| 0x22 | 4600 | w | spawn param c | 2 |
| 0x24 | 4602 | w | (unused here) | — |
| 0x26 | 4604 | w | spawn param | 0 |
| 0x28 | 4606 | b | flag (byte) | 1 |
| 0x29 | 4607 | w | **handler offset**  \ | — |
| 0x2B | 4609 | w | **handler segment** / | — |

Handler far-pointer = `[+0x29]:[+0x2B]` → an object-type procedure in segment
`0x1F9A` (see table below). It is invoked by the object update loop to drive
per-type behaviour + animation.

## Spawn function @`0x47D` (far; `retf 0x1C` = 14 word params)

Params (far-call layout, first param at `[bp+6]`):
`[bp+6]`=handler OFF, `[bp+8]`=handler SEG, `[bp+0xa]`..`[bp+0x20]` = fields,
and `[bp+0x20]` = **object index**.

Behavioral summary:
1. `X = [bp+0x1a]<<4`, `Y = [bp+0x18]<<4` (tile→pixel).
2. store selected shape/param at `+0x1C` (chooses `[bp+0x10]` vs `[bp+0x12]`
   based on `[bp+0x16]==1`).
3. If `[0xA10A]==0`: stamp the level base tile at (X,Y) via `SetCell(0x59)`
   with `block=[0x2B8]`, `shape=[0x5122]` — i.e. the object sits on the map bg.
4. If `[+0x26] != 0x63`: draw the object shape with `lcall 0x28B5:0x191F`
   (block `[+0x0E]`, shape `[+0x1C]*0xA0`), at (X,Y) and again at (X, Y+0x190).
5. Copy params into the record; clear `+0x1A`; set `+0x04=1`, `+0x00`(state)
   later set to `2` (active) or `0x63` (free) by the caller; store handler far
   pointer at `+0x29`/`+0x2B`.

Example call site @`0x27D8` (`'['`/object command): pushes `[0x5120]`, then
`0x1e,0x0c, col,row,0,childshape,0xb,0xb,1,frame,0`, then far ptr
`0x1F9A:0x6458`, then `push cs; call 0x47D`; afterwards sets `+0x04=0x7D00`
and increments `[0x5120]`.

## Object-type handler table (segment 0x1F9A)

46 distinct handlers, discovered by scanning all `mov ax,OFF; mov dx,0x1F9A;
push dx; push ax; push cs; call 0x47D` sites (82 spawn sites total):

```
1FB9 1FC2 2176 2850 2877 298A 2ACF 2BD4 2D37 2E25 2F83 323C 32F6 3416
36B0 3904 3A2E 3BE8 3D23 3E70 3F13 3F6C 403A 4145 47B9 48FE 4A4B 4A82
4AD3 4C92 4FD4 52E7 55EC 5855 5BBE 5C8F 6232 62F2 6458 6681 672B 69D3
6AFA 6D31 6FF6 70E4
```

Frequency hints: `0x1FB9` (17 sites) = generic collectible/bg sprite,
`0x5BBE` (8), `0x6232`/`0x6458` (4), `0x3A2E`/`0x403A`/`0x6AFA`/`0x6D31`/
`0x6FF6`/`0x70E4` (2–3).

## Related globals / routines

- `[0xA0B4]` phase counter → `@0x6BE`: sets `[0x2BA]` (anim speed) `2` for
  counts 1–2, `4` for `3..0x7D00`.
- `@0x6EE` toggles `[0x5140]`; `@0x70D` toggles `[0x30C]`.
- **Keyboard ISR @`0x727`**: sets `DS=0x2CD3`, reads port `0x60`, stores raw
  scancode `[0x2B5E]`, and maintains directional flags
  `[0x338B..0x3393]` (left/right/up/down/fire) by comparing against key
  bindings `[0x2B6C]`,`[0x2B6E]`,`[0x2B70]`,`[0x2B7A]` (±0x80 for release).
- `[0xA10A]` — spawn "suppress base-tile stamp" flag.
- Object records live just after `[0x45DE]`; map cell array is `DS:0x508C`.

## Status
- C1 (record layout + init): **done**.
- C2 (spawn + type table): **done** (handler taxonomy extracted).
- C3 (update loop + hub handlers): **done for the hub slice**. The real 0xD72A
  loop and handlers 3F13/6FF6/70E4/3D23/6AFA/6D31 match the Unicorn oracle.
  Remaining handler semantics: see `notes/gameplay.md`.
- C4 (player + physics/collision): **done**. Exact startup DS (`0xC3E`) is
  captured by `tools/emulate_startup.py`; the vertical table is `DS:0xA111`
  indexed by `[0x5134]`. Exact physics/camera/platform carry is validated by
  `tools/oracle_physics.py` + `tools/physics_fixtures.py` and
  `wasm/test_physics.js` (16 scenarios / 670 frames).

## Object update loop (discovery)

Update loop iterates object records (index `* 0x2D`, DI at record base). Key calls:
- `call far [di+0x4607]` (handler ptr, seg:off) at `0xD87C`, `0xD8D8` — conditional on record flags.
- Uses `[0x4606]` (byte at +0x28) as an active/ready flag checked in places.
- Records with state `+0x00` in {0x2, 0x2B} take special paths; state `0xD` and `0x3` also handled.
- `call 0xF38` checks collision/map interaction (takes object index+1 and coords).
- Handler 0x1F9A:0x251D called directly at `0xD918` in one path.
- When `+0x4606` cleared and conditions met, `+0x4606` set to 1.
- Some paths free other objects by setting their state `+0x45DE` to 0 (e.g. `0xDC7F`, `0xDC94`, `0xDCA7`).

Object drawing happens in spawn and in the update loop (`lcall 0x28B5:0x191F` at `0xDC57` draws sprite using block `[di+0x45EC]`, shape from `[di+0x45FA]*0xA0`, plus `[0xA0B6]` scroll).

Frame counter / anim: `[0x5134]` incremented in update-related code (`0xDCBF+`).

Keyboard ISR @`0x727` reads port `0x60`, updates flags `[0x338B]..[0x3393]` (movement/fire).
