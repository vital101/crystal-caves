# Crystal Caves C/WASM architecture

Status: Phases 0–5 are implemented for the gameplay/C surface.  Phase 4 split
`game/game_internal.h` into per-module headers, moved generated-data includes
to their owner translation units, landed the deterministic wildcard build and
added the wasm export-stability check.  The only follow-up is splitting the
small `shell/shell_internal.h` umbrella the same way.  The browser script lives
in `game.js` plus three supporting modules, and the test harnesses share
`test_helpers.js`.

Read this together with:

- [`refactor/README.md`](../refactor/README.md) — phase plan and guardrails
- [`notes/refactor_baseline.md`](../notes/refactor_baseline.md) — size, perf
  and export baseline
- [`notes/refactor_map.md`](../notes/refactor_map.md) — old → new function map
- [`notes/regression.md`](../notes/regression.md) — subsystem → fixture matrix
- [`notes/handlers.md`](../notes/handlers.md) — original handler inventory

## 1. Layers and dependency rules

```text
 browser:  game.html (markup)
              │
              ├─ game.js (boot, config, frame loop, test hook)
              │    ├─ game/render.js    framebuffer presentation
              │    ├─ game/requests.js  save/restore handshake
              │    └─ game/input.js     keyboard/gamepad/touch wiring
              └─ adapters: keymap.js  legend.js  persist.js
                           sound_synth.js  audio.js  touch.js
              │
              ▼
 ┌──────────────────────── public wasm API (wasm/*.h) ─────────────────────────┐
 │ objects.h  shell.h  text.h  render.h  sound.h                               │
 │ stable exports: cc_objects_*, cc_shell_*, cc_text_*, cc_render_*, cc_*_get/set │
 └────────────────────────────────────┬─────────────────────────────────────────┘
                                      ▼
 ┌──────────────────────────── gameplay modules ──────────────────────────────┐
 │ state.c          cc_state_t, g_state, counters, flags, cell accessors      │
 │ object.h         frozen ObjectEntry record; no data tables                  │
 │ physics.c        input, horizontal/vertical/jump, player/camera sync       │
 │ combat.c         fire, slash, damage, transform, player interaction        │
 │ cell_event.c     pickup/switch/hazard rules, contact and exit events       │
 │ level.c          startup/level state, overlay, load/warp, generated data   │
 │ update.c         step loop, kind dispatch, frame orchestration            │
 │ update_death.c   death/restart/checkpoint, full-step entry points         │
 │ handlers_*.c     the 49 dispatch-table behaviours                          │
 │ handlers_table.c one offset → function → original label table              │
 │ api_accessors.c  cc_obj_*, cc_phys_*, cc_cell_*, cc_cur_level              │
 │ api_globals.c    cc_global_get/set compatibility shim                      │
 │ rng.c            Turbo Pascal RNG (0x2BB5:0x980)                           │
 │ save_state.c     CCS2 snapshot, CRC, CCS1 migration                        │
 └────────────────────────────────────┬─────────────────────────────────────────┘
                                      ▼
 ┌──────────────────────────── shared state ───────────────────────────────────┐
 │ g_state (cc_state_t): objects, cells, player/camera, counters, flags, RNG   │
 │ generated data: objects_data.h, cells_data.h, startup_data.h, overlay       │
 └─────────────────────────────────────────────────────────────────────────────┘
                                      ▲
 ┌──────────────────────────── sibling subsystems ─────────────────────────────┐
 │ shell/  state, input, render, hiscore, config, persist, scroll              │
 │ text/   font, window, hud                                                   │
 │ render/ runtime, blit, screens, level/view rendering                        │
 │ sound.c sound driver state (deliberately outside cc_state_t)                │
 └─────────────────────────────────────────────────────────────────────────────┘
```

Dependency rules:

1. Public API → gameplay/shell/text/render modules → shared state.  A module
   may read/write `g_state` through the helpers of the module that owns the
   field; it must not reach into another module's private statics.
2. Cross-module functions are declared in a module header and defined once.
   Keep everything else `static`; the compiler is the boundary enforcement.
