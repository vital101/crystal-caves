# Crystal Caves C/WASM architecture

Status: **Phase 1 implemented**.  The gameplay core has been mechanically split
into `wasm/game/*.c` modules with a shared `game_internal.h` umbrella header;
`wasm/objects.c` is removed.  The ownership boundaries below are the target
shape and are partially matched by the mechanical split (line-range based).
Shell/text/render splits are Phase 2.  See
[`../notes/refactor_map.md`](../notes/refactor_map.md) for the implemented
module map.  This document grows with each phase and is completed in Phase 5.

Read this together with:

- [`refactor/README.md`](../refactor/README.md) — phase plan and guardrails
- [`notes/refactor_baseline.md`](../notes/refactor_baseline.md) — size, perf
  and export baseline
- [`notes/regression.md`](../notes/regression.md) — subsystem → fixture matrix

## Layer diagram

```text
                     browser (game.html, audio.js, persist.js, touch.js, keymap.js)
                                              │
                                              ▼
        ┌──────────────────────────── public wasm API (wasm/*.h) ───────────────────────────┐
        │ objects.h  shell.h  text.h  render.h  sound.h                                     │
        │ stable export names: cc_objects_*, cc_shell_*, cc_text_*, cc_render_*, cc_*_get/set │
        └────────────────────────────────────┬───────────────────────────────────────────────┘
                                             ▼
        ┌──────────────────────────── gameplay modules ──────────────────────────────────────┐
        │ objects.c        frame orchestration + public facade                            │
        │ state.c          cc_state_t, g_state, constants, cave/score flags               │
        │ object.c         ObjectEntry slots, animation, spawns, redraw                   │
        │ cell.c           mutable 20-byte records, solid/exit, dig, borders, switches   │
        │ collision.c      proximity, rectangle overlap, pixel/cell tests                │
        │ physics.c        input, horizontal/vertical/jump, player sync, camera          │
        │ combat.c         fire, fire timers, slash, damage, transform                    │
        │ cell_event.c     cell events, contact, hazards, switch helpers                 │
        │ level.c          startup/level state, overlay, load/arg/warp, doors            │
        │ update.c         step loop, kind dispatch, death/restart, frames               │
        │ rng.c            Turbo Pascal RNG (0x2BB5:0x980)                               │
        │ save_state.c     CCS2 snapshot + CCS1 migration                                │
        │ api_accessors.c  cc_obj_*, cc_phys_*, cc_cell_*, cc_cur_level                  │
        │ api_globals.c    cc_global_get/set compatibility shim                          │
        │ test_hooks.c     cc_test_* hooks and fixture seeding                           │
        └────────────────────────────────────┬───────────────────────────────────────────────┘
                                             ▼
        ┌──────────────────────────── shared state ───────────────────────────────────────────┐
        │ g_state (cc_state_t): objects, cells, player/camera, counters, flags, RNG         │
        │ generated data: objects_data.h, cells_data.h, startup_data.h, level_overlay.bin   │
        └────────────────────────────────────────────────────────────────────────────────────┘
                                             ▲
        ┌──────────────────────────── sibling subsystems ─────────────────────────────────────┐
        │ shell/  state, input, render, hiscore, config, persist, scroll                    │
        │ text/   font, window, hud                                                         │
        │ render/ runtime, blit, screens, level/view rendering                              │
        │ sound.c sound driver state (deliberately outside cc_state_t)                      │
        └────────────────────────────────────────────────────────────────────────────────────┘
```

The dependency rule is one-way: public API → modules → shared state.  A module
may use the shared state and generated assets; it must not reach into another
module's private statics.  `sound.c` is a leaf: anything may call it, and it
must not call gameplay.

## Current → target file map

