# Gameplay / update-loop notes

All addresses are load-module offsets (`file = load + 0x1E00`).  The object
update loop is `0xD72A`; handlers live in segment `0x1F9A` (load base
`0x1F9A0`).  `DS = 0x2CD3`, object base `0x45DE`, stride `0x2D`.

## Object update loop (`0xD72A`)

- Processes slots **2..59, then 0**.  Slot **1 is the player** and is not
  processed by this loop.
- For every slot: `x_new = x + p1e*p22`, `y_new = y + p20*p22`.
- Empty slot = `kind (+0x26) == 0`.
- If `ready (+0x28) != 0`, advance state `0 -> 1 -> 2`.
- Kinds `0x20 0x2b 0x21 0x14 0x13 0x33 0x2d 0x27 0x3b 0x26 0x2a` call their
  handler directly (only while `ready == 0`).
- Other kinds: if visible (`0xF38`, margin `0x20`) and `[0xA0F8] == 0`, call
  the handler (only while `ready == 0`).
- Not visible: kind `2` may mark `ready=1`; kind `0xD` marks `ready=1` unless
  state 3; kinds `3/0x1D/0x32/0x37/0x31` set `ready=1` and clear the previous
  slot's `p18`; kind `4` sets `ready=1` and clears own `p18`; kind `7` runs
  its init path; kind `0x10` clears `[0x5126]`; kind `0x16` frees the next
  three slots (`state = 0`).
- Handler call convention: `(y_new, x_new, slot_index)`, `retf 6`.

`cc_objects_step_objects()` exposes this loop without player physics/camera so
frame state can be compared directly with the oracle.

## Shared helpers

| Address | Meaning |
|---|---|
| `0x1FCB0` | `anim_next`: `p10++`, wrap when `p10 > p16` |
| `0x1FCF7` | `anim_select_shape`: `shape = (p1e == 1 ? p14 : p12)` |
| `0x1FC73` | queue cell redraw at `(x,y)`; calls `0x26C5:0`/`0x26C5:0x221` |
| `0x1F9E3` | player 9-point clear test (x offsets 3/8/12, y offsets 0/8/15); returns 1 clear, 0 blocked, clears `[0xA0B4]` on block |
| `0xF38` | visibility test against camera; margin is `0x20` in the update loop |
| `0x47D` | object spawn (14 word args, `retf 0x1C`) |
| `0x28B5:0x191F` | object sprite blitter |
| `0x26C5:0` | redraw cell |
| `0x2BB5:0x25D` | memory copy (TP runtime) |

## Level roles (from loader argument mapping)

`level 0 -> 0xFFFF`, `level 1 -> 0xFFFE`, `level N>=2 -> N-2`.

- 0/1: intro/finale story screens
- 2: main mineshaft/hub, 16 doors (`kind 60`, handler `0x3F13`)
- 3..18: the 16 caves; completion order is free

## Handlers ported so far

- `0x3F13` (kind 60): `p18++`, wrap at `0x7D00`, `anim_next` on even `p18`.
- `0x6FF6` (kind 32): flip `p1e` at x bounds, step on even `p18`.
- `0x70E4` (kind 33): partner mover; swaps records with `[0x512A]` at
  `p18 == 3`.
- `0x3D23` (kind 34): two-record door animation.
- `0x6AFA` (kind 19): horizontal moving platform; carries the player when
  attached (`[0x513D]`, `[0x511A]`, `[0x5118]`).
- `0x6D31` (kind 20/0x27): vertical moving platform; carries the player when
  attached (`[0x5142]`, `[0x511C]`, `[0x5128]`).
- `0x1FC2` (kind 8): proximity-triggered companion spawner/mover.
- `0x323C` (kind 24), `0x3BE8` (kind 21), `0x5BBE` (kind 29/49/50/54/58),
  `0x2176` (kind 52): additional animators/movers ported from the disassembly.
- `0x5855` (kind 48): three-record moving group; animate/move and wall-flip
  paths ported (random companion spawn path still TODO).
- `0x3E70` (kind 36), `0x4A4B` (kind 31): simple two-frame animations.
- `0x4A82` (kind 44): interaction call with `[0x513C]`/`[0x5140]` toggles.
- `0x20406` first block (touch damage, kind-specific pickups, 0x2C score and
  effect spawn) and the kind-3 transform block are ported.  The second-hit /
  projectile block (after 0x20707) and cell-based collection/exit paths are
  still missing.
- `0x20247`/`0x831` pickup effect object is ported.

## Golden gameplay oracle

`tools/emulate_game.py <level> <frames> [left] [right] [jump] [fire] --json`
runs the real update loop with segment `0x1F9A` live; video/sound are stubbed.
It starts from the exact `0xC3E` startup image plus the real loader, so
startup defaults and hub door cell IDs are included.  `wasm/test_oracle_objects.js`
compares the C object state and the full 20-byte cell image against that JSON.

```sh
cd wasm
make test-oracle-objects
```

Player physics is covered separately by `tools/oracle_physics.py` +
`tools/physics_fixtures.py`; it seeds the exact startup+loader DS image and
runs `0xE011 → 0xE36B` (plus `0xD72A` for platform carry).  See
`notes/physics.md`.  The real vertical table is `DS:0xA111`, indexed by the
value of `[0x5134]` (not `0xF245`).
