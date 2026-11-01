# Phase 1 — Split the gameplay core

## Goal

Mechanically split `wasm/objects.c` (5,330 lines) into cohesive modules. **No
renaming, no logic changes, no behavior fixes.** Move code, expose what must be
shared, add declarations, compile, test.

## 1.1 Move the file first

Do a path-only commit so history follows the main file:

```sh
git mv wasm/objects.c wasm/game/objects.c
```

Update the Makefile and include paths in the same commit. Add `-I.` if needed.
Verify `make clang && make test-digest` before extracting anything.

## 1.2 Remove the `S` macro early

The existing code uses `#define S (&g_state)` everywhere. Replace `S->` with
`g_state.` in one mechanical pass, and delete the macro. This is a large but
compiler-checked diff; doing it early makes every later extraction cleaner.

Also move `cc_state_t` and `g_state` into `game/state.h` / `game/state.c`.
Keep the field order and add the Phase 0 layout asserts.

## 1.3 Target files

```text
wasm/game/
  objects.c          public gameplay API + frame orchestration (~200 lines)
  state.h/.c         cc_state_t, g_state, score/cave flags, constants
  object.h/.c        ObjectEntry helpers, slots, anim, spawns, redraw
  cell.h/.c          mutable cell records, solid/exit, dig, border, switches
  collision.h/.c     proximity, rect overlap, pixel/cell tests, player_clear
  physics.h/.c       input, horizontal/vertical/jump, player sync, camera
  combat.h/.c        fire, fire timers, slash, damage, transform, interaction
  cell_event.h/.c    cell_event, contact_frame, hazard cells, switch helpers
  level.h/.c         startup state, level state, overlay, load/arg/warp, doors
  update.h/.c        step loop, kind dispatch, death/restart, frame dispatch
  rng.h/.c           TP RNG
  save_state.h/.c    cc_state_* versioned snapshot
  api_accessors.c    cc_obj_*, cc_phys_*, cc_cell_*, cc_cur_level
  api_globals.c      cc_global_get/set compatibility shim
  test_hooks.c       cc_test_* hooks and fixture seeding
  handlers/
    handler.h        handler_fn signature + declarations
    table.c          offset -> function table, unknown-handler counter
    platforms.c      moving platforms, doors, formation movers
    actors.c         walkers, enemies, bosses
    interaction.c    proximity triggers, pickups, effects, switches
    combat.c         slash/projectile handlers
    misc.c           temporarily unclassified handlers
```

## 1.4 Extraction order

Each row is one commit (or a small series), followed by build + the gameplay
test ladder.

| Commit | Extract | Why first |
|---|---|---|
| 1 | `state.h/.c`, `rng.h/.c` | Shared state header; removes `S` |
| 2 | `cell.h/.c` | Cell accessors are used everywhere; clean seam |
| 3 | `object.h/.c`, `collision.h/.c` | Shared object/spawn/proximity helpers |
| 4 | `physics.h/.c` | Self-contained player/camera logic |
| 5 | `combat.h/.c` | Fire/slash/damage helpers |
| 6 | `cell_event.h/.c` | Cell-event + contact logic |
| 7 | `level.h/.c` | Startup/level/overlay/transition logic |
| 8 | `update.h/.c` + `handlers/*` | Orchestration and the 46 behaviours |
| 9 | `api_accessors.c`, `api_globals.c`, `save_state.c`, `test_hooks.c` | Remaining public/debug surface |
| 10 | Thin `game/objects.c` | Only public entry points and frame dispatch |

## 1.5 Function mapping

Use this as the extraction checklist. Line numbers refer to the current file
and will drift; match by function name.

### `state.h/.c`

- `cc_state_t`, `g_state`, `g_cells_asset`
- `wadd`, `wsub`, `wmul`, `wneg`
- `score_add`
- `cave_flag_get`, `cave_flag_set`
- constants (`OBJ_MAX`, screen/tile sizes, timer wraps)

### `rng.h/.c`

- `rng_next_hi`
- `cc_rng_next`, `cc_rng_seed`, `cc_rng_lo`, `cc_rng_hi`

### `cell.h/.c`

- `cc_set_cells`
- `cell_rec`, `cell_byte_cur`, `cell_put`, `cell_word_cur`, `cell_word_put`
- `cell_word_raw`, `cell_word_raw_put`
- `cell_solid_cur`, `cell_solid_at`
- `dig_cell`
- `cell_border_byte`, `cell_border_put`, `cell_w0_is_dig`
- `switch_pair`
- `count_crystal_cells`
- `cc_cell_byte`, `cc_cell_set`

### `object.h/.c`

- `obj_safe`
- `anim_next`, `anim_select_shape`
- `mark_redraw`, `visible_obj`
- `base_cell_stamp`
- `reuse_free_slot`
- `spawn_effect`, `spawn_template_2002d`, `spawn_object_47d`
- object accessors (`cc_obj_*`, `cc_obj_get/set`, `cc_obj_count`, cursor)

