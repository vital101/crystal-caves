# Crystal Caves C/WASM Refactoring Plan

Status: Phases 0 (safety net), 1 (gameplay split), 2 (shell/text/render
split), 3 (readability), 4 (structural cleanups, gameplay side) and 5 (browser
JS, tests and docs) are implemented.  The only Phase 4 follow-up left is
splitting the small `shell_internal.h` umbrella.  See
[`../notes/refactor_baseline.md`](../notes/refactor_baseline.md) for the
captured baseline, [`../notes/refactor_map.md`](../notes/refactor_map.md) for
the implemented module map and [`../wasm/ARCHITECTURE.md`](../wasm/ARCHITECTURE.md)
for the architecture document.

This directory contains the refactoring plan split by phase:

| Phase | File | Goal |
|---|---|---|
| 0 | [phase-0-safety-net.md](phase-0-safety-net.md) | Baseline, characterization tests, layout asserts and CI guardrails |
| 1 | [phase-1-gameplay-split.md](phase-1-gameplay-split.md) | Mechanically split `objects.c` into gameplay modules |
| 2 | [phase-2-shell-text-render.md](phase-2-shell-text-render.md) | Split `shell.c`, `text.c` and `render.c` by responsibility |
| 3 | [phase-3-readability.md](phase-3-readability.md) | Rename, extract and table-drive the unreadable code |
| 4 | [phase-4-structural-cleanups.md](phase-4-structural-cleanups.md) | Header hygiene, build system, optional hardening |
| 5 | [phase-5-js-docs.md](phase-5-js-docs.md) | Browser JS, test helpers and final documentation |

## Progress

Phase 1 is implemented as a mechanical split of `wasm/objects.c` into
`wasm/game/*.c` modules with a shared `game_internal.h` umbrella header.  The
split preserves the `cc_state_t` layout, the public wasm export names and the
original-address comments.  `wasm/objects.c` is removed; all 46 handler
behaviours (49 dispatch offsets) remain reachable through `handler_for()` in
`game/handlers_table.c`.

Verification at the Phase 1 milestone:

- `make clang` builds cleanly from scratch without the monolith.
- `make test-all` and `make test-digest` pass (929 digest snapshots).
- `make perf` reports 1.15 ms per level frame against the 57.1 ms budget and a
  163,796-byte `render.wasm` against the 524,288-byte cap.
- No public wasm export name changed.

Known follow-ups (deferred to later phases by design): the umbrella header was
later split into per-module headers by Phase 4; the Phase 3 splits resolved the
long functions.

Phase 2 is implemented the same way: `wasm/shell.c`, `wasm/text.c` and
`wasm/render.c` moved to their directories in path-only commits, then split
mechanically into `wasm/shell/*.c` (umbrella `shell_internal.h`),
`wasm/text/*.c` and `wasm/render/*.c` (small per-module headers).  The sound
attempt log also moved from the gameplay modules into `wasm/sound.c`.  Public
export names are unchanged.

Verification at the Phase 2 milestone:

- `make clang` builds cleanly; `make test-all` and `make test-digest` pass
  (the same 929 digest snapshots, before and after each split).
- New render/text/scroll characterization harness: `make test-render-digest`
  (155 digests in `notes/refactor_render_digests.json`), A/B-verified against
  the pre-split build.
- `make perf` reports ~1.0 ms per level frame (57.1 ms budget) and a
  164,026-byte `render.wasm`; `make test-browser` passes.
- Exit criteria: `shell/shell.c` 25 lines, `text/text.c` 20, `render/render.c`
  144; largest file in the three areas is `shell/input.c` at 411 lines.

Phase 3 (readability) is implemented.  One seam per commit, each guarded by
`make test-digest` (the 929 goldens never changed):

- Named constants: cell-record fields `CELL_W0..W9` / `CELL_FLAG_*`
  (`game/cell_fields.h`), `SND_*` effect ids (`sound_ids.h`).
- Descriptive handler names for every dispatch-table entry, resolved through
  the single `g_handlers[]` table in `game/handlers_table.c`.