3. `sound.c` is a leaf: any module may call it; it must not call gameplay.
4. Shell notifications (`cc_shell_notify_*`, `cc_shell_message`) are only for
   state transitions the original game performs inside gameplay handlers.
   Purely visual reporting belongs in the renderer, not in gameplay.
5. Generated data headers are included only by their owner translation unit:
   `objects_data.h` → `game/level.c`; `sound_data.h` → `sound.c`;
   `shell_text_data.h` → `text/window.c` plus the shell umbrella;
   `shell_exact_data.h` → the shell umbrella.  `cells_data.h` is included by
   its cell-data consumers (`game/state.c` and `game/api_accessors.c`);
   `startup_data.h` is included by `game/level.c`, `game/physics.c` and
   `game/handlers_actors_a.c`.
6. The browser modules have no build step or imports: they are plain scripts
   loaded in dependency order.  `game.js` is the only entry point; the three
   `game/*.js` helpers receive their wasm instance and callbacks from it.

Ownership summary:

| State | Owner | Notes |
|---|---|---|
| `cc_state_t g_state` | `game/state.c` | Single source of truth; layout frozen |
| Object slots | object helpers in `game/` | Allocation, cursor, animation, spawn, redraw |
| 20-byte cells | cell helpers in `game/state.c` / `game/cell_event.c` | Renderer is read-only |
| Player/camera/input | `game/physics.c` | Mirrors `0xE011`/`0xE36B`; syncs slot 1 |
| Score/lives/ammo/cave flags | `game/state.c` + `game/combat.c` / `game/cell_event.c` | Public accessors in `api_accessors.c` |
| RNG | `game/rng.c` | Exact Turbo Pascal byte-wise sequence |
| Level image/overlay/doors | `game/level.c` | `apply_startup_state`, `apply_level_state`, `apply_level_overlay` |
| Frame dispatch | `game/update.c` / `update_death.c` | `0xE011 → 0xE36B → 0xE5D3 → 0xEBF2 → 0xEAEA → 0xD72A` |
| Save/restore snapshot | `game/save_state.c` | Versioned header + raw `cc_state_t` payload |
| Shell page/state machine | `shell/state.c` | Pages, story/finale, message helpers |
| Shell input/scan codes | `shell/input.c` | `cc_shell_key`, `cc_shell_key_scan`, `cc_input_scan` |
| High scores | `shell/hiscore.c` | Defaults, sort/tie rules, 20-char name entry |
| Config | `shell/config.c` | Sound/joystick/video flags, dirty handshake |
| Save/restore request | `shell/persist.c` | `cc_shell_request_*`; JS owns localStorage |
| Fonts/text/HUD | `text/*.c` | Font banks, wrapping, HUD formatting |
| Blitters/screens/view | `render/*.c` | GFX blocks, PCX screens, view crop |
| Sound driver | `sound.c` | Host log/callback outside `cc_state_t` |

## 2. Module map

### Public headers

- `wasm/objects.h` — gameplay/objects public API: cell accessors, object slots,
  player/camera queries, level arguments and save entry points.  It declares
  only primitive-typed entry points and includes neither `game/object.h` nor
  the generated object tables.
- `wasm/shell.h` — shell state machine, menus, high scores, config, save
  request handshake and shell notifications.
- `wasm/text.h` — font setup, text-window primitives, block/line drawing, HUD.
- `wasm/render.h` — asset setup, cell/level/view renderers and raw framebuffer
  queries.
- `wasm/sound.h` — Crystal Caves Sound driver state and `cc_sound_note()`.
- `wasm/sound_ids.h` — named `SND_*` effect ids used by gameplay and shell.

### Gameplay (`wasm/game/`)

- `game_internal.h` — small aggregator over the per-module headers below.
  Individual modules may include the specific header they need.
- `state.h` - `cc_state_t`, `g_state`, shared gameplay globals, screen
  constants and the frozen-layout contract.
- `math.h` — freestanding `memcpy_`, 16-bit word arithmetic, Turbo Pascal
  long multiply/divide and `score_add`.