### `collision.h/.c`

- `proximity_test`, `proximity_mode2`, `proximity_obj_dir`
- `rect_overlap`
- `player_clear`, `cell_solid_px`, `cell_special_px`
- `cell_type_collectible`, `cell_test_200fe`
- `cc_collide_player`

### `physics.h/.c`

- `cc_input_set`, `input_commit`
- `update_walk_speed`
- `cc_vertical`, `cc_jump_vertical`, `cc_horizontal`
- `player_anim_next`, `player_select_shape`, `sync_player_object`
- `push_player`
- `cc_phys_get/set`
- `cc_set_player`, `cc_player_x/y`, `cc_cam_x/y`, `cc_move_dir`

### `combat.h/.c`

- `damage_life`
- `transform_object`
- `slash_gate`, `slash_state1`, `slash_state2`
- `player_interact`
- `cc_fire`, `cc_fire_timers`
- `cc_player_draw_sound` (or move to `update.c`; pick one and document why)

### `cell_event.h/.c`

- `helper_a110_clear`, `helper_a59d`
- `helper_toggle_gravity`, `helper_toggle_kind`
- `cell_event`
- `hazard_cell`
- `contact_frame`
- `cc_test_cell_event`, `cc_test_contact`

### `level.h/.c`

- `apply_startup_state`, `apply_level_state`, `apply_level_overlay`
- `load_level`, `load_arg`
- `cc_objects_init`, `cc_objects_init_silent`
- `cc_warp_level`, `cc_level_exit`, `cc_load_arg`
- `cc_set_level_overlay`
- `door_entry_frame`, `exit_to_hub_frame` (or `update.c`; choose one owner)

### `update.h/.c`

- `step_objects`
- `restore_checkpoint`, `reload_current_level`, `fade_latch`
- `restart_sequence`
- `death_animation`, `death_state1/2/3`
- `cc_objects_step`, `cc_objects_step_objects`, `cc_objects_step_physics`,
  `cc_objects_step_full`
- `cc_status_timers`
- `cc_test_fill_source`, `cc_test_call_handler`, `cc_test_spawn_*`

### `api_globals.c`

- `cc_global_get`, `cc_global_set`
- Keep the DS-offset keys and exact side effects.

### `save_state.c`

- `rd16_`, `rd32_`, `wr16_`, `wr32_`, `crc32_`
- `state_valid_`
- `cc_state_size/save/load/load_ex/last_error`
- `g_load_tmp`, `g_state_error`

### `handlers/`

Move all `h_XXXX` functions. Proposed grouping:

- **platforms.c:** `6AFA`, `6D31`, `6FF6`, `70E4`, `3F13`, `3D23`, `5855`,
  `5BBE`, `55EC`, `36B0`, `2F83`, `2E25`, `2D37`, `32F6`, `2176`
- **actors.c:** `298A`, `2ACF`, `2BD4`, `3416`, `47B9`, `48FE`, `4AD3`, `4EEB`,
  `6232`, `62F2`, `5C8F`, `4FD4`, `4145`, `6458`, `3904`
- **interaction.c:** `1FB9`, `2877`, `831`, `1FC2`, `323C`, `3E70`, `4A4B`,
  `4A82`, `3BE8`, `403A`, `3A2E`, `3F6C`, `6681`, `672B`, `69D3`, `2241`
- **combat.c:** `251D`, `2850`, `52E7`, `4C92`
- **misc.c:** anything whose role is unclear after tracing; move it later,
  don't guess.

The exact grouping is less important than the rule: every handler keeps its
original offset in a comment, and `handler_for()` is the single dispatch point.

## 1.6 Internal header rules

- Public `objects.h` keeps only the wasm-facing API. Remove
  `#include "objects_data.h"` from it.
- New `game/*.h` headers declare only what other modules need.
- `game_internal.h` is allowed initially as a convenience umbrella, but replace
  it with per-module headers before the phase ends.
- Only the data-owner translation unit includes generated tables
  (`objects_data.h`, `cells_data.h`, `startup_data.h`).
- No ad-hoc `extern` declarations in `.c` files.

## 1.7 Test ladder

After each extraction commit:

```sh
make clang
make test-digest
make test-handlers
make test-caves
make test-cells
make test-combat
make test-physics
make test-loader
```

At the end of the phase:

```sh
make test-all
make test-browser        # if available
make perf
```

## Exit criteria

- No gameplay file exceeds 600 lines.
- `game/objects.c` is under 300 lines or gone.
- All 46 handlers are reachable through one dispatch table.
- `make test-all`, `make test-digest` and `make perf` pass.
- `render.wasm` size and frame time are within the agreed budget.
- No public wasm export name changed.
