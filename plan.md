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
- 21/46 object handlers are implemented; 25 remain.
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
- Known transition limitation: the original loader overlays cells/objects on
  the source level's memory. The port now loads the destination level's fresh
  image, so *unused* leftover bytes can differ after a transition. The loader
  map-command occupancy rules remain to be ported.

Important remaining unknowns:

- Exact loader cell/object overlay for transitions (map-command occupancy).
- 23 object handlers remain (see Phase 4 list).
- HUD, menus, story/finale transitions, sound and save/high scores.

## Critical path

1. Recover exact player physics/startup data.
2. Complete interaction/combat and projectiles.
3. Implement cell collection, exits, and hub door flow.
4. Port remaining handlers and finish all 16 caves.
5. Add HUD, menus, save/high scores, and sound.
6. Build full regression coverage and release.

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

## Phase 3 — Cells, crystals, exits, hub doors (COMPLETE, one known transition caveat)

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

Known caveat:

- The original loader overlays cells/objects onto the source level's memory.
  The port currently uses the destination level's fresh image on transitions,
  so unused leftover cell/object bytes can differ after hub->cave or
  cave->hub.  Contact, crystal, exit-animation and direct cell-event state are
  oracle-exact.  Exact transition-state replay needs the loader map-command
  occupancy rules; this is the first Phase 4 item.

## Phase 4 — Remaining handlers and all 16 caves

Remaining handlers (25; `0x6681`, `0x672B`, `0x69D3` are now ported):

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

## Phase 5 — HUD, text, and game shell

Tasks:

- Decode HUD layout and semantics around the HUD code near `0x1CE3B`:
  - lives/health, score, crystals,
  - message windows,
  - gravity-reversed/status indicators.
- Load and render MNI fonts (`mni_f1.bin`, `mni_f2.bin`) for HUD/messages.
- Implement HUD redraw in the 320×200 viewport.
- Decode/display PCX screens: title (`CC1.TTL`), Apogee logo (`CC1.APG`),
  credits (`CC1.CDT`), ending (`CC1.END`).
- Implement title/new game/story/instructions/ordering/about/foreign orders/
  high scores/BBS/video screens.
- Implement help menu, sound toggle, key redefine, gamepad/joystick,
  video toggle, save/restore 0–9, quit current game/title/main level, warp 1–16.
- Use browser-appropriate substitutions for DOS-only options while preserving
  the original screens/text.

Acceptance:

- Title-to-game-to-finale flow works.
- HUD values update correctly.
- All menus and options are reachable.
- Save/restore and high scores persist in `localStorage`.

## Phase 6 — Sound

Tasks:

- Reverse the Crystal Caves Sound driver (`0x0FC5`) and the three SND banks.
- Map `play_sound_or_effect` IDs (`0x1FBBF`) to effects.
- Pre-render PC-speaker effects to PCM at build time, or implement a runtime
  synthesizer.
- Add a WebAudio channel manager and sound-enable toggle.
- Add fixtures for sound ID selection/timing.

Acceptance:

- Sound effects trigger at the correct times and obey the in-game toggle.
- No missing/unknown sound IDs in normal gameplay.

## Phase 7 — Save/high-score/config state

Tasks:

- Define a versioned save-state schema:
  - globals, player, object array, mutable cells, level progress, crystals,
    score, lives, flags.
- Implement save/restore slots 0–9 in `localStorage`.
- Implement high-score table/name entry and config persistence.
- Verify save/restore mid-level and mid-hub round-trips exactly.
- Add UI to show “not available” for DOS-only file/BBS actions.

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
  - menus/save/high scores.
- Make `make test-all` cover the full deterministic suite.
- Add CI workflow (build, data checks, render diff, gameplay fixtures).
- Optimize wasm size/memory and frame budget.
- Refresh `AGENTS.md`, `notes/*`, and add a README with build/run instructions.
- Remove stale stub assertions/tests and superseded tools.

Acceptance:

- Clean checkout builds and passes the full suite.
- Playable from title to finale with no known gameplay stubs.
- Regression suite fails on any oracle divergence.

## Immediate next actions

1. Port the loader map-command occupancy rules so transition cell/object
   leftovers match the original exactly.
2. Port the remaining 25 handlers in cave-sized batches.
3. Decode HUD layout/counters and the story/finale transitions.

## Validation commands

```sh
cd wasm
make test-all
make test-physics            # committed physics fixtures (fast)
make test-oracle-physics     # regenerate physics fixtures (Unicorn)
make test-oracle-objects     # requires tools/requirements.txt + .venv
make test-combat             # committed combat fixtures (fast)
make test-cells              # committed cell/contact/exit fixtures
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
PASS: 17 cell scenarios, 20 frames match oracle
PASS: hub door and cave exit transitions load the correct levels
PASS: mutable cell renderer matches baked; crystal clear changed 150 pixels
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
- `wasm/render.c` — static renderer, viewport, object draw (HUD pending).
- `wasm/play.html` — playable shell, input, camera view, level selector.
- `wasm/test_oracle_objects.js` — object/cell oracle comparison.
- `wasm/test_combat.js` — committed combat/cell/globals/RNG/sound oracle test.
- `tools/cell_fixtures.py` / `notes/cell_fixtures.json` — committed cell/contact/exit oracle trace.
- `wasm/test_cells.js`, `wasm/test_levels.js`, `wasm/test_cells_render.js` — cell/transition/render regressions.
- `tools/emulate_game.py` — full update-loop oracle and interaction fixtures;
  extend with scenario seeding and full-frame order in Phase 2.
- `tools/oracle_frame.py` — player physics sub-routine oracle (DS data missing).
- `tools/combat_fixtures.py` / `notes/combat_fixtures.json` — planned
  committed combat oracle trace.
- `wasm/test_combat.js` — planned C-vs-oracle combat regression.
- `tools/export_assets.py` — portable asset export; add PCX/SND/UI assets here.
- `out/portable/cells.bin` — mutable 20-byte cell records.
- `notes/handlers_disasm.txt` — handler port source.
- `notes/gameplay.md` — update-loop/helper/handler notes.
- `notes/oracle_catalog.json`, `notes/oracle_objects.json` — ground truth.
- `extracted/CC1-UNLZ.EXE` — original unpacked binary.
- `extracted/CC1.TTL/APG/CDT/END`, `CC1-*.SND`, `CC1-F1/F2.MNI` — remaining assets.
