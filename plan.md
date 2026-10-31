# Crystal Caves Ep.1 — Remaining Work

Goal: a faithful browser/WASM port of the shareware Episode 1.

Locked decisions:

- Full game: intro/finale, 16-door hub, 16 caves, menus, save/high scores, sound.
- Deterministic gameplay state must match the original frame-for-frame.
- Rendering must remain pixel-exact.
- Saves, high scores, key bindings, and config use `localStorage`.
- First vertical slice: hub level 2 + cave level 3 end-to-end.

## Current baseline

Verified today:

- Static level rendering is pixel-exact on all 19 levels (`make test-render-all`).
- Level-2 object state matches the Unicorn oracle for 10 frames.
- Level-3 object state matches the Unicorn oracle for 120 frames.
- Full mutable 20-byte cells are exported as `out/portable/cells.bin` and
  verified byte-for-byte against the oracle.
- All 46 object handlers are implemented (Phase 4).
- The full `0x20406` interaction (touch, transform, slash gate, normal and
  power slash matrices) and the `0x20247`/`0x831` effect object are ported and
  covered by `notes/combat_fixtures.json`.
- Player fire is `0xCF6C` (called from `0xEBF2`), not `0xD17D`/`0xD236`;
  the attack is object slot 2 (`0x4638`) driven by `0x251D`.
- Exact startup DS (`0xC3E`) is recovered. The vertical displacement table is
  at `DS:0xA111` (not `0xF245`), indexed by `[0x5134]`.
- Exact player/camera physics is validated frame-for-frame against the real
  routines: 16 scenarios / 670 frames, including level-2/3 platform carry and
  reversed gravity. Tick rate is 8 × 140.06 Hz PIT ticks ≈ 17.51 Hz.
- `out/portable/cells.bin` now comes from startup+loader and includes the
  level-2 hub door IDs; static oracle renders were regenerated to match.
- Phase 2 is complete: `make test-all`, `make test-oracle-objects`,
  `make test-oracle-physics`, and `make test-oracle-combat` pass.
- Phase 3 is complete for cell events, contact, crystals, exit unlock and the
  exit/door state machines. `notes/cell_fixtures.json` (17 scenarios) covers
  direct cell branches plus full-frame contact/exit runs; `make test-cells`
  and `make test-oracle-cells` pass.
- Dynamic cell rendering is implemented (`cc_render_cells`,
  `cc_render_view_cells`); collected crystals and opened doors redraw.
- Phase 4 is complete: all 46 handlers, all-cave full-frame fixtures and exact
  hub -> cave transition overlays pass.
- Phase 5 is implemented: title/credits/title flow, main menu, PCX screens,
  text windows, oracle-exact HUD, story/finale transitions, help/pause/quit/
  warp, save/restore 0–9, high scores and config persistence in
  `localStorage`.  `notes/hud_fixtures.json`, `make test-hud` and
  `make test-shell` pass.  Committed as `4665c32`.
- P5.R5 exactness verification is implemented: tick-based screen timing,
  captured story/finale pages, original high-score defaults/sort/name entry,
  DOS scan-code key redefine, and browser key mapping.  See
  `notes/shell_exactness_fixtures.json`, `notes/hiscore_fixtures.json`,
  `notes/keymap_fixtures.json`, `make test-exactness`.

Important remaining work:

- Phase 5 polish: browser smoke test; title scroll visual parity is
  approximated from the original tick-driven sequence.
- Sound (Phase 6) is implemented; IDs 2/7 trigger-coverage audit and a
  full oracle-driven title/menu sound flow remain.
- Save/high-score/config hardening and mid-level round-trip verification
  (Phase 7).
- Release regression/CI/docs/performance work (Phase 8).

## Critical path

1. Recover exact player physics/startup data. (done)
2. Complete interaction/combat and projectiles. (done)
3. Implement cell collection, exits, and hub door flow. (done)
4. Port remaining handlers and finish all 16 caves. (done)
5. Add HUD, menus, save/high scores (done in Phase 5); finish sound in Phase 6
   and Phase 5 polish items.
6. Build full regression coverage and release (Phase 7/8).

## Phase 1 — Exact player physics and startup data (COMPLETE)

Why first: every gameplay, collision, and level-transition test depends on it.

Status: complete. See `notes/physics.md` for the address map, table, tick
derivation, input mapping and fixture matrix. Key correction: the vertical
table is `DS:0xA111`, indexed by the value of `[0x5134]`; `0xF245` was a
miscomputed effective address. Startup defaults come from `0xC3E`, not the
loader. The current implementation and fixtures are described below.

Tasks:

- ~~Recover or reconstruct the startup DS image, especially the 20-byte table
  at `DS:0xF245` used by the vertical routine.~~ Done: table is `DS:0xA111`;
  `tools/emulate_startup.py` captures the full `0xC3E` state.
- Determine the original tick rate and main-loop call order
  (`0xCF6C`, `0xDCB9`, `0xE011`, `0xE2FD`, `0xD72A`).
- Port player input, horizontal movement, vertical movement/gravity, jump,
  head bump, camera-x, camera-y, and platform carry exactly.
- ~~Replace the approximate constants in `wasm/objects.c` (`player_speed`,
  `PLAYER_GRAVITY`, `PLAYER_JUMP`, exit heuristic) with derived values.~~ Done.
- Extend `tools/oracle_frame.py` / `tools/emulate_game.py` with scripted player
  fixtures: left/right/jump/fall/camera/platform-carry.
- Add a C-vs-oracle player physics test.

Acceptance:

- Scripted player runs match the oracle frame-for-frame for player/camera state.
- Level 2/3 platform-carry behavior matches the oracle.
- No guessed physics constants remain.

## Phase 2 — Complete `0x20406`, player fire, and combat

Status: COMPLETE. The Phase 2 harness, RNG, fire/slash update, full
`0x20406` matrices, spawn/effect helpers, timer integration, and death/restart
states are ported and covered by `notes/combat_fixtures.json` (74 scenarios /
624 frames, including 120-frame full-frame level-2 and level-3 runs).

### Verified address model (corrects earlier notes)

- Player fire is **`0xCF6C`**, called from the items/fire frame routine
  **`0xEBF2`** at `0xEEAE`. `0xD17D`/`0xD1C6`/`0xD342` are the
  `[0x5124]` death/restart handlers selected by `0xF4E6`, not fire.
