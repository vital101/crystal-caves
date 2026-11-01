# Phase 3 — Make the code readable

## Goal

Now that the files are split, replace address-shaped names and giant functions
with concepts, enums, named rules and small functions. Every commit in this
phase is still expected to be behavior-preserving; use `make test-digest` as
the primary guard.

Work by domain, one commit per item below. Do not combine a rename with a
logic change.

## 3.1 Replace magic numbers with named concepts

Add small shared headers and replace literals across the gameplay code.

### `game/cell_fields.h`

```c
enum cell_word_offset {
    CELL_W0 = 0x00, CELL_W1 = 0x02, CELL_W2 = 0x04,
    CELL_W3 = 0x06, CELL_W4 = 0x08, CELL_W5 = 0x0A,
    CELL_W6 = 0x0C, CELL_W7 = 0x0E, CELL_W8 = 0x10, CELL_W9 = 0x12
};
#define CELL_FLAG_SOLID 0x11
#define CELL_FLAG_EXIT  0x13
```

Replace every `cell_word_cur(col, row, 0x0E)` with `CELL_W7`, etc. Use the same
enum in `render/render.c` so the renderer and gameplay cannot drift.

### `game/object_kind.h`

Define named kinds derived from verified handlers:

```c
enum object_kind {
    OBJ_KIND_HORIZ_PLATFORM = 0x19,
    OBJ_KIND_VERT_PLATFORM  = 0x14,
    OBJ_KIND_ELEVATOR       = 0x27,
    OBJ_KIND_DOOR           = 0x3C,
    ...
};
```

Where a kind is still uncertain, use `OBJ_KIND_XX` with a comment rather than
inventing a misleading name.

### `game/object_state.h`

```c
enum object_state {
    OBJ_STATE_FREE = 2,
    OBJ_STATE_ACTIVE = 0
};
```

Use only the names that are verified by the update loop and tests.

### Other constant headers

- `game/score.h` — named score awards (crystal, ammo, treasure, boss, etc.).
  Verify each value against its cell-event branch before naming it.
- `game/sound_ids.h` — named sound IDs where the call site is verified. Leave
  unknown IDs as `CC_SND_XX` with comments.
- `game/input.h` — input flag enum (`CC_INPUT_LEFT`, `CC_INPUT_RIGHT`,
  `CC_INPUT_JUMP`, `CC_INPUT_FIRE`, `CC_INPUT_DOWN`).
- `game/geometry.h` — view dimensions, tile size, culling margins and timer
  wrap constants.
- `text/glyphs.h` — F1/F2 glyph roles (window fill, border corners, cursor,
  digits, icons).

## 3.2 Rename state and object fields

### `cc_state_t`

Rename only fields whose meaning is verified. Keep the declaration order and
the DS-offset comments. Proposed mapping:

```c
g_a0cc  -> crystals_remaining
g_50a2  -> crystals_collected
g_a0f5  -> exit_unlocked
g_a0f7  -> hub_door_active
g_a0f9  -> switch_latch
g_a0f3  -> lever_state
g_a0fb  -> all_caves_complete
g_a126  -> story_transition
g_a10b  -> collectible_spawn_active
g_a10a  -> suppress_base_stamp
g_a0e8  -> switch_repeat
g_a0d0  -> spawn_cell_x
g_a0d2  -> spawn_cell_y
g_a0d4  -> switch_a_col
g_a0d6  -> switch_a_row
g_a0d8  -> switch_b_col
g_a0da  -> switch_b_row
g_a0dc  -> switch_c_col
g_a0de  -> switch_c_row
g_a0ce  -> collectible_spawn_timer
g_a0c2  -> power_shot_timer_preset
g_a0c8  -> gravity_timer_preset
g_a0ca  -> red_mushroom_timer_preset
g_a0e4  -> stun_timer_preset
g_a0c6  -> power_subtimer
g_509e  -> health
g_509c  -> ammo
g_5124  -> death_state
g_a0ea  -> invuln_timer
g_a0f6  -> interaction_lock
g_5118  -> carry_x_delta
g_5128  -> carry_y_delta
g_5126  -> scroll_x_accum
g_513c  -> interaction_enabled
g_5140  -> interaction_suppressed
g_513d  -> riding_horizontal
g_5142  -> riding_vertical
g_511a  -> platform_slot_h
g_511c  -> platform_slot_v
g_2b5e  -> restart_timer
g_2b5c  -> text_scroll
g_3394  -> restart_input_latch
g_a110  -> restart_latch
power_shot -> power_shot_active
```

