# Player physics, startup state, and timing (Phase 1)

All addresses are load-module offsets (`file = load + 0x1E00`).  Runtime:
`CS = 0`, `DS = ES = 0x2CD3`, `SS:SP = 0x370C:0x4000`.

## Startup state (`0xC3E`)

The level loader (`0xB1A9`) only writes objects/cells/player/camera.  Gameplay
defaults come from the new-game initializer at `0xC3E`:

- vertical displacement table `DS:0xA111` (20 bytes, index = `[0x5134]`),
- `[0x2BA]=0` walk speed, `[0x2CA]=0` impulse, `[0xA0B4]=0` walk phase,
- `[0x306]=0` jump flag, `[0xA0FD]=0` airborne, `[0x5132]=1` gravity scale,
- `[0x5134]=1` vertical phase,
- `[0x509C]=5`, `[0x509E]=3`, `[0xA0EA]=0xFFFF`,
- `[0x2E0]=0x14`, `[0xA0C0]=7`,
- platform/carry/interaction latches cleared, `[0x513C]=1`.

`tools/emulate_startup.py` runs the real `0xC3E` (with video/sound far-calls
stubbed), then the real loader for all 19 levels.  It writes
`notes/startup.json`; `tools/make_startup_data.py` turns that into
`wasm/startup_data.h` (never edit by hand).

Important correction: an earlier note listed the table as `DS:0xF245`.  That
was a miscomputed effective address.  The instruction is
`mov al,[di+0xA111]` with `DI = [0x5134]`, so the table base is **`0xA111`**.
Index 0 is unused; `0xC3E` initializes indices 1..19:

```
index:  0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19
value:  0  0  8  8  8  4  4  2  2  2  2  2  2  2  2  4  4  8  8  8
```

This is per-frame displacement, not acceleration.

The loader sets per-level player/camera/shape state, including hub door cell
IDs in level 2.  `out/portable/cells.bin` and the static oracle renders are
generated from the startup+loader image, not loader-only.

## Timing

- `0xFC24` programs PIT channel 0 with divisor `0x2147` (8519), ≈140.06 Hz.
- INT 1Ch handler `0xA3F` increments `[0x2B60]` once per tick.
- The normal game loop advances a frame when `[0x2B60] > [0xA0C0]` (=7),
  then clears it.  One game frame = 8 ticks ≈ **17.51 Hz** (57.12 ms).
- `wasm/play.html` uses a fixed-step accumulator at that rate.

## Main-loop order (normal state)

`0xF4E6` branch:

```
0xE011  horizontal movement + camera-x, tail 0xE2FD camera-y
0xE36B  jump start + ascending (calls 0xDCB9)
0xE5D3  hazard/contact                         (Phase 2)
0xEBF2  items/fire/exit; calls 0xCF6C at 0xEEAE (Phase 2/3)
0xEAEA  timers/status                          (Phase 2/3)
0xD72A  object update loop
```

Phase 1 fixtures use `0xE011 → 0xE36B` for pure physics and add `0xD72A` for
platform-carry cases.  `wasm/objects.c` exposes this as
`cc_objects_step_physics()` / `cc_objects_step()`.

## Input mapping

Keyboard flags (set by ISR `0x727`):

| key | flag |
|---|---|
| left | `[0x338E]` |
| right | `[0x338F]` |
| jump/up | `[0x338B]` |
| fire | `[0x3390]` |

`[0x338A]` is joystick fire, not the keyboard key.  Relevant ISR side
effects mirrored by `cc_input_set`/oracle fixtures:

- no left/right held -> `[0xA0B4]=0` (walk acceleration resets),
- left/right held -> `[0x30A]=0` (animation suppression off).

## Horizontal movement (`0xE011`, camera tail `0xE2FD`)

- `[0x2CA]` is a decaying impulse velocity; `[0x2BA]` is walk speed.
- `0x6BE` bumps `[0xA0B4]` and sets `[0x2BA]=2` for phases 1–2, `4` from
  phase 3 through `0x7D00`.
- Right/left are processed sequentially (both keys -> both actions).
- Walk step is ±`[0x2BA]`; collision uses helper `0x1F9A:0x43` with
  `xoff = ±[0x2BA] -/+ [0x5126]`.  A blocked move clears `[0xA0B4]`.
- Camera-x scrolls only near screen edges:
  - right: `cam_x < 0x140 && cam_x + 0xAA < player_x`,
  - left: `cam_x > 0 && cam_x + 0x96 > player_x`,
  - clamp `0..0x140`.