- The player attack is **object slot 2** (`DS:0x4638`), not a separately
  spawned projectile. Its record aliases:
  `[0x463E]=x`, `[0x4640]=y`, `[0x4642]/[0x4644]=prev x/y`,
  `[0x4646]=tile/block`, `[0x464E]=p16`, `[0x4650]=p18`,
  **`[0x4652]=p1a` (1 = normal slash, 2 = power slash)**,
  `[0x4654]=shape`, `[0x4656]=vx (facing)`, `[0x465A]=speed`,
  `[0x465E]=kind 2`, **`[0x4660]=ready/hit lock**,
  `[0x4661]/[0x4663]=handler 0x1F9A:0x2850`.
- Slot 2 is driven by `0x1F9A:0x2850 -> 0x251D`. `0x251D` is the real slash
  update: animation, terrain collision, conversion to kind `0x2B` with
  handler `0x2241`, and cell digging. The current C `h_2850` is a no-op and
  `h_251d` only bumps `p18`; the update loop also only special-cases kind 2,
  not kind `0x2B`.
- `0x20406` is one routine spanning `0x20406..0x21778` with four blocks:
  first touch/pickup (`0x20491..0x20597`), transform (`0x2059A..0x20707`),
  slash gate (`0x20707..0x2081A`), slash state 1 (`0x20824..0x21335`) and
  slash state 2 (`0x21342..0x21775`). The current C returns before the
  slash gate and never executes either state matrix.
- `0x251D`'s terrain path clears cell bytes `+0x06`, `+0x10`, `+0x11` and
  writes `w0=0xFFA1`; `0x20406` writes cells at least at `+0x00`, `+0x06`,
  `+0x0A`, `+0x0C`, `+0x0E`. The assembly cell index is
  `0x508C + (col+1)*500 + (row+1)*20`; translate to logical `(col,row)` in C.
- Score is `[0x50A5]` (low) + `[0x50A7]` (high) with `add/adc`. A plain
  `uint32_t` add is not exact once the low word wraps.
- TP RNG is `0x2BB5:0x980` (load `0x2C4D0`): LCG at `0x2C51F`, multiplier
  `CS:[0xA05]=0x8405`, increment 1, state `DS:0x2A6/0x2A8`; it returns
  `(state >> 16) % n` (n=0 returns 0). Startup+loader seed is deterministic
  (currently `0x8A6D4FF1`); capture it in `startup.json` and expose an
  explicit seed API.
- Fire gating: `[0x2E0] > 6` (cooldown, incremented in `0xEBF2`),
  `[0x509C]` ammo (`[0xA0FF]` power shot bypasses the ammo decrement),
  `[0x4660]` hit lock, `[0x4629]` facing. Normal slash: sound `0x0E`,
  speed 4, p16 6, state 1, shapes `0x25`/`0x2B`. Power slash: sound `0x0F`,
  speed 8, p16 4, state 2, shapes `0x1D`/`0x21`, no ammo loss.
- Handler call convention passes predicted `x+vx*speed`, `y+vy*speed` to
  `0x20406`; the first/slash hitbox tests use those args, while later
  branches reload record fields directly. Do not substitute one for the
  other.
- Current stubs that must be finished on combat paths: `0x251D`, `0x2850`,
  `0x2877` (transform/dissolve, 99 instructions), `0x2241` (dug-block
  animator), `0x2002D` (effect spawn from slot 3 template), `0x200FE`
  (cell test helper), and `0x47D` spawn/free semantics.

### P2.0 — Determinism oracle and test surface (do first)

Without this, the rest cannot be validated frame-for-frame.

- Extend `tools/emulate_startup.py` `GLOBALS` with `0x2A6/0x2A8` (RNG),
  `0x50A5/0x50A7` (score), `0x2DA` (fire impulse), `0xA0FF`/`0xA0C4`
  (power shot + timer), `0x50A0/0x50BE/0x50C4/0x50C7/0x50C9` (restart
  checkpoint). Regenerate `startup_data.h` via `make startup`.
- Extend `PhysicsOracle`/`GameOracle` with generic seeding:
  `seed_global(off,val,width)`, `seed_object(slot, fields)`,
  `seed_cell(col,row,bytes)`, `seed_rng(lo,hi)`, per-frame inputs, and a
  `step_full()` that runs the real normal frame order
  (`E011`, `E36B`, `EBF2`, `EAEA`, `D72A`). Leave `0xE5D3` out until it is
  ported; Phase 2 scenarios must avoid hazard cells and assert it is inert.
- Extend dumps/fixtures to include every object field used by combat
  (`p02`, `p04`, `x_prev`, `y_prev`, `handler_seg`), all game globals,
  RNG state, `obj_cursor`, and sound-ID events - not just the subset in
  `test_oracle_objects.js` today.
- Add C test APIs: `cc_obj_set_*` for every field, `cc_cell_set`,
  `cc_rng_seed`, `cc_rng_next`, game-state getters for score low/high,
  ammo/health/invuln/cooldown, and a full-frame step entry point. Keep
  `cc_objects_step_objects()` object-only so existing oracle tests stay
  stable.
- Add `tools/combat_fixtures.py`, committed
  `notes/combat_fixtures.json`, `wasm/test_combat.js`, and
  `make test-combat` / `make test-oracle-combat`. Land the harness with an
  object-only no-op fixture first; switch it to full-frame once P2.2 adds the
  `0xEBF2` prefix and `0xEAEA`.

Exit: a no-op object-only combat scenario round-trips objects, cells,
globals, RNG, cursor, and sound IDs byte-for-byte against the oracle. The
full-frame version becomes the gate in P2.2.

### P2.1 — TP RNG and the `0x5855` random branch

- Port the 32-bit LCG and modulo exactly in `objects.c`; keep the state in
  `cc_state_t` and include it in startup data and save state.
- Port the full `0x5855` handler, especially `0x5960`: `Random(100) < 5`
  respawns the previous slot (`shape`, `ready=0`, `state=0`, copied x/y)
  and plays sound `0x10`; the non-hit path must leave RNG state unchanged
  except for the draw itself.
- Add hit-seed and miss-seed fixtures; compare the full slot group and RNG
  state after every frame.

### P2.2 — Fire, slash update, and frame integration

- Port `0xCF6C` exactly: ammo/cooldown/hit-lock gates, normal and power
  variants, facing/left-right shapes, slot-2 field writes, `[0x2CA]=[0x2DA]`,
  `[0x2E0]=0`, `[0x30A]=1`, and the sound IDs `0x0E`/`0x0F`.
- Port `0x251D` (`0x21EBD..0x221ED`): `p18++`, `anim_next`, the two terrain
  cell tests at `x+9`, `y`/`y+0xF`, kind `0x2B`/handler `0x2241` conversion,
  cell clears/`w0=0xFFA1`, and the no-terrain lifetime path.
- Port `0x2850` as the ready gate around `0x251D`, and `0x2241` as the
  dug-block animator. Fix `step_objects()` so kind `0x2B` takes the same
  direct `0x251D` path as kind 2.
- Port the `0xEBF2` fire/timer prefix only: cooldown increment/clamp,
  power-shot expiry, fire dispatch at `0xEE99..0xEEAE`, and the reset
  writes. Explicitly mark the spawner/exit branches unreachable for Phase 2
  fixtures and finish them in Phase 3.
- Port `0xEAEA` now: the invulnerability/status timer section is required
  for repeated damage fixtures. `0xE5D3` hazard contact remains deferred.
- Wire a full normal-frame entry point (physics, fire/timers, object loop)
  and use it from `play.html`; update `test_objects`/`test_gameplay` if the
  richer frame changes their expectations.
- Fixtures: normal/power fire, left/right, ammo exhausted, cooldown active,
  hit lock active, held fire, terrain hit/miss, slash expiry; compare slot 2,
  ammo, cooldown, sounds, and cells each frame.

### P2.3 — `0x20406` first-hit and transform blocks

- Restore the exact control flow: first block -> `0x20707`; transform block
  when `[0x513C]==0` -> `0x20707`; slash gate only skipped when
  `[0x4660]!=0` or no rect overlap.
- Verify every first-block kind case: `0x10/0x0E` death, `0x24` heal/ammo,
  `0x16` gravity-dependent state 1/3, `0x2C` score/effect, default damage;
  match `damage_life` (`0x203D6`), `[0x509E]`, `[0x509C]`, `[0x460D]`,
  `[0x5124]`, `[0xA0EA]`, sounds, and spawns.
- Complete the transform list (kinds `9,7,0xA,0x39,0x1A,5,0x35,0x30,0x17,
  0x23,0x1C,8,0xB,0xC`) and port `0x2877` so transformed objects dissolve
  correctly over the following frames.
- Replace the hand-picked `test_interaction.js` assertions with oracle
  fixtures covering each branch.

### P2.4 — `0x20406` slash state 1 (normal)

- Port the full `0x20824..0x21335` matrix mechanically:
  - rect test against slot 2, `[0x4660]` lock, kind-`0x24` special handling
    (`g_5124=2`, p10/p16 reset), object `p04--`;
  - clear the hit lock for kinds `0x15,0x1D,0x12,0x37,0x19`;
  - per-kind kill/survival paths and score adds (`+0x64`, `+0x3E8`,
    `+0x1388`) via `[0x50A5]/[0x50A7]` add/adc;
  - cell writes at `+0x00/+0x06/+0x0A/+0x0C/+0x0E`, neighbor-slot/free
    writes, `[0x512C]` counter, sounds;
  - helper calls `0x2002D`, `0x200FE`, `0x20247`, `0x1FC73`, `0x1FBBF`,
    `0x1FCB0`, `0x1FCF7` with exact argument order.
- Fixtures: one scenario per kind group (`0x1A/0x23/0x17/0x0C/0x32/0x36/
  0x3A/0x31`, `0x2F`, `0x09`, `0x05`, `0x30`, `0x39`, `0x35`, `0x33`,
  `0x2C`, and the no-kill/`p04>=1` path), plus hit-lock clear checks.

### P2.5 — `0x20406` slash state 2 (power)

- Port `0x21342..0x21775`: excluded-kind early return (`0x16,0x0E,0x24,
  0x1D,0x15,0x10,0x37,0x12,0x19,4`), `0x2C` effect/spawn, `0x35`,
  `0x2F`, `0x33`, `0x30`, `0x39`, score/cell/sound writes, and the same
  helper calls.
- Fixtures: power slash vs each handled kind, no-ammo power shot, power
  expiry, and a mixed normal/power sequence.

### P2.6 — Death, life loss, and restart paths

Needed so "take damage and restore lives" is actually playable rather than
leaving `[0x5124]` stuck.

- Port `0xCF46` (restore checkpoint ammo/score/health), `0xD15C` (reload
  current level via the loader-equivalent), `0xD140` (fade/lives latch).
- Port the `[0x5124]` handlers selected by `0xF4E6`: `0xD17D` (state 1,
  wait for fire/key then restart), `0xD1C6` (state 2, spawn
  `0x1F9A:0x6681`, timed sequence), `0xD342` (state 3, timed sequence).
  `0x6681` (cell-erase effect) must be ported here rather than deferred to
  Phase 4.
- Fixtures: lethal hit, non-lethal hit, hit while invulnerable, death ->
  restart, and compare the post-restart object/cell/global state.

### P2.7 — Spawn/effect helpers used by combat

- Port the exact state side of `0x47D` (14-word far args): field copy,
  `[0x5120]` cursor advance/wrap, state `0x63`, base-cell stamp
  (`0x59`/`[0x2B8]`/`[0x5122]`), and caller-visible state transitions.
  Drawing may remain a renderer concern, but every cell/object write must
  be preserved.
- Port `0x1FFC8` free-slot reuse, `0x2002D` slot-3-template effect spawn,
  and `0x200FE` cell test/damage helper. Add a one-spawn oracle test before
  relying on them in the slash fixtures.

### P2.8 — Regression, fixtures, and docs

- Commit `notes/combat_fixtures.json` with the full matrix:
  - touch: default, `0x10/0x0E`, `0x24`, `0x16` (both gravities), `0x2C`,
    invulnerable, already dead;
  - transform: all 14 kinds plus a non-listed kind and dissolve frames;
  - fire/slash: normal/power, directions, ammo/cooldown/hit-lock, terrain
    hit/miss, expiry;
  - slash state 1 and state 2 kind matrices;
  - RNG `0x5855` hit/miss;
  - death/restart;
  - long full-frame level-2 and level-3 runs (>=120 frames) with movement
    and fire.
- Add `make test-combat` (committed fixture) and `make test-oracle-combat`
  (regenerate), then add both to `test-all`. Keep `test-oracle-objects`
  passing with the richer comparison. `0x20406` is called from 40 handler
  sites, so rerun `test-oracle-objects` after every matrix batch.
- Update `notes/gameplay.md` and `AGENTS.md` with the corrected fire path,
  slot-2 record map, RNG, cell offsets, and the new test commands.

Acceptance:

- C `0x20406` matches the oracle for every touch, transform, normal-slash,
  and power-slash fixture; object slots 2/3, all cell bytes, score low/high,
  ammo, health/lives, invuln, hit lock, cooldown, RNG, cursor, and sound IDs
  all match frame-for-frame.
- Player can collect, take damage, lose/restore lives, restart after death,
  and fire both slash types correctly.
- `0x5855` companion respawn is deterministic with a seeded RNG.
- No stubs remain on active combat paths (`0x251D`, `0x2850`, `0x2877`,
  `0x2241`, `0x2002D`, `0x200FE`, spawn/free).
- Existing render, physics, object, and gameplay regressions still pass.

Risks / decisions:

- `0xEBF2` also contains item spawner/exit branches. Port only the
  fire/timer prefix now; Phase 2 fixtures must either stay below the
  spawner threshold or assert those branches unreachable. Finish the rest in
  Phase 3 rather than guessing.
- `0xE5D3` hazard contact is deferred. Combat scenarios avoid hazard cells
  and verify the routine is state-inert for them; otherwise port it early.
- Cell offsets and the `+1` border indexing are easy to get wrong. Port
  them through a single `cell_record(col,row)` accessor and prove it against
  the oracle before porting the matrices.
- `0x47D` drawing/base-cell stamping has many call sites. Validate the state
  side with one known spawn (slot, cursor, cells) before using it broadly.
- The current `score` field and `[0x50A5]/[0x50A7]` are duplicated. Replace
  the former with exact low/high fields and carry-aware adds.
- `test_gameplay.js` currently asserts score/collectibles stay zero. Update
  those assertions only when the corresponding real rule lands; do not add
  temporary fake counters.

Suggested order: P2.0 -> P2.1 -> P2.2 -> P2.3 -> P2.7 -> P2.4 -> P2.5 ->
P2.6 -> P2.8. P2.7 must land before P2.4 because the slash matrices call
`0x2002D`/`0x200FE`, and before P2.6 because `0xD1C6` spawns `0x6681` through
the general spawn path. Commit after P2.0/P2.2/P2.3/P2.5/P2.8 so each step has
a working oracle fixture.

## Phase 3 — Cells, crystals, exits, hub doors (COMPLETE)

Status: COMPLETE for the active gameplay path.

Implemented:

- `0xE5D3` player contact: five-point `0xFC5:BB26` calls, `0xFF9F`/`0xFF9D`
  hazards, crystal pickup spawns (`0x672B`/`0x69D3`) and player pose changes.
- `0x0FC5:0xBB26` cell-event handler: crystal cells (`w6=0xD`, `w7=1..4`),
  ammo/power-up/score cells, switches/levers, hazard cells, hub doors and cave
  exits.  Crystal pickup decrements `0xA0CC`, increments `0x50A2`, adds score
  and plays sounds 8/0x20.
- `0xEBF2` collectible spawn/expiry (`0xA0CE`/`0xA10B`/`0xA0D0`/`0xA0D2`) and
  all-crystals exit unlock (`0xA0CC==0` -> `0xA0F5=1`, clear exit solid).
- `0xD4E5` hub-door entry and `0xD360`/`0xD412` cave exit: door/exit cell
  animation, spawn override `[0x2B72]/[0x2B74]`, cave-completion flags
  `[0x50A9..0x50B8]`, all-caves check, hub load.
- Level-argument mapping: `0`=hub, `1..16`=caves, `0xFFFF`=intro,
  `0xFFFE`=finale (`cc_load_arg`, `cc_level_exit`).
- Dynamic cell rendering from the mutable 20-byte records:
  `cc_render_cells` / `cc_render_view_cells`; layer mapping `(w4,w2)`,
  `(w5,w3)`, `(w6,w7)`.
- New tests: `notes/cell_fixtures.json`, `make test-cells`,
  `make test-oracle-cells`, functional `test_levels.js`, and
  `test_cells_render.js`.
- Handlers `0x6681`, `0x672B`, `0x69D3` moved from Phase 4 into Phase 3 and
  implemented.

Transitions:

- The original loader overlays cells/objects onto the source level's memory.
  Phase 4 now replays the loader occupancy overlay exactly in both directions
  (hub->cave and cave->hub), including preserved source bytes, source-dependent
  shapes, RNG-selected fields, hub decorations and flag-driven door cells.

## Phase 4 — Remaining handlers and all 16 caves (COMPLETE)

Status: COMPLETE. All 46 handler-table entries are ported (plus dynamic
`0x4EEB`), `make test-handlers` passes 235 scenarios / 376 frames, and
`make test-caves` passes all 16 caves (370 frames).  Transitions replay the
exact loader occupancy overlay in both directions (`make test-loader`, 32
scenarios), including the hub's RNG-driven decorations and flag-driven doors.

Historical initial handler worklist (25 entries; all now ported):

```text
298A 2ACF 2BD4 2D37 2E25 2F83 32F6 3416 36B0 3904 3A2E 3F6C 403A
4145 47B9 48FE 4AD3 4C92 4FD4 52E7 55EC 5C8F 6232 62F2 6458
```

Order:

1. High-frequency shared handlers: `4145`, `3904`, `47B9`, `48FE`, `2D37`,
   `3416`, `32F6`, `2ACF`, `2BD4`, `298A`, `403A`, `55EC`, `5C8F`, `62F2`,
   `6232`, `4C92`, `52E7`, `2F83`, `4AD3`, `36B0`.
2. Dynamic/spawn-only handlers: `3F6C`.
3. Per-cave verification after each batch.

Tasks:

- Port each handler from `notes/handlers_disasm.txt` with exact field writes.
- Add it to `handler_for()` and `is_real_handler()`.
- Add the level/cave oracle fixture that exercises it.
- Port remaining shared helpers used by handlers:
  `0x1FCF7`, `0x1FBBF`, `0x0A66`, `0x075E`, `0x02D3`, `0x1E74`, `0x1F58`,
  `0x08A7`, `0x251D`, `0x0043`, `0x2BB5` math/RNG helpers.
- Implement dynamic spawn/free and map-cell mutation where handlers require it.

Acceptance:

- 46/46 handlers have real implementations.
- All 16 caves pass object/cell oracle fixtures.
- No inert handler fallback remains for active objects.

## Phase 5 — HUD, text, and game shell (IMPLEMENTED, polish remaining)

Status: core implementation committed as `4665c32`.  The title/credits/title
flow, main menu, PCX screens, text windows, oracle-exact HUD, story/finale
transitions, help/pause/quit/warp, save/restore 0–9, high scores and config
persistence are implemented.  `notes/hud_fixtures.json`, `notes/text_fixtures.json`,
`make test-hud`, `make test-text`, `make test-messages` and `make test-shell`
pass; `make test-oracle-shell` reproduces both the HUD and text-window fixtures.

Completed Phase 5 polish:

- text-window frame/fill/text are oracle-exact (23 fixtures, including 14 real
  message wrappers, menus/story/instructions, and camera/scroll variants);
- the remaining in-game message triggers are wired (red/poison mushroom,
  stunned, perfect health, more crystals, lever, air generator, saved/errors);
- the HUD power timers tick once per game step (`cc_hud_tick()`) instead of
  once per rendered frame, with a cadence regression test;
- cave→hub transitions replay the destination-2 overlay, including the hub's
  two RNG-driven background decoration recipes and flag-driven door cells;
- manual browser smoke testing is complete; it caught and fixed object-frame
  selection (`shape + p10 - 1`) and debug-warp source-cell leakage, both now
  covered by regressions.

Remaining Phase 5 polish (see the checklist at the end of this phase):

- add automated headless browser smoke coverage (Phase 8);
- title/credits scroll is approximated by the original 432-tick duration.

Goal: complete the non-gameplay shell around the already-exact gameplay core:
status HUD, text windows, PCX screens, title/menu flow, story/finale
transitions, help/config screens, save/restore, high scores, and persistent
browser settings.

### Verified starting points

- HUD is `0x0FC5:0xD1E0` (linear `0x1CE30`), called from the main loop at
  linear `0xFA29` when `[0xA0FE] != 0`.
- HUD draws an 8-pixel status row at **`y = 1`** using the glyph blitter
  `0x28B5:0x1A4D`; x positions include `[0x512E] & 7` (camera sub-pixel).
- HUD uses `DS:0x337C` (F1), `DS:0x3380` (F2) and `DS:0x3384` (SPL).
  Score icon = F1 glyph 14 (offset `0x233`); ammo icon = SPL glyph 0;
  hearts = SPL glyph 4 (offset `0xA3`); status icon = SPL glyph 5 (offset
  `0xCB`); power `*` = F1 glyph 20 (offset `0x323`); digits use SPL.
- HUD counters: `[0x509E]` health/lives, `[0x509C]` ammo, `[0x50A2]`
  crystals, `[0x50A5]/[0x50A7]` score, `[0xA0C4]/[0xA0C6]` power-shot timer,
  `[0xA0F9]` status icon, `[0xA0FB]` all-caves flag.
- Text renderer is at linear `0x10314`; window line buffer is `DS:0x163C`
  with stride `0x100`; glyph blit is `0x28B5:0x445`; font selection is by
  character code (F1 for `0x17..0x48`, F2 for `0x49..0x7A`, SPL for digits).
  Lowercase ASCII glyphs are shifted by -6 before indexing (recovered while
  matching the original menu text).
- MNI files are 50 glyphs of 40 bytes after a 3-byte header; glyph = 8 rows ×
  5 bytes `[mask,p0..p3]`; the remaining 45 bytes are unused.
- Main menu is at linear `0x17564`; its text block starts at linear `0x1745D`;
  option dispatch is by **scan code**, not ASCII: N/Space/Enter = new game,
  R = restore, S = story, I = instructions, O = ordering, H = high scores,
  F = foreign orders, C = BBS, V = video, A = about, Q/Esc = quit.
- Title routine is at linear `0x17F20`; it loads `cc1.ttl` through
  `0x1D6E:0x1915` and calls the main menu.
- PCX screens are 320×200, 4-plane, 40 bytes/line, RLE; header palettes are
  EGA AA/AB variants.
- `cc_objects_step_full()` now calls the shell finale hook for `[0xA0FB]`;
  last-crystal, gravity and power-shot events raise shell messages.  The
  `[0xA126]` gameplay-message path still has no dedicated trigger.
- `wasm/game.html` is the full shell page; `wasm/play.html` remains a dev
  level-select harness.

### Locked decisions

- HUD is **oracle-exact** (`notes/hud_fixtures.json`).  Text windows use the
  original fonts, text blocks and layout; their frame/fill styling is targeted
  for a per-pixel oracle diff in the remaining polish work.
- PCX screens are displayed from build-time decoded indexed pixels; use the
  standard EGA palette for consistency with the rest of the port.
- Shell state lives in C for determinism/testability; JS handles canvas,
  keyboard/gamepad, DOM, localStorage, and audio enable/disable.
- DOS-only features are browser substitutions: original screens/text are
  preserved, but no DOS file I/O, BBS dialing, or ordering network calls.
- Phase 5 ships a minimal versioned save/config/high-score layer; Phase 7
  hardens/migrates it.  Do not create two formats.
- Do not port the original shell control flow literally.  The original uses
  overlapping TP segments (CS bases `0x0FC5`/`0x1000`) and overlaid text
  buffers; recover observable behavior through an oracle and implement a
  clean state machine.

### P5.0 — Shell oracle, text/window fixtures, and asset export (PARTIAL)

Status: HUD oracle, text-window oracle and asset export are done.  The full
INT 21h shell oracle (menu flow, file I/O, keyboard script) is not built yet;
the text-window capture now covers the real renderer and message wrappers.

- Add `tools/oracle_shell.py`:
  - Reuse the `emulate_startup`/`emulate_load` machine and the EGA plane
    hooks from `emulate_render.py`.
  - Add a minimal INT 21h file shim (`open`/`read`/`close`/`seek`) mapping
    `CC1.*` to extracted files so the real PCX/MNI loaders can run.
  - Add scripted INT 16h keyboard input and INT 10h video-mode/palette hooks.
  - Capture title/Apogee/credits/ending screens, the HUD status line for
    seeded counters, text windows for the main menu/instructions/story/help/
    high scores/pause/errors, and menu flow after scripted keys.
  - Dump window buffers (`DS:0x163C`), fonts, palette, and framebuffers.
- Add `tools/export_shell_assets.py`:
  - Decode PCX RLE to `out/portable/screens.bin` (indexed 320×200) plus
    `screens.json`.
  - Export `mni_f1.bin`, `mni_f2.bin`, `mni_spl.bin` (document the 3-byte
    header and 45 trailing bytes).
  - Extract every length-prefixed text block with an ID and line array (main
    menu, help, instructions, story pages, ordering, foreign, BBS, about,
    video, high scores, pause, quit, warp, save/restore, key redefine,
    errors).  Confirm block starts with a scanner using the
    `END OF WINDOW` sentinel.
  - Export the scan-code → key-name table, menu key mappings, high-score
    defaults, and config defaults.
  - Add all assets to `out/portable/manifest.json` and `make data`.
- Add `wasm/test_shell.js` and committed `notes/hud_fixtures.json`; add
  `make test-shell` / `make test-oracle-shell`.  A text-window fixture is the
  P5.R1 follow-up.
- Extend the loader overlay/replay to level 2 if needed; the Phase 4
  hub-decoration caveat must not leak into title→new-game or cave→hub
  acceptance.

Exit: assets round-trip; a trivial HUD fixture matches the oracle; PCX decode
matches; the test harness runs.

### P5.1 — Font/text primitives and HUD (DONE)

- Add `wasm/text.c` / `wasm/text.h`:
  - `cc_fonts_set(f1, f2, spl)`, `cc_glyph_ptr(bank,index)`,
    `cc_draw_glyph(x,y,bank,index)`, `cc_draw_text_line`.
  - Reproduce TP integer formatting (`0x2BB5:0xB26`) exactly: width, space
    padding, truncation, low-word wrap.
  - Reproduce the character→font mapping and special glyphs/icons.
- Add `cc_hud_render()` as a direct port of `0x0FC5:0xD1E0`:
  - score width 7, ammo width 3, crystals width 4, health width 1,
  - hearts, status icon, power-shot `*` plus 2 digits,
  - `[0x512E]&7` sub-pixel x offset,
  - `y=1` status row and exact draw order.
- Add `[0xA0FE]` (HUD active) to `cc_state_t`/startup data; call HUD after
  map/objects render, before presenting.
- Draw HUD into `g_view` in `render.c`; keep `cc_render_view_cells` and
  object drawing unchanged.
- Add `wasm/test_hud.js` with oracle fixtures:
  - health `0..3`, `9/10/99`,
  - score `0/9/10/9999999`, low-word wrap,
  - ammo `0..999`, crystals `0..9999`,
  - power timer inactive/`0..17`, `*` glyphs,
  - `[0xA0F9]` and `[0xA0FB]` on/off,
  - camera sub-pixel `0..7`.

Acceptance: HUD is pixel-exact for every fixture and updates during play.

### P5.2 — Text-window engine and static screens (DONE; oracle-exact)

- Port the text renderer at linear `0x10314` / glyph blit `0x28B5:0x445` and
  the window renderer at `0x0FC5:0x0877`, or implement equivalents with
  identical geometry and glyph mapping.
- Implement the original window flow:
  - line buffer at `DS:0x163C`, stride `0x100`,
  - centered/positioned line drawing, border/background colors, `[0x2B5C]`
    scroll offset,
  - press-any-key prompts and page advance,
  - `END OF WINDOW` sentinel.
- Add `cc_text_window(id)` and `cc_text_page(id, page)`.
- Add `cc_draw_screen(screen_id)` for APG/TTL/CDT/END.
- Add the in-game message dispatcher for gameplay flags (`[0xA126]`,
  `[0xA0F5]`, `[0xA10D]`, power-up/switch/lever/mushroom/air-generator
  messages) rather than only static menus.
- Add pixel tests for representative windows: main menu, instructions page 1,
  story page 1, help, high scores, pause, a save/restore error, and each
  in-game message type.

Acceptance: page navigation and press-any-key behavior are correct.  Text
frame/fill/text pixel parity is validated by `notes/text_fixtures.json`
(23 cases, 0 differing pixels; `make test-text`).

### P5.3 — Screens, title, main menu, and story/finale flow (DONE; timing/order verification pending)

- Add `wasm/shell.c` / `wasm/shell.h`:
  - states: `LOGO`, `TITLE`, `MAIN_MENU`, `SUBMENU`, `STORY_INTRO`, `GAME`,
    `STORY_FINALE`, `ENDING`, `HIGH_SCORE_ENTRY`, `HELP`, `PAUSE`,
    `QUIT_MENU`, `SAVE`, `RESTORE`, `KEY_REDEFINE`, `WARP`.
  - `cc_shell_init`, `cc_shell_step`, `cc_shell_key`, `cc_shell_screen`.
  - Route keys by state; never leak menu keys into gameplay.
- Implement the title sequence and menu options using extracted text blocks:
  - Apogee logo → title → main menu (with original timer/key behavior),
  - N/Space/Enter new game, R restore, S story, I instructions, O ordering,
    H high scores, F foreign orders, C BBS, V video, A about, Q/Esc quit,
  - all submenus use original text and press-any-key flow.
- Implement story transitions:
  - new game → intro level arg `0xFFFF` story pages → hub,
  - all 16 caves complete → finale arg `0xFFFE` story pages → `CC1.END` →
    high scores/title.
  - Replace the `[0xA0FB]` and `[0xA126]` stubs in `cc_objects_step_full()`
    with shell transition hooks.
- Tests: `test_shell_flow.js` with scripted keys: title → new game → intro →
  hub; every menu option reachable; all caves → finale → ending → high
  scores; static screens pixel-compared.

Acceptance: title→game→finale flow works and all menus are reachable.  Exact
story page order/timing is verified by P5.R5.

### P5.4 — In-game help, pause, quit, warp, input config (DONE; DOS scan-code parity open)

- Implement the help menu (`0x17866` block) and actions:
  - instructions, sound toggle, joystick toggle, joystick calibration,
    video support, key redefine, restore old game, save current game,
    quit current game, Esc return.
- Implement pause (`Game paused`), quit submenu (`D)os`, `T)itle`,
  `M)ain Level`), and warp 1–16 (decode the original cheat trigger; preserve
  the screen).
- Key redefine screen using original labels and scan-code table; browser key
  capture; persist in config.
- Browser substitutions:
  - DOS quit → return to title / informational message,
  - BBS/foreign/ordering → original text screens with "not available in
    browser" note; no external calls,
  - video toggle → stored no-op,
  - joystick → optional Gamepad API mapping; calibration screen preserved or
    auto-calibrated.
- Tests: help navigation, pause/resume, quit-to-title/main-level, warp, key
  redefine round-trip, config reload.

Acceptance: every help option is reachable and behaves; input/config
persists.

### P5.5 — Save/restore, high scores, config persistence (DONE; Phase 7 hardening pending)

- Define a versioned `cc_save_state` v1 schema:
  - header/magic/version, level arg, player/camera, object array, full
    20-byte cells, gameplay globals, RNG state, cave flags, score/lives/ammo/
    crystals, checkpoint, story/progress flags.
  - metadata (timestamp, slot) outside the deterministic snapshot.
- Add C serialize/deserialize APIs and a JS `localStorage` adapter:
  - slots 0–9,
  - original hub-only save/restore restriction and messages,
  - empty/corrupt slot error text,
  - save full cells/objects so restore is pixel-exact without regenerating
    hub decorations.
- High scores:
  - decode the 10-entry table/name-field/sort/tie rules from the high-score
    routine (linear `0x13CC8`),
  - defaults from the binary (`I. Jones`, `B. Blaze`, `S. Logan`, `D. Nukem`,
    `J. Kirk`),
  - top-ten check on finale, name entry using the text engine, persist.
- Config:
  - sound on/off, joystick on/off, video alternate, key bindings; defaults
    from the original instructions (arrows, Ctrl jump, Alt fire/action);
    persist separately.
- Tests: hub save/restore round-trip byte-exact in memory; high-score
  insert/sort/persist across reload; config round-trip; corrupt-slot
  handling.

Acceptance: save/restore reproduces identical state; high scores/config
survive reload.  Phase 7 owns schema hardening/migration; Phase 5 must ship
the single v1 implementation.

### P5.6 — Integration, docs, regression (DONE)

- Add a real shell page (`wasm/game.html` or upgrade `play.html`); keep a dev
  level-select page optional.
- Canvas stays 320×200; input routing by shell state; localStorage adapter;
  sound enable hook for Phase 6.
- Add `make test-shell`, `make test-hud`, `make test-save`, `make test-hiscore`;
  add them to `test-all`.
- Update `AGENTS.md`, `plan.md`, and add `notes/shell.md` with routine maps,
  asset formats, and test commands.
- Remove temporary diagnostics/stub assertions.

Acceptance: `make test-all` passes; title→finale flow works; no shell stubs
remain.

### Risks and mitigations

- **Overlapping TP segments / overlaid buffers:** do not trust linear
  addresses for shell control flow; use oracle captures and reimplement
  state/layout.  Reference routines by linear file offset plus segment:offset
  where known.
- **MNI export formatting:** F1/F2/SPL are exported as 50 × 40 B after the
  3-byte header; the 45 trailing bytes are unused.  The lowercase -6 shift is
  required for the original glyph order.
- **PCX palette AA vs AB:** display uses the standard EGA palette for
  consistency with the hardware renderer; header palettes remain metadata.
- **HUD position:** verified at `y=1`; the power-timer cadence still needs to
  be tied to game frames rather than render calls.
- **Save/restore and hub decorations:** full mutable cells/objects are saved,
  which avoids depending on the unresolved hub RNG decoration replay.
- **Text engine complexity:** only the used screen types are rendered; the
  window frame/fill still needs an oracle diff.
- **Story/finale transitions:** the shell callbacks are implemented; page
  order and timing still need an oracle check.
- **Browser input conflicts:** preventDefault, a browser-key redefine map and
  Gamepad API support are implemented.  DOS scan-code parity remains open.
- **Scope overlap with Phase 7:** Phase 5 ships the v1 save/config/high-score
  format; Phase 7 hardens/migrates it.

### Suggested order and commits

Completed in commit `4665c32`:

1. P5.0 — assets, HUD oracle, test skeleton (full shell oracle still open).
2. P5.1 — fonts/text primitives + HUD.
3. P5.2 — text windows + static screens/messages.
4. P5.3 — title/menu/story flow.
5. P5.4 — help/pause/quit/warp/input config.
6. P5.5 — save/restore/high scores/config.
7. P5.6 — integration/docs/regression.

### Validation commands

```sh
cd wasm
make test-shell
make test-oracle-shell
make test-hud
make test-save
make test-hiscore
make test-all
```

### Remaining Phase 5 work (ordered)

- **P5.R1 — Text-window oracle (DONE).**  `tools/oracle_shell.py` captures
  the real renderer at `0x0FC5:0x0877` from 14 message wrappers plus
  menu/story/instructions/camera/scroll cases into `notes/text_fixtures.json`.
  `wasm/test_text.js` compares `cc_text_draw_block()` per pixel; all 23 cases
  pass with 0 differing pixels.
- **P5.R2 — Message triggers (DONE).**  Red/poison mushroom, stunned, perfect
  health, more crystals, lever, air generator, saved/error and the existing
  last-crystal/gravity/power-shot triggers are wired; `make test-messages`
  covers positive and negative causes.
- **P5.R3 — HUD cadence (DONE).**  `cc_hud_tick()` advances
  `[0xA0C4]/[0xA0C6]` once per game step; `cc_hud_render()` is pure.
  `test_hud.js` checks 18 ticks with extra renders ignored.
- **P5.R4 — Browser smoke test (DONE, manual).**  Exercised `wasm/game.html`
  in a real browser through title→new game→hub, HUD, help/pause/warp and the
  save/restore/high-score/config flows.  The pass found two rendering bugs:
  object animation frames were not selected (`shape + p10 - 1`) and debug warp
  reused transition overlays from the source cave; both are fixed and covered
  by `test_shell.js`.  Headless Playwright coverage remains Phase 8 work.
- **P5.R5 — Exactness verification (DONE).**  Added
  `tools/shell_exactness.py`, `notes/shell_exactness_fixtures.json`,
  `notes/hiscore_fixtures.json`, `notes/keymap_fixtures.json`, and
  `tools/make_shell_exact_data.py`.  Screen timing now advances in 140.06 Hz
  PIT ticks (APG 720; credits+title combined 432); intro/finale pages are captured from
  the original wrappers; high scores use the original ten-row defaults,
  qualification/tie rules and 20-character name entry; key redefine uses DOS
  set-1 scan codes and the original labels/confirmation flow.  Browser
  substitutions are documented in `notes/shell.md`.  Validation:
  `make test-exactness`, `make test-keymap`, `make test-oracle-exactness`.
- **P5.R6 — Full shell oracle (optional supporting work).**  Add the INT 21h
  file shim and scripted INT 16h keys to run the original title/menu flow for
  end-to-end screen/state comparison.

## Phase 6 — Sound (IMPLEMENTED)

Status: implemented.  The real driver is captured, all 36 banks are traced
tick-for-tick, and the browser synthesizes the trace at load time.

Recovered model:

- `play_sound_or_effect` is `0x1F9A:0x021F` (linear `0x1FBBF`); bank selection
  `(id-1)/12+1`, slot `(id-1)%12+1`; far pointers at `DS:0x510C/0x5110/0x5114`.
- `CC1-1.SND`..`CC1-3.SND` are 12 × 610-byte records.  Notes are `uint16` Hz
  terminated by `0xFFFF`; `+600` priority; `+604` gate period.
- INT 1Ch ISR `0x0A3F`; PIT channel 0 divisor `0x2147` = 140.061 Hz;
  `0x287F6` sets `divisor = 1193181/Hz` (≤18 is ignored); `0x28823` silences;
  `0xFB33` plays ID 26 and waits for `[0x513F]`.
- Exact priority rule: new priority `>=` current preempts; lower is ignored.

Implemented:

- `tools/sound_banks.py` / `tools/sound_fixtures.py` — model + Unicorn oracle.
  All 36 traces match; selection matrix matches.
- `wasm/sound.c/h` — exact enable/priority/preemption/tick/event state.
- `tools/export_sound.py` — `out/portable/sound.json` +
  `wasm/sound_data.h` + synth hashes.
- `wasm/sound_synth.js` — deterministic integer-phase square-wave renderer.
- `wasm/audio.js` — single-channel WebAudio manager, autoplay unlock, toggle.
- `wasm/game.html` — 140.06 Hz sound tick accumulator, event polling, stop on
  transitions/restore/quit, play through pause/help.
- Shell effects: menu 33, page 35, high score 34, ending 32, level start 36,
  and the ID-26 all-caves fanfare/wait.
- Fixtures/tests: `notes/sound_fixtures.json`,
  `notes/sound_synth_fixtures.json`, `notes/sound_shell_fixtures.json`,
  `make test-sound`, `make test-oracle-sound`.

Acceptance: met for the 36 banked effects and the shell integration.
Remaining coverage audit: IDs 2 and 7 have no C gameplay trigger (ID 7's
static site is `0xF74C/0xF757`); the full title/menu shell sequence is
statically mapped rather than captured end-to-end.

## Phase 7 — Save/high-score/config hardening

Phase 5 ships the first working save/config/high-score implementation and the
v1 schema.  This phase hardens it; it must not introduce a second format.

Tasks:

- Harden/version the Phase 5 save-state schema:
  - globals, player, object array, mutable cells, level progress, crystals,
    score, lives, flags.
- Add migration/validation for version changes and corrupt slots.
- Verify save/restore mid-level and mid-hub round-trips exactly.
- Harden high-score table/name entry and config persistence; verify the
  original default table, sort/tie rules and CC1.HS semantics.
- Keep the browser “not available” treatment for DOS-only file/BBS actions
  consistent with the Phase 5 shells.

Acceptance:

- Mid-level save/restore reproduces identical state.
- High scores survive reload.
- Key bindings/config survive reload.

## Phase 8 — Full regression and release

Tasks:

- Add permanent oracle fixtures:
  - all 19 static renders,
  - hub/level object+cell frames,
  - player physics,
  - interaction/combat,
  - hub door flow/exit,
  - menus/save/high scores,
  - text-window frames, message triggers and HUD cadence.
- Add sound-ID/timing fixtures (Phase 6).
- Make `make test-all` cover the full deterministic suite.
- Add CI workflow (build, data checks, render diff, gameplay fixtures, browser
  smoke test).
- Optimize wasm size/memory and frame budget.
- Refresh `AGENTS.md`, `notes/*`, and add a README with build/run instructions.
- Remove stale stub assertions/tests and superseded tools.

Acceptance:

- Clean checkout builds and passes the full suite.
- Playable from title to finale with no known gameplay stubs.
- Regression suite fails on any oracle divergence.

## Immediate next actions

1. Phase 6 follow-up: audit the remaining ID 2/7 gameplay trigger sites and
   add end-to-end title/menu sound capture if desired.
2. Phase 7: harden the v1 save/config/high-score schema and verify round-trips.
3. Phase 8: add Playwright browser smoke coverage, CI, release regression,
   performance and docs.

## Validation commands

```sh
cd wasm
make test-all
make test-physics            # committed physics fixtures (fast)
make test-oracle-physics     # regenerate physics fixtures (Unicorn)
make test-oracle-objects     # requires tools/requirements.txt + .venv
make test-combat             # committed combat fixtures (fast)
make test-handlers           # committed per-handler fixtures (fast)
make test-loader             # exact transition fixtures (hub<->caves)
make test-caves              # committed all-cave full-frame fixtures
make test-cells              # committed cell/contact/exit fixtures
make test-hud                # oracle-exact HUD fixtures
make test-text               # oracle-exact text-window fixtures
make test-messages           # in-game message trigger causes
make test-shell              # shell flow/save/high-score/finale fixtures
make test-exactness          # P5.R5 timing/story/high-score/scan-code fixtures
make test-keymap             # browser key -> DOS scan-code mapping
make test-sound              # sound driver/trace/JS synth/shell integration
make test-oracle-sound       # regenerate sound traces (Unicorn) and compare
make test-oracle-exactness   # regenerate shell exactness fixtures/header
make test-oracle-shell       # regenerate HUD + text fixtures
make test-oracle-handlers    # regenerate handler fixtures (Unicorn)
make test-oracle-loader      # regenerate transition fixtures (Unicorn)
make test-oracle-caves       # regenerate all-cave fixtures (Unicorn)
make test-oracle-cells       # regenerate cell fixtures
make startup                 # regenerate notes/startup.json + startup_data.h
```

Current passing checks:

```text
ALL 19 LEVELS PIXEL-EXACT
PASS: 15 viewport crops match the full level buffer
PASS: object init/input/collision checks
PASS: level 2 fixture checks
PASS: 0x20406 pickup interaction
PASS: 10 frames match oracle object state (level 2)
PASS: 120 frames match oracle object state (level 3)
PASS: 16 physics scenarios, 670 frames match oracle
PASS: 74 combat scenarios, 624 frames match oracle
PASS: 235 handler scenarios, 376 frames match oracle
PASS: 32 loader transitions match oracle (hub<->caves)
PASS: 16 all-cave scenarios, 370 frames match oracle
PASS: 17 cell scenarios, 20 frames match oracle
PASS: hub door and cave exit transitions load the correct levels
PASS: mutable cell renderer matches baked; crystal clear changed 150 pixels
PASS: HUD matches the real routine (4 boundary cases)
PASS: text windows match the real renderer (23 fixtures, 0 px)
PASS: shell flow, story/finale, save/restore, high scores
PASS: P5.R5 tick timing, captured intro/finale pages, high-score defaults/tie
PASS: high-score screen matches the original framebuffer
PASS: DOS scan-code defaults, key redefine, and browser key map
PASS: 36 sound traces, driver priority/preemption, JS synth hashes
PASS: shell sounds and ID-26 finale wait
PASS: debug warp uses fresh destination cells and preserves score
PASS: player walk animation advances through shapes 1..12
PASS: HUD/text fixtures regenerate via make test-oracle-shell
PASS: shell exactness fixtures regenerate via make test-oracle-exactness
```

## Relevant files

- `wasm/objects.c` — update loop, handlers, interaction, exact physics.
- `wasm/objects.h` — public gameplay API + physics debug fields.
- `wasm/startup_data.h` — generated startup/table/level-start data.
- `notes/startup.json` — exact `0xC3E` + loader state dump.
- `notes/physics_fixtures.json` — committed physics oracle trace.
- `notes/physics.md` — recovered physics/timing/input documentation.
- `tools/emulate_startup.py` / `tools/make_startup_data.py` — startup pipeline.
- `tools/oracle_physics.py` / `tools/physics_fixtures.py` — physics oracle.
- `wasm/test_physics.js` — C-vs-oracle physics regression.
- `wasm/render.c` — static renderer, viewport, object draw, HUD/screen hooks.
- `wasm/text.c` / `wasm/text.h` — fonts, glyph blitter, text windows (Phase 5).
- `wasm/shell.c` / `wasm/shell.h` — title/menu/story/help state machine (Phase 5)
- `wasm/sound.c/h`, `wasm/sound_data.h`, `wasm/sound_synth.js`, `wasm/audio.js` — Phase 6 sound
- `tools/sound_banks.py`, `tools/sound_fixtures.py`, `tools/export_sound.py` — sound oracle/export
- `notes/sound_fixtures.json`, `notes/sound_synth_fixtures.json`, `notes/sound_shell_fixtures.json`, `notes/sound.md`.
- `wasm/play.html` — dev level-select/input harness.
- `wasm/game.html` — full shell page (Phase 5).
- `wasm/test_hud.js` — HUD-vs-oracle regression.
- `wasm/test_text.js` — text-window frame/fill/text oracle regression.
- `wasm/test_shell.js` — shell flow/save/high-score/finale regression.
- `wasm/test_exactness.js` — P5.R5 timing/story/high-score/scan-code regression.
- `wasm/test_keymap.js` / `wasm/keymap.js` — browser key → DOS scan-code map.
- `tools/shell_exactness.py` / `tools/make_shell_exact_data.py` — P5.R5
  oracle capture and generated `wasm/shell_exact_data.h`.
- `tools/oracle_shell.py` — real-HUD and text-window oracle capture.
- `tools/export_shell_assets.py` — PCX/MNI/text/key/high-score export (Phase 5).
- `notes/hud_fixtures.json`, `notes/text_fixtures.json`, `notes/shell.md` —
  Phase 5 fixtures/docs.
- `tools/shell_exactness.py` / `notes/shell_exactness_fixtures.json` —
  P5.R5 capture harness (screen timing, story pages, high-score screen).
- `tools/make_shell_exact_data.py` / `wasm/shell_exact_data.h` — generated
  timing, story page, high-score default and scan-code label tables.
- `notes/hiscore_fixtures.json`, `notes/keymap_fixtures.json`,
  `wasm/test_exactness.js`, `wasm/test_keymap.js`, `wasm/keymap.js` — P5.R5
  high-score and scan-code regressions.
- `wasm/test_oracle_objects.js` — object/cell oracle comparison.
- `wasm/test_combat.js` — committed combat/cell/globals/RNG/sound oracle test.
- `tools/cell_fixtures.py` / `notes/cell_fixtures.json` — committed cell/contact/exit oracle trace.
- `wasm/test_cells.js`, `wasm/test_levels.js`, `wasm/test_cells_render.js` — cell/transition/render regressions.
- `tools/emulate_game.py` — full update-loop oracle and interaction fixtures.
- `tools/oracle_frame.py` — player physics/collision sub-routine oracle.
- `tools/combat_fixtures.py` / `notes/combat_fixtures.json` — committed combat
  oracle trace.
- `wasm/test_combat.js` — committed C-vs-oracle combat regression.
- `tools/export_assets.py` — portable GFX/level/font export; shell assets live
  in `tools/export_shell_assets.py`.
- `out/portable/cells.bin` — mutable 20-byte cell records.
- `out/portable/screens.bin`, `out/portable/shell_text.json`,
  `out/portable/mni_*.bin` — Phase 5 shell assets.
- `wasm/shell_text_data.h` — generated shell text tables.
- `wasm/render.h` — view/screen/glyph drawing API.
- `notes/handlers_disasm.txt` — handler port source.
- `notes/gameplay.md` — update-loop/helper/handler notes.
- `notes/oracle_catalog.json`, `notes/oracle_objects.json` — ground truth.
- `extracted/CC1-UNLZ.EXE` — original unpacked binary.
- `extracted/CC1.TTL/APG/CDT/END`, `CC1-*.SND`, `CC1-F1/F2/SPL.MNI` — remaining assets.