- `cell.h` — hand-written cell dimensions; includes `cell_fields.h`; generated
  `cells_data.h` includes `cell.h` instead of redefining its constants.
- `cell_ops.h` — raw 20-byte cell access, borders, digging, cave flags and
  cell-pixel tests.
- `object_ops.h` — `SpawnArgs`, object animation/visibility, proximity and
  spawn helpers.
- `physics.h` — input commit, player collision/carry, horizontal/vertical/jump
  and player/camera sync prototypes.
- `combat.h` — fire/timers, slash state machine, damage and interaction.
- `cell_event.h` — pickup/switch/hazard/contact helpers.
- `handlers.h` — `handler_fn`, the `HDL_*` offset enum and all named handler
  prototypes.
- `level.h` — startup/level/overlay/load and loader-argument prototypes.
- `update.h` — step loop, door/exit frames, death/restart helpers.
- `save.h` — little-endian/CRC helpers and `state_valid_`.
- `rng.h` — the internal RNG step.
- `sound_ops.h` — the internal `play_sound` bridge.
- `state.c` — owns `cc_state_t g_state`; word/long helpers, score/cave-flag
  accessors, raw 20-byte cell helpers and the compile-time layout asserts.
  Ports the original state block plus the cell addressing at
  `0x508C + (col+1)*500 + (row+1)*20`.
- `object.h` — hand-written `ObjectEntry` (0x2D bytes, packed), `OBJ_MAX` and
  loader-level count.  Field order is frozen; see the `offsetof` asserts.
- `cell_fields.h` — `CELL_W0..W9` word indices and `CELL_FLAG_*` byte offsets
  for the mutable 20-byte cell record.
- `score.h` — named score awards and the cell-event/combat branch that pays
  each one.
- `physics.c` — input flags, animation selection, player collision/carry,
  horizontal/vertical/jump motion and camera sync.  Original anchors:
  `0xE011`, `0xE36B`, `0xE5D3`, the vertical table at `DS:0xA111`.
- `combat.c` — fire, fire timers, slash state machine, damage, object
  transform and player/object interaction.  Fire entry is `0xCF6C` from
  `0xEBF2`; slot 2 is the attack object.
- `cell_event.c` — cell-event rule tables and helpers (pickups, switches,
  hazards, exits), `contact_frame()` and the four-corner contact walk.
  Original event handler is `0x0FC5:0xBB26`.
- `level.c` — startup/level state, `apply_startup_state`,
  `apply_level_state`, loader overlay application, `load_level`,
  `cc_warp_level`, level exit, sound host log accessors.  It is the only
  translation unit that includes `objects_data.h`.
- `update.c` — `step_objects()` kind dispatch, object loop ordering, fire and
  status timers, door/exit frame helpers and the public step entry points.
- `update_death.c` — death animation/restart/checkpoint, unknown-handler
  counter, full-step and physics-step orchestration.
- `handlers_platforms.c` — player no-op, attack gate, dissolve, pickup effect,
  hub door/lever, horizontal/vertical platforms and flip movers.
- `handlers_interaction.c` — companion spawners, map companions, spawners,
  animated movers, dug blocks, levers and projectile spawning helpers.
- `handlers_actors_a.c` — walkers, shovers, paired movers, projectiles,
  camera-relative spawns, formation movers, crystal bob and pickup rise.
- `handlers_actors_b.c` — proximity walkers/movers, respawners, boss
  formations, bouncing adversary and companion pairs.
- `handlers_table.c` — the single `g_handlers[]` table, `handler_for()`,
  `is_real_handler()`, `handler_name()` and coverage reporting.
- `api_accessors.c` — public `cc_obj_*`, player/camera, score/counters,
  cave flags, `cc_phys_get/set` and cell/object compatibility accessors.
- `api_globals.c` — DS-offset compatibility shim backed by
  `g_global_fields[]`; byte/word width and signedness come from the field.
- `rng.c` — Turbo Pascal RNG at `0x2BB5:0x980`, including the byte-wise
  high-word sequence (not a plain 32-bit multiply).
- `save_state.c` — `CCS2` header/CRC, v1 `CCS1` migration, atomic validation
  through `g_load_tmp`, `cc_state_size/save/load/load_ex/last_error`.

