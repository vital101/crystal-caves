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
- 18/46 object handlers are implemented; 28 remain.
- `0x20406` first interaction block (touch damage, pickups, kind-`0x2C`
  score/effect) and the `0x20247`/`0x831` effect object are ported.
- Exact startup DS (`0xC3E`) is recovered. The vertical displacement table is
  at `DS:0xA111` (not `0xF245`), indexed by `[0x5134]`.
- Exact player/camera physics is validated frame-for-frame against the real
  routines: 16 scenarios / 670 frames, including level-2/3 platform carry and
  reversed gravity. Tick rate is 8 × 140.06 Hz PIT ticks ≈ 17.51 Hz.
- `out/portable/cells.bin` now comes from startup+loader and includes the
  level-2 hub door IDs; static oracle renders were regenerated to match.
- `make test-all`, `make test-oracle-objects`, and `make test-oracle-physics`
  pass. Changes are currently uncommitted.

Important remaining unknowns:

- Crystal collection and cave exit/airlock logic are not decoded/ported.
- Hub door-to-cave mapping and door entry are not decoded.
- `0x20406` second-hit/projectile block after `0x20707` is not ported.
- `0x5855` random companion-spawn branch is still TODO.
- General dynamic spawn (`0x47D`) is not ported; only the pickup effect spawn
  (`0x20247`) exists.
- Hazard/contact (`0xE5D3`), items/fire/exit (`0xEBF2`) and timers (`0xEAEA`)
  are outside the Phase 1 physics subset.

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

## Phase 2 — Complete `0x20406` and combat

Tasks:

- Port the second-hit/projectile block after `0x20707`:
  - player projectile hitbox `[0x463E]/[0x4640]`,
  - projectile state `[0x4652]`,
  - enemy hit/kill score paths,
  - hit locks `[0x4660]`.
- Port player fire and projectile spawn (player fire path `0xD17D`/`0xD236`).
- Port general object spawn `0x47D` and free paths needed by combat/effects.
- Port remaining `0x20406` score bonuses and kind-specific hit reactions.
- Add interaction fixtures with seeded player position/global flags:
  - pickup, damage, death/life loss, projectile hit, transform-to-kind-3.
- Resolve the `0x5855` random companion spawn branch; recover the TP RNG
  (`0x2BB5:0x980`) or seed behavior so fixtures are deterministic.

Acceptance:

- C `0x20406` matches the oracle for touch and projectile scenarios.
- Player can collect, take damage, lose/restore lives, and shoot correctly.
- Interaction fixtures are deterministic.

## Phase 3 — Cells, crystals, exits, hub doors

Tasks:

- Decode cell-based collectibles, especially the green crystal tiles.
  - Determine the cell fields that mark a collectible and the clear/redraw
    behavior.
  - Identify the crystal counter and HUD relationship.
- Port cell collection/exit routines around `0x1CCB0`/`0x1CD1C`.
- Port exit state machine `0xD412–0xD5B4` exactly:
  - cell `w0` next-level argument,
  - spawn override `[0x2B72]/[0x2B74]`,
  - airlock entry/exit animation.
- Decode hub door entry:
  - how a kind-60 door (`0x3F13`) maps to a cave,
  - how the 16 doors are ordered/selected,
  - return-to-hub spawn position and completion state.
- Implement mutable cell redraw/dirty-cell updates so collected crystals and
  opened doors visibly change.
- Add fixtures for crystal pickup, exit, hub door entry, and hub return.

Acceptance:

- Level 3 can be completed start-to-exit, returns to the hub, and marks progress.
- Hub doors load the correct caves.
- Cell/crystal state matches the oracle frame-for-frame.

## Phase 4 — Remaining handlers and all 16 caves

Remaining handlers (28):

```text
298A 2ACF 2BD4 2D37 2E25 2F83 32F6 3416 36B0 3904 3A2E 3F6C 403A
4145 47B9 48FE 4AD3 4C92 4FD4 52E7 55EC 5C8F 6232 62F2 6458 6681
672B 69D3
```

Order:

1. High-frequency shared handlers: `4145`, `3904`, `47B9`, `48FE`, `2D37`,
   `3416`, `32F6`, `2ACF`, `2BD4`, `298A`, `403A`, `55EC`, `5C8F`, `62F2`,
   `6232`, `4C92`, `52E7`, `2F83`, `4AD3`, `36B0`.
2. Dynamic/spawn-only handlers: `3F6C`, `6681`, `672B`, `69D3`.
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

1. Port the `0x20406` second-hit/projectile block and player fire.
2. Decode cell crystal collection and the `0xD412` exit state machine.
3. Decode hub door entry and return-to-hub spawn (door cell `w0` now carries
   IDs 1..16 in level 2).
4. Then port the remaining 28 handlers in cave-sized batches.

## Validation commands

```sh
cd wasm
make test-all
make test-physics            # committed physics fixtures (fast)
make test-oracle-physics     # regenerate physics fixtures (Unicorn)
make test-oracle-objects     # requires tools/requirements.txt + .venv
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
- `wasm/test_interaction.js` — `0x20406` pickup test.
- `tools/emulate_game.py` — full update-loop oracle and interaction fixtures.
- `tools/oracle_frame.py` — player physics sub-routine oracle (DS data missing).
- `tools/export_assets.py` — portable asset export; add PCX/SND/UI assets here.
- `out/portable/cells.bin` — mutable 20-byte cell records.
- `notes/handlers_disasm.txt` — handler port source.
- `notes/gameplay.md` — update-loop/helper/handler notes.
- `notes/oracle_catalog.json`, `notes/oracle_objects.json` — ground truth.
- `extracted/CC1-UNLZ.EXE` — original unpacked binary.
- `extracted/CC1.TTL/APG/CDT/END`, `CC1-*.SND`, `CC1-F1/F2.MNI` — remaining assets.
