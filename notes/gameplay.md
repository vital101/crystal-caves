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
| `0xF5xx` / `0xF7A1` | object draw: sprite shape = `base_shape + p10 - 1`; player uses `shape_player + anim_timer - 1` |
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
- `0x6681`, `0x672B`, `0x69D3`: death/crystal pickup effects.
- `0x1FC2` (kind 8): proximity-triggered companion spawner/mover.
- `0x323C` (kind 24), `0x3BE8` (kind 21), `0x5BBE` (kind 29/49/50/54/58),
  `0x2176` (kind 52): additional animators/movers ported from the disassembly.
- `0x5855` (kind 48): three-record moving group; animate/move, wall-flip and
  the `Random(100) < 5` companion-respawn path are ported.
- `0x3E70` (kind 36), `0x4A4B` (kind 31): simple two-frame animations.
- `0x4A82` (kind 44): interaction call with `[0x513C]`/`[0x5140]` toggles.
- `0x20406` is fully ported for combat: touch/pickup block, transform block,
  slash gate, normal slash matrix (`0x20824..0x21335`) and power slash matrix
  (`0x21342..0x21775`).
- `0x20247`/`0x831` pickup effect object and the `0x2002D` slot-3 effect
  template are ported.  `0x47D` general spawn, `0x1FFC8` free-slot reuse,
  `0x200FE`/`0x1FD4B` cell tests, `0x259` base-cell stamping and `0x6681`
  cell-erase effect are ported.
- Player fire is `0xCF6C` (called from `0xEBF2`), the attack is object slot 2
  (`DS:0x4638`), and `0x251D`/`0x2850`/`0x2241` drive the slash.
- `0xEAEA` status timers and the `0x5124` death/restart states
  (`0xD17D`, `0xD1C6`, `0xD342`) with `0xCF46` checkpoint restore,
  `0xD15C` reload and `0xD140` latch are ported.

## Cells, crystals, exits and hub doors (Phase 3)

- Player contact is `0xE5D3`, called between jump physics and `0xEBF2`.  It
  calls the cell-event handler `0x0FC5:0xBB26` (linear `0x1B776`) for five
  player-relative points, applies `0xFF9F`/`0xFF9D` hazard cells, and runs the
  collectible pickup path.
- `0xFC5:BB26` handles crystal cells (`w6=0xD`, `w7` 1..4), ammo/power-up
  cells, score pickups, switches/levers, hazard cells, hub doors and cave
  exits.  Crystal collection decrements `[0xA0CC]`, increments `[0x50A2]`,
  adds score and plays sound 8/0x20.
- Counter semantics: `[0x509E]` is health (starts 3, damage decrements),
  `[0x509C]` is ammo, `[0x50A2]` is crystals collected, `[0xA0CC]` is
  crystals remaining.  `cc_lives()` is retained as a compatibility alias for
  health.
- Cave flags are at `[0x50A8+arg]` for args 1..16 (1 = available/uncompleted,
  0 = completed).  `[0xA0F5]` unlocks the exit when `[0xA0CC]==0`.
- Exit/door state: `0xD4E5` is hub-door entry (cell `w0` = destination arg,
  spawn override from `[0x2B72]/[0x2B74]`); `0xD360`/`0xD412` are the cave
  exit animation, completion flag clear, all-caves check and hub load.
- Level args: `0` = hub, `1..16` = caves, `0xFFFF` = intro, `0xFFFE` =
  finale.  `cc_load_arg()` / `cc_level_exit()` use this mapping.
- Dynamic cell rendering is handled by `cc_render_cells` /
  `cc_render_view_cells`; layer mapping is `(w4,w2)`, `(w5,w3)`, `(w6,w7)`.
- Transitions to caves 3–18 replay the real loader's occupancy overlay
  (`out/portable/level_overlay.bin`): only bytes the loader writes are
  replaced, so source leftovers, `[0x2d6]`-dependent map shapes and
  RNG-selected spawn fields match the original.  See
  `tools/export_level_overlay.py`, `notes/loader_tags.json` and
  `wasm/test_loader.js`.
- Hub transitions replay the same v2 overlay: the loader's RNG-driven
  background decorations and flag-driven door cells are regenerated, so the
  hub is not a fresh static image.

HUD and the Phase 5 shell are implemented; see `notes/shell.md`.  The real
HUD routine is `0x0FC5:0xD1E0` (linear `0x1CE30`) and `make test-hud` compares
it against the original routine.

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

## Combat / frame integration (Phase 2)

Normal frame order: `0xE011 → 0xE36B → 0xEBF2 → 0xEAEA → 0xD72A`.
`0xE5D3` hazard contact is implemented (Phase 3); the older combat fixtures
still avoid hazards where they predate that work.

- Fire cadence: `[0x2E0]` increments in `0xEBF2` and must be `> 6` to fire;
  fire resets it to 0.
- Power shot: `[0xA0FF]` bypasses the ammo decrement; `[0xA0C4]` is its
  expiry timer.  Normal slash = slot-2 `p1a=1`, speed 4, p16 6, shapes
  `0x25`/`0x2B`; power = `p1a=2`, speed 8, p16 4, shapes `0x1D`/`0x21`.
- Slot 2 aliases: `0x4638` state, `0x463E` x, `0x4640` y, `0x4642/44` prev,
  `0x4646` tile, `0x464E` p16, `0x4650` p18, `0x4652` p1a, `0x4654` shape,
  `0x4656` vx, `0x465A` speed, `0x465E` kind, `0x4660` ready/hit lock,
  `0x4661/63` handler.
- `0x251D` tests the bottom-edge cells `(x, y+9)` and `(x+0xF, y+9)`; terrain
  hits convert the slash to kind `0x2B` (handler `0x2241`) and clear cell
  bytes `+0x06/+0x10/+0x11` plus `w0=0xFFA1`.
- Cell addressing: assembly `0x508C + (col+1)*500 + (row+1)*20`; the border
  helpers in `objects.c` treat the original `col/row` values as raw +1.
- TP RNG `0x2BB5:0x980`: state `[0x2A6]/[0x2A8]`, multiplier `0x8405`,
  increment 1.  The high word is computed by the original byte-wise
  add/shift sequence, not a plain 32-bit multiply; `cc_rng_next` reproduces
  the register operations exactly.  Return value is `(high) % n`.
- RNG seed, `0x2DA`, `0x30B`, `0x2B8`, `0x511E`, `0x5122`, `0x2F2` and
  checkpoint state are captured per level in `startup_data.h`.

Combat fixtures:

```sh
cd wasm
make test-combat          # committed notes/combat_fixtures.json
make test-oracle-combat   # regenerate from the real routines, then compare
make test-cells           # committed cell/contact/exit fixtures
make test-oracle-cells    # regenerate cell fixtures from Unicorn
```