### Shell (`wasm/shell/`)

- `shell.c` — thin facade documenting the split.
- `shell_internal.h` — shared shell state/prototypes (umbrella header).
- `state.c` — page storage, story/finale transitions, message blocks, shell
  init/step/tick and notification helpers.
- `input.c` — key dispatch per shell state, text keys, scan-code redefinition,
  warp prompt and the `cc_shell_key` / `cc_shell_key_scan` entry points.
- `render.c` — shell page rendering, high-score/name-entry screens, overlays
  and the DOS exit screen.
- `hiscore.c` — ten-row table, original defaults, insertion sort/tie rules and
  qualification checks.
- `config.c` — sound/joystick/video flags, action bindings, dirty handshake
  and `cc_input_scan()`.
- `persist.c` — save/restore request state (`cc_shell_request_*`).
- `scroll.c` — title/credits scroll position and the input cursor phase.

### Text (`wasm/text/`)

- `text.c` — thin facade.
- `font.c` / `font.h` — glyph lookup, char/code mapping and F1/F2/SPL banks.
- `window.c` / `window.h` — frame cells, strings, line wrapping, block drawing
  and cursor drawing.  It owns the `shell_text_data.h` include.
- `hud.c` / `hud.h` — HUD counter formatting, tick animation and rendering.

### Render (`wasm/render/`)

- `render.c` — asset/palette setup, texture/view framebuffer selection, static
  level and view-cell rendering.
- `render_internal.h` — tile/screen geometry, palette and framebuffer state.
- `runtime.c` / `runtime.h` — freestanding `memset`, `memcpy` and `cc_alloc`.
- `blit.c` / `blit.h` — shape/glyph blitters and object draw helpers.
- `screens.c` / `screens.h` — PCX screen setup and raw view primitives
  (clear, fill, pixel, glyph, screen rows).

### Sound

- `sound.c` / `sound.h` — driver selection, priority/preemption, tick state,
  `[0x513E]` enable semantics and the host attempt log/event queue.
- `sound_data.h` — generated three-bank / 36-effect data.
- `sound_synth.js` — deterministic PIT trace → PCM renderer (must match
  `tools/sound_banks.py:synth_reference()`).
- `audio.js` — single-channel WebAudio manager; the C side decides what plays.

### Browser shell

- `game.html` — markup, styles, touch-pad DOM and script order only.
- `game.js` — asset loading, wasm instantiation, config/high-score boot,
  fullscreen setup, 140.06 Hz sound ticks, 17.5 Hz frame loop and `?test=1`
  `window.__cc` hook.
- `game/render.js` — `present`, `renderGame`, `renderShell`, `render`.
- `game/requests.js` — `handleRequest()` save/restore handshake over
  `CCPersist`/`localStorage`.
- `game/input.js` — keyboard scan-code wiring, jump aliases/latches, gamepad
  polling, blur release and touch attachment.  It owns the `KEY` and
  `JUMP_ALIAS_SCANS` tables.
- `keymap.js` — `KeyboardEvent.code` → IBM set-1 scan code, with `keyCode`
  fallback.
- `legend.js` — live key-cap legend rebuilt from the shell bindings.
- `persist.js` — localStorage config/high-score/slot adapter and migration.
- `touch.js` — pointer pad, soft keyboard, tap-to-advance and visibility
  preference.

### Tests

- `test_helpers.js` — shared `loadWasm`, `bindMemory`, `copyAssets`,
  `loadGame`, `stateBytes`, `stateHash`, `makeStorage`, assertion and string
  helpers.  Every test remains runnable standalone.
- `test_*.js` — one fixture family per file; see
  [`notes/regression.md`](../notes/regression.md) for the full matrix.

## 3. State and save invariants

`cc_state_save()` writes a `CCS2` header followed by a **raw image** of
`cc_state_t`:

```text
offset 0   magic "CCS2" (0x32534343)
offset 4   version 2 (uint16 LE)
offset 6   payload size (uint16 LE) == sizeof(cc_state_t)
offset 8   CRC-32/ISO-HDLC of the payload
offset 12  raw cc_state_t image
```

