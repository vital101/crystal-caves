# Regression coverage matrix

Phase 8 requires every deterministic subsystem to have a committed oracle
fixture and a fast test that fails on divergence.  This file maps each
requirement to the target, fixture and harness that covers it.

`make -C wasm test-all` runs every row marked **yes** below.  It needs only
`node`, a C toolchain (Homebrew LLVM + lld, or apt `clang`/`lld`) and
`python3`; no virtualenv, Unicorn or Capstone.  Oracle-regeneration targets
are separate and need `tools/requirements.txt`.

| Subsystem | Make target | Fixture / reference | Harness | In `test-all` |
|---|---|---|---|---|
| Structural oracle checks | `test-data` | `notes/oracle_catalog.json`, `extracted/CC1-UNLZ.EXE` | `tools/check_oracle.py` | yes |
| Static rendering, all 19 levels | `test-render-all` | `out/oracle_L00..L18.png` | `test_render_all.js` | yes |
| Viewport crop | `test-view` | `out/oracle_L*.png` | `test_view.js` | yes |
| Object init/input/collision | `test-objects` | `out/portable/objects.json` | `test_objects.js` | yes |
| Level-2 fixture smoke | `test-gameplay` | level tables + object image | `test_gameplay.js` | yes |
| Render integration | `test-integration` | `out/portable/*`, oracle PNG | `test_integration.js` | yes |
| Player/camera physics | `test-physics` | `notes/physics_fixtures.json` | `test_physics.js` | yes |
| Combat / fire / slash / death | `test-combat` | `notes/combat_fixtures.json` | `test_combat.js` | yes |
| All 46 handlers | `test-handlers` | `notes/handler_fixtures.json` | `test_handlers.js` | yes |
| Hub↔cave transitions | `test-loader` | `notes/loader_fixtures.json` | `test_loader.js` | yes |
| All 16 caves, full frames | `test-caves` | `notes/cave_fixtures.json` | `test_handlers.js` | yes |
| Cells / contact / exits | `test-cells` | `notes/cell_fixtures.json` | `test_cells.js`, `test_levels.js`, `test_cells_render.js` | yes |
| HUD | `test-hud` | `notes/hud_fixtures.json` | `test_hud.js` | yes |
| In-game messages | `test-messages` | `notes/text_fixtures.json` + triggers | `test_messages.js` | yes |
| Text windows | `test-text` | `notes/text_fixtures.json` | `test_text.js` | yes |
| Shell flow / save / finale | `test-shell` | `notes/shell_exactness_fixtures.json` | `test_shell.js` | yes |
| Title/credits scroll | `test-shell-flow` | `notes/shell_flow_fixtures.json` | `test_shell_flow.js` | yes |
| Input cursor animation | `test-cursor` | F2 glyph bank + `notes/shell_flow_fixtures.json` | `test_cursor.js` | yes |
| Save format / high scores / config | `test-persist` | `notes/persist_v1_fixture.json`, `notes/hiscore_fixtures.json` | `test_persist.js` | yes |
| Shell timing / story / high scores | `test-exactness` | `notes/shell_exactness_fixtures.json`, `notes/hiscore_fixtures.json` | `test_exactness.js` | yes |
| DOS scan-code key map | `test-exactness` → `test-keymap` | `notes/keymap_fixtures.json` | `test_keymap.js` | yes (via exactness) |
| Sound driver / synth / shell | `test-sound` | `notes/sound_fixtures.json`, `notes/sound_synth_fixtures.json`, `notes/sound_shell_fixtures.json` | `test_sound.js`, `test_sound_shell.js` | yes |
| Browser smoke | `test-browser` | `wasm/browser_expectations.json` | `test_browser.js` (Playwright) | no (separate CI job) |
| Frame budget / wasm size | `perf` | `wasm/perf_baseline.json` | `measure_perf.js` | no (CI summary) |
Oracle regeneration (not part of `test-all`; requires `tools/requirements.txt`):

`test-oracle-objects`, `test-oracle-physics`, `test-oracle-combat`,
`test-oracle-handlers`, `test-oracle-loader`, `test-oracle-caves`,
`test-oracle-cells`, `test-oracle-shell`, `test-oracle-exactness`,
`test-oracle-sound`, `test-oracle-shell-flow`, plus `startup` and `data`.

## Unknown-handler guard

`cc_unknown_handler_hits()` is asserted to be `0` at the end of
`test_handlers.js` (which also serves `test-caves`).  Any level data that
reaches an unported handler now fails the fixture suite instead of silently
running the inert fallback.

## Non-fixture artifacts

`wasm/oracle_level*.json` (including `oracle_level3_interact.json`) are
per-run dumps produced by `tools/emulate_game.py`; they are gitignored and
not used by any test.  `out/oracle_L*.png` are the committed static
references.

## Build size and frame budget

The shipped `make clang` build uses `-Oz`: about 152 KB versus 180 KB at
`-O2`, with no fixture divergence.  `--gc-sections` saves only 2 bytes, so
`-Wl,--no-gc-sections` is kept (it avoids relying on linker reachability for
the exported surface).  `wasm-opt`/binaryen is not a build dependency.

`make perf` runs the same per-frame work as `game.html` (view cells, object
blits, HUD) for levels 2 and 3 on this machine:

```text
render.wasm: ~152000 bytes
memory: 256 pages (16 MiB initial, unchanged)
level 2 frame      avg ~1.2 ms  worst ~1.6 ms
level 3 frame      avg ~1.2 ms  worst ~1.6 ms
shell render       avg ~0.08 ms
```

The game frame budget is 57.1 ms (`8 * 0x2147 / 1193182` Hz ≈ 17.51 Hz), so
the wasm port has roughly 45x headroom.  `perf_baseline.json` holds loose
ceilings (10 ms average, 50 ms worst, 512 KiB) to catch pathological
regressions without making CI machine-dependent; exact timings are reported
in the CI job summary.