- `cc_state_t` renames of the understandable fields; the remaining `g_a0XX`
  latch/companion names carry comments (public exports like `cc_g_a0c4` stay).
- `cell_event()` split into a rule table plus small rule helpers;
  `slash_state1()` into per-kind arm/kill functions; `cc_horizontal()` into
  impulse/walk/carry helpers; `apply_startup_state()` into state sections;
  `apply_level_overlay()` along the overlay format sections; `cc_shell_key()`
  into per-state handlers; `cc_shell_render()` into per-screen helpers.
- `cc_global_get/set()` replaced by one `g_global_fields[]` data table
  (DS offset → field, byte-vs-word width derived from the field, sign flag);
  this exposed and fixed two latent read mismatches: `0x5132` gravity_scale
  reads sign-extended and `0xA0BE` next_level reads zero-extended, matching
  the original getters.

Verification at the Phase 3 milestone:

- `make lint` reports zero function-length failures; CI now runs
  `make lint-strict`.
- `make test-all` passes with zero FAIL lines; `make test-digest` (929) and
  `make test-render-digest` (155) match their committed goldens.
- `make perf` reports ~0.88 ms per level frame (57.1 ms budget) and a
  161,656-byte `render.wasm`; `make test-browser` passes.

Phase 4 (structural cleanups) is implemented for the gameplay side:

- `f9acf6d` replaced the hand-maintained clang/emcc source lists with
  deterministic wildcard lists and per-file objects plus `-MMD -MP` depfiles.
- `db966ba` moved `ObjectEntry` to hand-written `game/object.h`; the generated
  `objects_data.h` is now data only and is included by `game/level.c`.
- `game_internal.h` is now a small aggregator over per-module headers
  (`state.h`, `math.h`, `cell_ops.h`, `object_ops.h`, `physics.h`, `combat.h`,
  `cell_event.h`, `handlers.h`, `level.h`, `update.h`, `save.h`, `rng.h`,
  `sound_ops.h`).  `cells_data.h` is included by `state.c`/`api_accessors.c`;
  `startup_data.h` by `level.c`/`physics.c`/`handlers_actors_a.c`.
- `make test-exports` compares the built module against the Makefile
  `EXPORTS` list (184 exports).
- Remaining follow-up: split `shell_internal.h` the same way.  Optional
  hardening ideas remain in [phase-4-structural-cleanups.md](phase-4-structural-cleanups.md).

Phase 5 (browser JS, tests and docs) is implemented:

- The ~507-line inline script moved out of `wasm/game.html` into `wasm/game.js`
  plus `game/render.js`, `game/input.js` and `game/requests.js`; `game.html` is
  now markup, styles and script tags.  `window.__cc` and
  `browser_expectations.json` are unchanged and `make test-browser` passes.
- `wasm/test_helpers.js` provides the shared load/asset/state/assertion helpers;
  `test_persist.js` and `test_browser.js` use it, as do the large shell/text
  harnesses and `test_refactor_digest.js` where the duplication was real.
  Every test still runs alone.
- Public JS entry points in `persist.js`, `keymap.js`, `touch.js`, `audio.js`
  and `sound_synth.js` have JSDoc; `persist.js` remains one cohesive module.
- `wasm/ARCHITECTURE.md` is complete (layers, module map, state/save invariants,
  traceability, handler/cell tables, extension recipes, test ladder and digest
  procedure), and `AGENTS.md` carries the short conventions.
- No bundler was added: the plain ordered scripts are deliberate.

## Why this plan exists

The code is correct but organized around the original binary's addresses rather
than around concepts. The main symptoms:

| Hotspot | Size | Problem |
|---|---:|---|
| `wasm/objects.c` | 5,330 lines | State, cells, physics, combat, 46 handlers, level loading, save format and test hooks in one file |
| `cell_event()` | 367 lines | Giant `if`/`goto` chain keyed by magic `w6`/`w7` values |
| `slash_state1()` | 206 lines | Per-kind hit matrix encoded as nested `if` chains |
| `apply_startup_state()` / `apply_level_state()` | 230 lines combined | Repetitive field-by-field copies with no single source of truth |
| `cc_global_get()` / `cc_global_set()` | 252 lines combined | Two parallel 126-case switches mapping DS offsets to fields |
| `step_objects()` | 94 lines | Long kind-specific special-case chain whose order matters |
| `shell.c` | 1,244 lines | Input, rendering, persistence, high scores, config and the state machine all mixed |
| `text.c` | 381 lines | Font lookup, text windows and HUD in one file |
| `render.c` | 310 lines | Freestanding runtime, blitter, level renderer and PCX screens in one file |
| 46 handlers | ~1,400 lines | Named `h_1fb9`, `h_2241`, … with no semantic names |
| State fields | 23 KB struct | Half the fields are named `g_a0cc`, `g_5118`, `g_a100` |
| `objects.h` | small | Includes the generated `objects_data.h`; leaks data into every includer |
| `cc_sound_note` | — | Declared in `sound.h` but implemented in `objects.c`; layering violation |

A `-Wall -Wextra` build currently reports two unused static functions, and there
is no architecture document or automated size/complexity guard.

## Non-negotiable guardrails

1. **No behavior changes in structural commits.** A bug found during
   refactoring gets a `TODO`/issue and a separate behavior-fix commit.
2. **One seam per commit.** Compile and run the relevant tests after every
   commit; run the full suite at each milestone.
3. **Never reorder `cc_state_t` or `ObjectEntry` fields.** The save payload is
   a raw struct image (`sizeof(cc_state_t) == 23010`). Rename fields only; keep
   the layout and the static asserts.
4. **Never change public wasm export names.** The browser page and every test
   depend on them.
5. **Never hand-edit generated headers.** Update the Python generator and run
   `make data`.
6. **Preserve original-address traceability.** Keep comments like
   `/* CC1 0x1F9A:0x2877 */` and maintain a handler table mapping
   offset → new name → file.
7. **Do not mix mass formatting with logic changes.** Use `git diff -w`,
   `--color-moved` and `git log --follow` for review.
8. **Target cohesion, not arbitrary file count.** 200–500 lines per file;
   hard cap 600 for hand-written `.c`.

## Target module layout

The final shape should look approximately like this. Names may shift during
implementation, but the ownership boundaries should not.

```text
wasm/
  runtime.c/.h                 freestanding memcpy/memset + bump allocator
  objects.h                    public gameplay API (unchanged exports)
  shell.h
  text.h
  render.h
  sound.h

  game/
    objects.c                  public facade + frame orchestration (~200)
    state.h/.c                 cc_state_t, g_state, score/cave flags, constants
    object.h/.c                ObjectEntry helpers, slots, anim, spawns, redraw
    cell.h/.c                  cell records, solid/exit, dig, border, switches
    collision.h/.c             proximity, rect overlap, pixel/cell tests
    physics.h/.c               input, horizontal/vertical/jump, player sync
    combat.h/.c                fire, fire timers, slash, damage, transform
    cell_event.h/.c            cell events, contact, hazard, switch helpers
    level.h/.c                 startup/level state, overlay, load/arg/warp, doors
    update.h/.c                step loop, kind dispatch, death/restart, frames
    rng.h/.c                   TP RNG
    save_state.h/.c            versioned cc_state_* snapshot
    api_accessors.c            cc_obj_*, cc_phys_*, cc_cell_*, cc_cur_level
    api_globals.c              cc_global_get/set compatibility shim
    test_hooks.c               cc_test_* hooks and fixture seeding
    handlers/
      handler.h                handler_fn signature + declarations
      table.c                  offset -> function table, unknown-handler counter
      platforms.c              moving platforms, doors, formation movers
      actors.c                 walkers, enemies, bosses
      interaction.c            proximity triggers, pickups, effects, switches
      combat.c                 slash/projectile handlers
      misc.c                   temporarily unclassified handlers

  shell/
    state.c                    init, step, page setup, story/message helpers
    input.c                    key dispatch, text keys, scan-code mapping
    render.c                   shell rendering and overlays
    hiscore.c                  high-score table, name entry, sorting
    config.c                   sound/joystick/video config
    persist.c                  save/restore request handshake
    scroll.c                   title/credits scroll + input cursor

  text/
    font.c/.h                  glyph lookup, character table, F1/F2 selection
    window.c/.h                frames, layout, strings, cursor
    hud.c/.h                   HUD formatting and rendering

  render/
    runtime.c/.h               memset/memcpy/cc_alloc
    blit.c/.h                  shape/glyph blitters, object draw helpers
    screens.c/.h               PCX screens and view primitives
    render.c                   level/view rendering and asset setup
```