Rules:

1. `sizeof(cc_state_t) == 23010`; `sizeof(ObjectEntry) == 0x2D`.  Compile-time
   asserts in `game/state.c` pin both plus the byte offsets of every major
   section (objects, cells, player/camera, input, counters, cave flags,
   checkpoint block).
2. Never reorder or resize fields.  Renaming is fine; update the comments and
   the affected asserts.  Public accessors that still carry DS-style names
   (`cc_g_a0c4`, `cc_g_a0ff`, …) must keep those export names.
3. A genuine layout change bumps the format version and adds a migration in
   `cc_state_load_ex()`.  Legacy `CCS1` saves remain readable; the real
   captured v1 fixture `notes/persist_v1_fixture.json` guards it.
4. `cc_state_t` must not contain host pointers, sound state, shell state or
   other host-only data.  The digest and save tests hash this image.
5. Validation happens on `g_load_tmp` before committing, so a rejected load
   never half-mutates the game.  Hub-only loads are enforced by the shell and
   by `cc_state_load_ex(..., hubOnly)`.
6. The original `CC1.HS` is all-or-nothing: a missing/corrupt high-score file
   is replaced by the ten defaults in `notes/hiscore_fixtures.json`.  Slot
   data, config and high scores are validated in `persist.js` before touching
   wasm memory.

## 4. Traceability convention

Every ported routine keeps its original location in a comment:

```c
/* CC1 0x1F9A:0x2877 (kind 3): transformed-object dissolve animation. */
static void h_dissolve(ObjectEntry *o, int idx, int16_t x, int16_t y)
```

Use the form `/* CC1 SEG:OFFSET */` or `/* 0x...:0x... */`; the loader test
tools assume segment offsets are byte offsets.  Useful anchors:

- load offset = `0x1F9A0 + offset`; file offset = load offset + `0x1E00`
- game runs `CS=0`, `DS=0x2CD3`
- original frame order: `0xE011 → 0xE36B → 0xE5D3 → 0xEBF2 → 0xEAEA → 0xD72A`
- cell addressing: `0x508C + (col+1)*500 + (row+1)*20`
- handler inventory: `notes/handlers.md` / `notes/handlers.json`
- cell-event handler: `0x0FC5:0xBB26` (linear `0x1B776`)
- sound driver: `0x1F9A:0x021F`, ISR `0x0A3F`, 140.06 Hz

When renaming a handler, update `g_handlers[]` and the table below.  The
original address and automatic label must remain discoverable from the new
name.  `tools/disasm.py`, `tools/trace_handlers.py` and the Python oracle
scripts are the lookup path for new work.

## 5. Handler table

`g_handlers[]` in `wasm/game/handlers_table.c` is the single authoritative
dispatch table: one row per original handler load offset with the named C
function and its automatic disassembler label.  It contains 49 dispatch
offsets: the 46 entries in `notes/handlers.md` plus the dynamic `0x4EEB` phase
and the helper entries `0x0831`/`0x2241`.  Object kinds map to these offsets
through the loader table in `wasm/objects_data.h`; the kind column below is
only filled where that table or a handler comment confirms it.

Fixture legend: `handler` = direct scenario in
`notes/handler_fixtures.json`; `cave` = exercised by the full-frame
`notes/cave_fixtures.json`; `loader` = transition fixture.  Every row appears
in at least one committed fixture and `test_handlers.js` fails on an unknown
offset.