Keep a `/* [0x....] */` or `/* DS:0x.... */` comment on every renamed field.
If a field's role is still ambiguous, keep a neutral name and a comment; do not
guess.

### `ObjectEntry`

The object record field map in `notes/objects.md` supports these renames:

```c
p02 -> hit_flag
p04 -> hit_points
p10 -> anim_frame
p12 -> anim_shape_a
p14 -> anim_shape_b
p16 -> anim_last
p18 -> phase
p1a -> subtype
p24 -> unused24
tile -> block
```

`vx`, `vy`, `speed`, `kind`, `ready`, `handler` and `handler_seg` are already
semantic.

This touches the generated `objects_data.h`, so update
`tools/make_objects_data.py` and `tools/check_oracle.py` in the same commit.
Keep the field order and add `offsetof` asserts for every renamed field. All
save/persist and handler fixtures must stay green.

## 3.3 Give handlers real names and a real table

Replace `h_6AFA` with `handler_horizontal_platform`, etc. Keep the original
offset in a comment:

```c
/* CC1 0x1F9A:0x6AFA — kind 0x19 horizontal moving platform. */
static void handler_horizontal_platform(ObjectEntry *o, int idx,
                                        int16_t x, int16_t y)
```

Replace `handler_for()`'s 56-line switch with one table:

```c
typedef struct {
    uint16_t offset;
    handler_fn fn;
    const char *name;   /* for docs and debug traces */
} HandlerEntry;

static const HandlerEntry g_handlers[] = {
    { 0x1FB9, handler_noop, "noop" },
    { 0x1FC2, handler_proximity_companion_spawner,
              "proximity_companion_spawner" },
    ...
};
```

Dispatch with a sorted table + binary search, or a simple linear scan. At 46
entries × 60 slots the cost is irrelevant against the 57 ms budget. Add an
assert/check that all 46 offsets are present. Keep `cc_test_call_handler`
working by lookup.

Update `notes/handlers.md` with columns: offset, new name, file, kind,
behaviour, fixture coverage.

## 3.4 Break up `cell_event()`

This is the biggest readability win. Extract each branch into a small rule
function and drive it from a table.

Current shape:

```c
if (w7 != 0) {
    if (w7 == 0x31 && w6 == 6) { ... goto phase2; }
    if (w7 == 0x32 && w6 == 6) { ... goto phase2; }
    ...
}
phase2:
...
hazard:
...
```

Target shape:

```c
typedef struct {
    int w6;
    int w7_min, w7_max;
    int (*apply)(CellEventCtx *c);
} CellRule;

static const CellRule g_cell_rules[] = {
    { 6, 0x31, 0x31, cell_take_ammo },
    { 6, 0x32, 0x32, cell_take_power_shot },
    { 0x0C, 0x26, 0x26, cell_take_gravity },
    ...
};
```

Correctness rules:

- Preserve the **order** of the original `if` chain; first match wins.
- Snapshot `w0/w3/w5/w6/w7` before mutations, because the original reads them
  again in phase 2.
- Preserve the `goto phase2` / `goto hazard` flow by having rules return
  "consumed", then running `cell_event_phase2()` and `cell_event_hazard()`
  separately.
- Keep every score/sound/cell mutation exact.

