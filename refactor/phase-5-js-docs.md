# Phase 5 — Browser JS, tests and final documentation

## Goal

Finish the cleanup outside the C core and leave the project with clear,
enforced conventions. This phase is lower priority than the C split but pays
off in maintainability.

## 5.1 Extract the inline browser script

`wasm/game.html` currently contains a ~507-line inline `<script>` block that
handles:

- wasm instantiation and asset loading;
- framebuffer presentation;
- game/shell rendering;
- keyboard and touch input;
- save/restore request handling;
- config/high-score persistence;
- the `?test=1` harness exposed as `window.__cc`.

Move it to `wasm/game.js` and keep `game.html` as markup plus a small
bootstrap. Suggested split:

```text
wasm/game.js            entry point, load/boot, frame loop
wasm/game/render.js     game/shell rendering helpers
wasm/game/input.js      keyboard/pointer/touch wiring
wasm/game/requests.js   save/restore handshake
```

Keep the public `window.__cc` test surface unchanged. `make test-browser`
must pass without modifying `browser_expectations.json`.

## 5.2 Shared test helpers

Several Node tests duplicate wasm instantiation, asset copying and state
snapshot logic:

- `test_persist.js` (716 lines);
- `test_browser.js` (552 lines);
- `test_handlers.js`, `test_cells.js`, `test_combat.js`, etc.

Add `wasm/test_helpers.js` with:

- `loadWasm()`;
- `copyAssets(e, mem, copy)`;
- `stateBytes(e, ...)`, `stateHash()`;
- `makeStorage()` for persistence tests;
- common assertion helpers.

Refactor the largest tests first (`test_persist.js`, `test_browser.js`), then
the smaller ones only if it clearly reduces duplication. Keep each test
runnable on its own.

## 5.3 Optional JS module cleanup

- `persist.js` (248 lines) is acceptable, but could split into
  `persist/config.js`, `persist/hiscore.js`, `persist/slot.js` if it grows.
- Add short JSDoc comments to the public functions in `persist.js`,
  `keymap.js`, `touch.js`, `audio.js` and `sound_synth.js`.
- Do not rewrite the JS build/bundling setup; there isn't one, and the current
  plain scripts work.

## 5.4 Finish the architecture document

`wasm/ARCHITECTURE.md` should contain:

1. **Layers and dependency rules**
   - public API → gameplay/shell/text/render modules → shared state;
   - which modules may include generated data headers;
   - which modules may call shell notifications.
2. **Module map**
   - one paragraph per file: responsibility, owned state, original routines.
3. **State and save invariants**
   - `cc_state_t` layout, `sizeof`, field renames vs. offsets;
   - v1/v2 save format and migration rules;
   - "never reorder fields" rule.
4. **Traceability convention**
   - `/* CC1 0x1F9A:0x2877 */` comments;
   - how to find the original routine in `notes/`.
5. **Handler table**
   - offset, name, file, kind, behaviour, fixture.
6. **Cell-event rules**
   - the `(w6,w7)` rule table and ordering constraints.
7. **How to add a handler or cell rule**
   - implement, name, add to table, add a fixture scenario, run digest.
8. **Test ladder and digest update procedure.**

## 5.5 Refactor map

Add `notes/refactor_map.md`:

- old file/line ranges and function names;
- new file and function names;
- commit hash or phase tag for each mapping;
- notes on any function that moved twice or was renamed.

This is the review aid for a decompilation refactor; it also helps future
contributors find the original routine.

## 5.6 Update `AGENTS.md`

Add or update:

- source layout and module ownership;
- file/function size budgets;
- no-behavior-change rule for structural commits;
- generated-header rules;
- `make lint` / `make test-digest` commands;
- handler/cell-rule conventions;
- test ladder.

Keep the existing build/data-pipeline sections intact.

## 5.7 Final CI pass

Run on macOS and Linux (CI):

```sh
make clean
make clang
make lint
make test-digest
make test-all
make perf
make test-browser        # if Playwright is available
```

Also run the optional oracle regeneration targets once to ensure the new
layout did not break the Python tools:

```sh
make test-oracle-handlers
make test-oracle-cells
make test-oracle-loader
make test-oracle-caves
make test-oracle-physics
make test-oracle-combat
```

These need the venv dependencies (`tools/requirements.txt`) and the committed
oracle data. They are a good final smoke test even if they are not in CI.

## Exit criteria

- `wasm/game.html` is markup + bootstrap only; the script lives in `game.js`
  and supporting modules.
- `window.__cc` and `browser_expectations.json` are unchanged.
- The largest test files use the shared helper where it removes real
  duplication.
- `wasm/ARCHITECTURE.md`, `notes/refactor_map.md` and `AGENTS.md` are complete.
- CI runs lint, digest, full tests and perf on the new layout.
- Optional oracle regeneration targets still work.