| Offset | C function | Original label | File | Behaviour | Fixture |
|---|---|---|---|---|---|
| `0x1FB9` | `h_player_noop` | `h_1fb9` | `handlers_platforms.c` | player no-op (`retf 6`) | cave |
| `0x2850` | `h_attack_gate` | `h_2850` | `handlers_platforms.c` | slash-ready gate, calls `0x251D` | cave |
| `0x2877` | `h_dissolve` | `h_2877` | `handlers_platforms.c` | transformed-object dissolve | cave |
| `0x0831` | `h_pickup_effect` | `h_831` | `handlers_platforms.c` | pickup/effect helper | cave |
| `0x3D23` | `h_door_lever` | `h_3d23` | `handlers_platforms.c` | door lever animation | cave |
| `0x3F13` | `h_door` | `h_3f13` | `handlers_platforms.c` | hub door (kind 60) | cave |
| `0x6AFA` | `h_platform_h` | `h_6afa` | `handlers_platforms.c` | horizontal platform | cave |
| `0x6D31` | `h_platform_v` | `h_6d31` | `handlers_platforms.c` | vertical platform | cave |
| `0x6FF6` | `h_flip_mover` | `h_6ff6` | `handlers_platforms.c` | flip mover | cave |
| `0x70E4` | `h_flip_partner` | `h_70e4` | `handlers_platforms.c` | flip partner | cave |
| `0x2241` | `h_dug_block` | `h_2241` | `handlers_interaction.c` | dug-block helper | cave |
| `0x1FC2` | `h_companion_spawner` | `h_1fc2` | `handlers_interaction.c` | companion/proximity spawner | cave |
| `0x2176` | `h_map_companion` | `h_2176` | `handlers_interaction.c` | map-cell companion | cave |
| `0x323C` | `h_spawn_below` | `h_323c` | `handlers_interaction.c` | spawn below cell | cave |
| `0x3BE8` | `h_expand_anim` | `h_3be8` | `handlers_interaction.c` | expand animation | cave |
| `0x3E70` | `h_ambient_anim_a` | `h_3e70` | `handlers_interaction.c` | ambient animation A | cave |
| `0x4A4B` | `h_ambient_anim_b` | `h_4a4b` | `handlers_interaction.c` | ambient animation B | cave |
| `0x4A82` | `h_state_toggle` | `h_4a82` | `handlers_interaction.c` | state toggle | cave |
| `0x5855` | `h_group_move3` | `h_5855` | `handlers_interaction.c` | three-object group mover | cave |
| `0x5BBE` | `h_anim_mover` | `h_5bbe` | `handlers_interaction.c` | animated mover | cave |
| `0x298A` | `h_walk_two_phase` | `h_298a` | `handlers_actors_a.c` | two-phase walker | handler, cave |
| `0x403A` | `h_player_shover` | `h_403a` | `handlers_actors_a.c` | player shover | handler, cave |
| `0x55EC` | `h_move_h_paired` | `h_55ec` | `handlers_actors_a.c` | paired horizontal mover | handler, cave |
| `0x6232` | `h_walk_two_frame` | `h_6232` | `handlers_actors_a.c` | two-frame walker | handler, cave |
| `0x4C92` | `h_spawn_projectile` | `h_4c92` | `handlers_actors_a.c` | projectile spawner | handler, cave |
| `0x4EEB` | `h_walk_companion` | `h_4eeb` | `handlers_actors_a.c` | dynamic companion walker | cave |
| `0x52E7` | `h_projectile` | `h_52e7` | `handlers_actors_a.c` | projectile | handler, cave |
| `0x2F83` | `h_move_v_paired` | `h_2f83` | `handlers_actors_a.c` | paired vertical mover | handler, cave |
| `0x3A2E` | `h_lever_anim` | `h_3a2e` | `handlers_actors_a.c` | lever animation | handler, cave |
| `0x3F6C` | `h_player_shover_alt` | `h_3f6c` | `handlers_actors_a.c` | alternate player shover | handler, cave |
| `0x2E25` | `h_spawn_cam_rel` | `h_2e25` | `handlers_actors_a.c` | camera-relative spawn | handler, cave |
| `0x4AD3` | `h_walk_anim_lr` | `h_4ad3` | `handlers_actors_a.c` | left/right walk animation | handler, cave |
| `0x36B0` | `h_move_v_formation` | `h_36b0` | `handlers_actors_a.c` | vertical formation mover | handler, cave |
| `0x6681` | `h_cell_erase` | `h_6681` | `handlers_actors_a.c` | cell erase | cave |
| `0x672B` | `h_crystal_bob` | `h_672b` | `handlers_actors_a.c` | crystal bob | cave |
| `0x69D3` | `h_pickup_rise` | `h_69d3` | `handlers_actors_a.c` | pickup rise | cave |
| `0x2D37` | `h_move_v_prox` | `h_2d37` | `handlers_actors_b.c` | proximity vertical mover | handler, cave |
| `0x3416` | `h_walk_prox_spawn` | `h_3416` | `handlers_actors_b.c` | proximity spawn walker | handler, cave |
| `0x62F2` | `h_walk_respawn` | `h_62f2` | `handlers_actors_b.c` | respawning walker | handler, cave |
| `0x5C8F` | `h_boss_formation` | `h_5c8f` | `handlers_actors_b.c` | boss formation | handler, cave |
| `0x2ACF` | `h_walk_flip_contact` | `h_2acf` | `handlers_actors_b.c` | flip-contact walker | handler, cave |
| `0x2BD4` | `h_walk_h_prox` | `h_2bd4` | `handlers_actors_b.c` | proximity horizontal walker | handler, cave |
| `0x3904` | `h_lever_switch` | `h_3904` | `handlers_actors_b.c` | lever switch | handler, cave |
| `0x4145` | `h_bounce_adversary` | `h_4145` | `handlers_actors_b.c` | bouncing adversary | handler, cave |
| `0x47B9` | `h_walk_two_anim` | `h_47b9` | `handlers_actors_b.c` | two-animation walker | handler, cave |
| `0x32F6` | `h_move_v_companion` | `h_32f6` | `handlers_actors_b.c` | vertical companion | handler, cave |
| `0x48FE` | `h_walk_random` | `h_48fe` | `handlers_actors_b.c` | random walker | handler, cave |
| `0x4FD4` | `h_boss_retarget` | `h_4fd4` | `handlers_actors_b.c` | boss retarget | handler, cave |
| `0x6458` | `h_companion_pair` | `h_6458` | `handlers_actors_b.c` | companion pair | handler, cave |