| Today | Target |
|---|---|
| `wasm/game/*.c` (implemented; `wasm/objects.c` removed) | `game/*.c` split per the diagram; semantic regrouping continues in Phase 3 |
| `wasm/game/handlers_*.c` handler functions `h_1fb9`, `h_2241`, … | `game/handlers/{platforms,actors,interaction,combat,misc}.c` with descriptive names |
| `wasm/shell.c` (1,2xx lines) | `shell/{state,input,render,hiscore,config,persist,scroll}.c` |
| `wasm/text.c` | `text/{font,window,hud}.c` |
| `wasm/render.c` | `render/{runtime,blit,screens,render}.c` |
| `wasm/render.c` `memset`/`memcpy`/`cc_alloc` | `runtime.c` / `render/runtime.c` |

Names may shift during implementation, but ownership must not.  Target size is
200–500 lines per hand-written `.c`; hard cap 600.  No function over 120 lines.

## Ownership rules

| State | Owner | Notes |
|---|---|---|
| `cc_state_t g_state` | `state.c` | Single source of truth; field layout frozen (see invariants) |
| Object slots (`g_state.obj`) | `object.c` | Slot allocation, cursor, animation, spawn, redraw |
| 20-byte cells (`g_state.cells`) | `cell.c` | All reads/writes go through cell helpers; renderer is read-only |
| Player, camera, input | `physics.c` | Mirrors original `0xE011`/`0xE36B`; syncs slot 1 |
| Score, lives, ammo, cave flags | `state.c` + `combat.c`/`cell_event.c` | Accessors in `api_accessors.c` |
| RNG (`rng_lo/rng_hi`) | `rng.c` | Exact Turbo Pascal byte-wise sequence |
| Level image / overlay / doors | `level.c` | `apply_startup_state`, `apply_level_state`, `apply_level_overlay` |
| Current frame dispatch | `update.c` | `step_objects`, death/restart, call order `0xE011 → 0xE36B → 0xE5D3 → 0xEBF2 → 0xEAEA → 0xD72A` |
| Save/restore snapshot | `save_state.c` | Versioned header + raw `cc_state_t` payload |
| Shell page/state machine | `shell/state.c` | Pages, story/finale, message helpers |
| Shell input/scan codes | `shell/input.c` | `cc_shell_key`, `cc_shell_key_scan`, `cc_input_scan` |
| High scores | `shell/hiscore.c` | Defaults, sort/tie rules, 20-char name entry |
| Config | `shell/config.c` | Sound/joystick/video flags, dirty handshake |
| Save/restore request handshake | `shell/persist.c` | `cc_shell_request_*`; JS owns localStorage |
| Fonts / text windows / HUD | `text/*.c` | Font banks, line wrapping, HUD formatting |
| Blitters / screens / level render | `render/*.c` | GFX blocks, PCX screens, view crop |
| Sound driver state | `sound.c` | Host log/callback outside `cc_state_t` |

Cross-module access is via small functions declared in that module's header,
not by `extern` globals.  Keep `static` on everything that does not need to
cross a boundary; the compiler is the enforcement mechanism.

## Save-format invariants

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
   asserts in `game/state.c` enforce both, plus the
   byte offsets of every major section (objects, cells, player/camera, input,
   counters, cave flags, checkpoint block).
2. Never reorder or resize fields.  Renaming is fine; update the comments and
   the affected asserts.
3. A genuine layout change bumps the format version and adds a migration
   (`cc_state_load_ex`).  Legacy `CCS1` saves remain readable; the v1 fixture
   `notes/persist_v1_fixture.json` guards it.
4. `cc_state_t` must not contain host pointers, sound state, shell state or
   other host-only data.  The digest and save tests hash this image.
5. Validation happens on a temporary copy (`g_load_tmp`) before committing, so
   a rejected load never half-mutates the game.

## Original-address convention

Every ported routine keeps its original location in a comment:

```c
/* CC1 0x1F9A:0x2877 (kind 3): transformed-object dissolve animation. */
static void h_2877(ObjectEntry *o, int idx, int16_t x, int16_t y)
```

Use the form `/* CC1 SEG:OFFSET */` or `/* 0x...:0x... */`; the loader test
tools assume segment offsets are byte offsets.  Useful anchors:

- load offset = `0x1F9A0 + offset`; file offset = load offset + `0x1E00`
- game runs `CS=0`, `DS=0x2CD3`
- original frame order: `0xE011 → 0xE36B → 0xE5D3 → 0xEBF2 → 0xEAEA → 0xD72A`
- cell addressing: `0x508C + (col+1)*500 + (row+1)*20`
- handler inventory: `notes/handlers.md` / `notes/handlers.json`

When renaming `h_XXXX` functions, update the mapping table below.  The original
address must remain discoverable from the new name.

## Handler taxonomy

Placeholder until Phase 3 assigns descriptive names.  The authoritative current
inventory is [`notes/handlers.md`](../notes/handlers.md); this table is the
refactor map that replaces it.  Phase 1 kept the original `h_XXXX` names and
grouped the handlers by line range into `wasm/game/handlers_*.c`.

| Offset | Current name | Descriptive name | Target file | Notes |
|---|---|---|---|---|
| `0x1FB9` | `h_1fb9` | player slot (no-op) | `handlers/misc.c` | `retf 6` |
| `0x2241` | `h_2241` | … | … | … |
| `0x251D` | `h_251d` | slash update | `handlers/combat.c` | called by `0x2850` gate |
| `0x2850` | `h_2850` | slash ready gate | `handlers/combat.c` | |
| `0x2877` | `h_2877` | transformed-object dissolve | `handlers/interaction.c` | |
| … | … | … | … | all 46 entries |

Rules for this table:

- One row per handler-table entry; the offset column is the dispatch key.
- A handler that is temporarily unclassifiable goes to `handlers/misc.c`.
- Every handler must have an oracle fixture in `notes/handler_fixtures.json`
  (`make test-handlers`) before it is renamed.

## How to add a handler

1. Find the original entry in `notes/handlers.md` (offset + segment).
2. Trace its call sites and state fields with `tools/disasm.py`; record the
   original address in the function comment.
3. Add the prototype to `handlers/handler.h`, implement it in the most
   specific taxonomy file, and register the offset in `handlers/table.c`.
4. If it owns new state, add the fields to `cc_state_t` **at the end** and
   update the layout asserts, or reuse an existing original global.  Never
   reorder existing fields.
5. Add/extend an oracle fixture (`tools/handler_fixtures.py`) and run
   `make test-oracle-handlers && make test-handlers`.
6. Run the gameplay ladder and `make test-digest`; the digest change must be
   explained in the commit (a new handler is a behavior change, so it updates
   the fixture in its own commit).
7. Update this table and `notes/handlers.md`.

## Test ladder

| Change area | Minimum tests |
|---|---|
| Gameplay core | `make test-digest test-handlers test-caves test-cells test-combat test-physics test-loader` |
| Shell | `make test-shell test-shell-flow test-menus test-hud test-text test-persist` |
| Rendering | `make test-render-all test-view test-caves test-browser` |
| Milestone boundary | `make test-all && make perf` |
| Every commit | `make lint` (warning-only in Phase 0; `make lint-strict` once clean) |

## Digest update procedure

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

- A refactor **must never** update this fixture.
- An intentional behavior change updates it in a **separate commit** with a
  message that says why the hashes moved.
- The digest is independent of host audio: sound is initialized then disabled,
  and every snapshot drains the attempt log.
- Scenario groups boot fresh wasm instances, so state cannot leak between them.

## Guardrails

1. No behavior changes in structural commits.
2. One seam per commit; compile and test after each.
3. Never reorder `cc_state_t` or `ObjectEntry`; rename only.
4. Never change public wasm export names (adding a hook is allowed and must be
   recorded in the baseline).
5. Never hand-edit generated headers; change the Python generator and run
   `make data`.
6. Preserve original-address traceability in comments and the handler table.
7. Do not mix mass formatting with logic changes.
8. Target cohesion, not file count: 200–500 lines per `.c`, hard cap 600.