## Test ladder

Use the smallest relevant set while iterating, then the full suite at each
milestone.

| Change area | Minimum tests |
|---|---|
| Build / public API | `make test-exports` |
| Gameplay core | `make test-digest test-handlers test-caves test-cells test-combat test-physics test-loader` |
| Shell | `make test-shell test-shell-flow test-menus test-hud test-text test-persist` |
| Rendering | `make test-render-all test-view test-caves test-browser` |
| Milestone boundary | `make test-all && make perf` |

`make test-digest` is part of `make test-all`.  Its
`notes/refactor_digests.json` golden master must never be updated by a
structural refactor; see "Digest update procedure" in
[`../wasm/ARCHITECTURE.md`](../wasm/ARCHITECTURE.md).

## Risk register

| Risk | Mitigation |
|---|---|
| Subtle behavior drift | Digest test + existing 2,000+ fixture frames + one seam per commit |
| Save format break | No field reordering; `sizeof`/`offsetof` asserts; v1 fixture; persist tests |
| Cross-TU inlining changes size/perf | Measure every phase; budgets in `perf_baseline.json`; fallback LTO/inline helpers |
| Hidden coupling via `static`/`S` macro | Compiler catches missing declarations; remove `S` early; per-module headers |
| Handler misclassification | Keep `handlers/misc.c`; trace with `notes/handlers.md`; tests cover all 46 |
| Review fatigue from huge mechanical diffs | `git mv` first, module-at-a-time commits, `git diff -w --color-moved` |
| Generated header drift | Generator-only edits; `make data`; CI checks `git diff` clean |
| Build path changes break CI | Explicit `SRC` list; test Linux CI; keep `make clang` as the single entry point |
| Refactor turns into a rewrite | Hard rule: no logic changes in structural commits; behavior fixes separate |

## Definition of done

- No hand-written `.c` file exceeds 600 lines; no function exceeds 120 lines.
- `objects.c` is gone or reduced to a < 300-line public facade; `shell.c` < 300;
  `text.c`/`render.c` < 250.
- All 46 handlers have descriptive names, one dispatch table, and entries in
  `notes/handlers.md`.
- `cell_event()` is rule-driven and under ~100 lines; slash matrices are
  per-kind functions.
- No `h_XXXX`, `g_a0XX` or raw cell-word offsets in new code; original DS
  addresses remain in comments/tables.
- `cc_state_t` layout unchanged (`sizeof == 23010`), v1 migration passes, all
  save-round-trip tests pass.
- Public wasm exports unchanged; JS works without changes.
- `make test-all`, `make test-digest`, `make perf` and `make test-browser`
  pass.
- `make lint` passes in CI; no new compiler warnings.
- `wasm/ARCHITECTURE.md`, `notes/refactor_map.md` and updated `AGENTS.md` are
  committed.

## Suggested first week

1. **Day 1:** baseline + digest harness + layout asserts + lint script. Commit.
   No source changes.
2. **Day 2:** `git mv objects.c game/objects.c`; extract `state.h/.c` +
   `rng.h/.c`; replace `S->` with `g_state.`; run the gameplay test ladder.
3. **Day 3:** extract `cell.h/.c` and `object.h/.c`; run tests.
4. **Day 4:** extract `collision.h/.c` and `physics.h/.c`; run tests + perf.
5. **Day 5:** extract `combat.h/.c`; run full `test-all` + digest; tag
   `refactor/game-core-1`.

The key point: **the first phase must be boring**. Move code, compile, test,
repeat. Readability work starts only once the boundaries are in place and the
digest test is watching.