- Platform x-carry `[0x5118]` is applied after walking, moves the camera by
  the same delta (the handler already moved the player), then clears.
- Camera-y (`0xE2FD`) only follows while `[0x5128]` is nonzero:
  - negative: player above `cam_y+0x50` -> `cam_y += [0x5128]`, clamp `0x10`,
  - positive: player below `cam_y+0x78` -> `cam_y += [0x5128]`, clamp `0xC8`.

## Jump and vertical movement (`0xE36B`, `0xDCB9`)

- `[0x306]` = ascending/jump flag; `[0xA0FD]` = airborne; `[0x5134]` = phase.
- Jump start is allowed when `[0x306]==0 && [0xA0FD]==0` and jump is pressed,
  provided the 9-point box is clear at `yoff = -4*[0x5132]`.
- On start: `[0x30A]=0`, `[0x306]=1`, `[0x2C6]=0`, `[0x5134]=0`, platform
  latches cleared, sound 1.
- Ascending: set animation shape, increment phase; at phase 10 set
  `[0x306]=0`/`[0x5134]=9`; subtract `table[phase]*[0x5132]` from `player_y`;
  collision check at `-table[phase]*[0x5132]`; camera-y follows.
- Falling (`0xDCB9`): increment/clamp phase to 19, set airborne; move down by
  `table[phase]*[0x5132]` (skipped while riding a platform before phase 0xB);
  floor test at `player_y+0x10` for cell byte `0x11`; special cell byte `0x12`
  has the `0xDE20` snap rule; reversed gravity (`[0x5132] == -1`) tests
  `player_y-1` and snaps via `((y+0xC)/16)*16`.
- Landing clears `[0x2C8]` and restores the player shape from p14/p12.
- Airborne state at end clears platform latches.

## Platform carry

`0x6AFA` (kind 19) and `0x6D31` (kind 20/0x27) were re-checked against the
real handlers:

- attach requires `[0x5132]==1`, player bottom within ±5 px of platform y,
  player center within `x-4 .. x+0x14`; horizontal also requires
  `[0x5134] > 0xA`;
- handlers move the player by `[0x5118]`/`[0x5128]` after moving the platform;
- camera consumes the same carry delta in `0xE011`/`0xE2FD`.

## C state mapping (`wasm/objects.c`)

Player globals are the slot-1 object record fields:

| original | meaning | C field |
|---|---|---|
| `0x4611/0x4613` | player x/y | `player_x/player_y` (kept in sync with `obj[1]`) |
| `0x4619` | block/tile | `player_block` |
| `0x461B` | animation frame | `anim_timer` |
| `0x461D/0x461F` | left/right shapes | `shape_b/shape_a` |
| `0x4627` | current shape | `shape_player` |
| `0x4629` | facing | `move_dir` |
| `0x460D` | slot-1 p02 | `g_460d` |

Other key fields: `walk_speed` (`0x2BA`), `impulse` (`0x2CA`), `walk_phase`
(`0xA0B4`), `jump_phase` (`0x5134`), `jump_flag` (`0x306`), `airborne`
(`0xA0FD`), `anim_suppress` (`0x30A`), `gravity_scale` (`0x5132`),
`cam_x/cam_y` (`0x512E/0x5130`), carry `g_5118/g_5128/g_5126`.

All 16-bit arithmetic goes through `wadd/wsub/wmul/wneg` so 8086 wraparound is
preserved.

## Fixtures / commands

- `tools/oracle_physics.py` runs the real routines from the exact DS image.
- `tools/physics_fixtures.py` writes `notes/physics_fixtures.json` (16
  scenarios, 670 frames): walking/turning/walls/camera/jump/head-bump/fall/
  reversed gravity, plus level-2/3 platform carry.
- `wasm/test_physics.js` compares every frame and platform object state.
- `make test-physics` — fast regression using the committed fixture.
- `make test-oracle-physics` — regenerate fixtures with Unicorn, then test.
- `make startup` — regenerate `notes/startup.json` + `wasm/startup_data.h`.
- `make test-all` includes `test-physics`.

## Related coverage

The old "out of scope" list is implemented and covered elsewhere:
`0xE5D3` hazard/contact and cell events (`notes/cell_fixtures.json`,
`make test-cells`), `0xEBF2` items/fire/exit and `0xEAEA` timers
(`notes/combat_fixtures.json`, `make test-combat`), exits and hub door
transitions (`notes/loader_fixtures.json`, `make test-loader`).  Physics
fixtures intentionally isolate movement from those interactions.