Target: `cell_event()` ~60–100 lines; each rule ~5–20 lines.

## 3.5 Break up the slash matrices

`slash_state1()` / `slash_state2()` are per-kind dispatch tables in disguise.
Convert to:

```c
switch (o->kind) {
    case OBJ_KIND_...:
        return slash_hit_xxx(o, idx, x, y);
    ...
}
```

Each handler gets a name, a comment with the original address range, and only
the logic for that kind. Keep `obj_safe()` and the `hit_points` semantics
unchanged.

## 3.6 Table-drive the repetitive accessors

### `cc_global_get/set`

Use one table to generate both directions:

```c
typedef struct {
    uint16_t ds_offset;
    uint16_t field_offset;   /* offsetof(cc_state_t, field) */
    uint8_t  size;
    uint8_t  is_signed;
    void   (*on_set)(cc_state_t *, int32_t);  /* optional side effects */
} GlobalField;
```

Keep explicit special cases:

- `0x4611` / `0x4613` mirror `obj[1].x/y`;
- `0x50A8..0x50B8` map to `cave_flags`;
- signed 16-bit reads.

This removes the risk of get/set drifting apart. Use the same pattern for
`cc_obj_get/set` and `cc_phys_get/set`.

### `apply_startup_state()` / `apply_level_state()`

Use an X-macro or a small declarative table for the simple copies
(`field`, `source`, `size`), leaving conditional/special cases explicit. Or
generate the copy functions from a manifest updated alongside
`startup_data.h`. Keep `apply_level_state`'s `fresh` / `use_start` conditions
visible; do not hide them in the table.

### `step_objects()`

Extract the order-sensitive kind chain into named helpers or a rule table:

```c
if (!o->ready && kind_runs_directly(o->kind)) { ... }
else if (visible) { ... }
else if (kind_is_invisible_special(o->kind)) { ... }
```

Preserve the exact `continue`/fall-through order and the `0xF38` visibility
check using the record's **current** x/y before the handler moves it. Add
comments explaining why the order matters.

## 3.7 Shell readability

- `cc_shell_key()` → one function per state (`key_menu`, `key_help`, `key_quit`,
  `key_save_restore`, `key_hiscore`, `key_warp`, …). The top-level switch should
  fit on one screen.
- `cc_shell_render()` → `render_title`, `render_menu`, `render_text`,
  `render_hiscore`, `render_game_overlay`, etc.
- Replace raw `cc_global_get(0x...)` / `cc_global_set(0x...)` calls in
  `text/hud.c` with typed accessors:
  - `cc_power_timer()`
  - `cc_power_subtimer()`
  - `cc_all_caves_complete()`
  - `cc_text_scroll()`
  - `cc_camera_x()`, `cc_camera_y()`
- Name the F1 glyph roles and frame layout constants.

## 3.8 Comments and traceability

Standardize the comment convention:

```c
/* CC1 0x1F9A:0x2877 — dissolve animation (kind 3).
 *
 * Reads:  phase, block, vx, speed
 * Writes: phase, ready, shape
 * Calls:  anim_next()
 */
```

Do not remove the original-address comments. They are the project's source of
truth and the main defense against "cleaning up" a deliberate quirk.

## Test ladder

After each readability commit:

```sh
make clang
make test-digest
```

Then the relevant suite (`test-handlers`, `test-cells`, `test-combat`,
`test-physics`, `test-shell`, `test-hud`, …). At the milestone boundary:

```sh
make test-all
make perf
```

## Exit criteria

- No hand-written function exceeds 120 lines (`make lint`).
- `cell_event()` is rule-driven and under ~100 lines.
- Slash matrices are per-kind functions.
- All handlers have descriptive names and one dispatch table.
- No raw cell-word offsets in gameplay/render code.
- No `g_a0XX` state names remain unless the meaning is genuinely unknown, and
  those have comments.
- `make test-all` and `make test-digest` pass.