## 6. Cell-event rules

`cell_event(x, y)` at `0x0FC5:0xBB26` snapshots the cell's `w0`, `w3`, `w5`,
`w6` and `w7` into `CellEventCtx` before doing any work.  The phases then run
in the original order:

1. **Pickups** — `g_cell_pickup_rules[]` matches `(w6, w7 inclusive range)`.
   The table is ordered exactly like the original `if` chain and the first
   match wins.  Rules award score, play a sound, clear `w7`, mark the cell for
   redraw and may arm a shell message or a power/stun timer.
2. **Switches** — `g_cell_switch_rules[]` matches the *snapshot* `(w3, w5)`
   and is skipped when `w3 == 0`.  The first match wins.  Levers and passage
   switches walk the live `w3` counter by ±1, set the lever/switch latches and
   flip their paired cell through `switch_pair()`.  Elevator start/stop rules
   call `helper_toggle_kind()` with the snapshot `w0`, which is why those
   objects must be identified by their pre-event kind.
3. **Hazard tail** — reads the *current* exit flag
   (`CELL_FLAG_EXIT`) but the snapshot `w6/w7`; gas cells poison the player and
   otherwise uncovered walking may cost a life.
4. **Exit/checkpoint rules** — the checkpoint (`w3=0x0D`, `w5=1`) records the
   exit cell, arms `hub_door_active`, copies the checkpoint block and plays the
   exit-found sound.  The exit cells drive the door/exit state machine in
   `update.c` / `update_death.c`.

Ordering constraints:

- Do not sort the rule tables.  Several predicates overlap and the original
  first-match order is observable (for example `w6=0x0D, w7=1..4` crystals
  before `w6=0x0D, w7=0x2C` prize key).
- Snapshot values are deliberate: later rules mutate `w3`/`w7`, but the switch
  table must keep evaluating the pre-phase values.
- All cell writes go through `cell_word_put` / `cell_put` on the live record,
  followed by `mark_redraw(x, y)` when the visible cell changes.
- `contact_frame()` walks four player-box points plus the optional fifth
  (`g_a106`) in the same order as `0xE5D3`; do not reorder those probes.

## 7. How to add a handler or cell rule

Add a handler:

1. Find the original entry in `notes/handlers.md` (offset + segment).
2. Trace call sites and state fields with `tools/disasm.py`; keep the original
   address in the function comment.
3. Add the prototype to `game/handlers.h`, implement it in the most specific
   `handlers_*.c` file, and register the offset in `game/handlers_table.c`.
4. If it owns new state, add fields at the end of `cc_state_t` and update the
   layout asserts.  Never reorder existing fields.
5. Add/extend an oracle fixture (`tools/handler_fixtures.py`) and run
   `make test-oracle-handlers && make test-handlers`.
6. Run the gameplay ladder and `make test-digest`.  A new handler is a
   behavior change, so its fixture update belongs in its own commit.
7. Update `notes/handlers.md` and the handler table above.

Add a cell rule:

1. Locate the original branch in `cell_event()` (`0x0FC5:0xBB26`) and record
   the `(w6,w7)` or `(w3,w5)` key.
2. Add a small `static` rule helper next to its peers; never inline a new
   branch into `cell_event()` itself.
3. Insert the row at the matching position in `g_cell_pickup_rules[]` or
   `g_cell_switch_rules[]` (first-match order is part of the behavior).
4. Extend `tools/cell_fixtures.py` with a direct branch scenario and a
   full-frame case if the rule changes contact/exit state.
5. Run `make test-oracle-cells && make test-cells test-caves`.
6. If the rule is a behavior fix rather than a mechanical port, update the
   digest in a separate commit and say why the hashes moved.

## 8. Test ladder and digest update

| Change area | Minimum tests |
|---|---|
| Build / public API | `make test-exports` |
| Gameplay core | `make test-digest test-handlers test-caves test-cells test-combat test-physics test-loader` |
| Shell | `make test-shell test-shell-flow test-menus test-hud test-text test-persist` |
| Rendering | `make test-render-all test-view test-caves test-browser` |
| Browser JS | `make test-browser` (plus `make test-persist test-keymap test-exactness`) |
| Milestone boundary | `make test-all && make test-render-digest && make perf` |
| Every commit | `make lint` (CI runs `make lint-strict`) |

`notes/refactor_digests.json` is a golden master of SHA-256 hashes:

```sh
cd wasm
node test_refactor_digest.js            # compare
node test_refactor_digest.js --update   # rewrite after an intentional change
```

The fixture covers:

1. static render of levels 0–18 (`cc_render_cells` full framebuffer);
2. levels 2–18 for 480 frames with a deterministic idle/walk/jump/fire/reverse
   schedule, snapshotting every 10 frames (save image, view framebuffer, sound
   log);
3. hub → every cave → hub transitions and `cc_warp_level` 0–18;
4. shell flow: logo → title → menu → intro → pause/help → key redefine →
   warp → save/restore request → high score → finale → ending → name entry;
5. save → mutate → restore → replay determinism.

Rules:

- A structural refactor **must never** update this fixture.
- An intentional behavior change updates it in a **separate commit** with a
  message that says why the hashes moved.
- The digest is independent of host audio: sound is initialized then disabled,
  and every snapshot drains the attempt log.
- Scenario groups boot fresh wasm instances, so state cannot leak between them.
- `notes/refactor_render_digests.json` (`make test-render-digest`) covers the
  shell/text/render surface more densely; it is separate from the Phase 0 file
  and follows the same update rule (`make render-digest-update`).

## 9. Guardrails

1. No behavior changes in structural commits.  Put a bug fix in its own commit.
2. One seam per commit; compile and run the relevant tests after each.
3. Never reorder `cc_state_t` or `ObjectEntry`; rename only.
4. Never change public wasm export names (adding a hook is allowed and must be
   recorded in the baseline).
5. Never hand-edit generated headers; change the Python generator and run
   `make data`.
6. Preserve original-address traceability in comments and the handler table.
7. Do not mix mass formatting with logic changes; review with `git diff -w`
   and `git log --follow`.
8. Target cohesion, not file count: 200–500 lines per hand-written `.c`, hard
   cap 600, no function over 120 lines.
