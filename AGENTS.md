# Crystal Caves Decompilation — AGENTS.md

## Build & Test (run from `wasm/`)

There is no root Makefile; the build lives in `wasm/`.

| Command | Description |
|---|---|
| `make clang` | Build freestanding wasm via Homebrew LLVM (default) |
| `make emcc` | Emscripten build (needs `emcc` on PATH; secondary, not in CI) |
| `make test-all` | Data checks + all-19 render + gameplay/loader/text/shell regression |
| `make test-render-all` | Pixel-diff every level against `out/oracle_L*.png` |
| `make test-data` | Run `../tools/check_oracle.py` (structural oracle assertions) |
| `make test-view` | 320x200 viewport crop regression |
| `make test-objects` | Object init/input/collision smoke test |
| `make test-gameplay` | Level-2 fixture smoke test |
| `make test-physics` | Exact player/camera/platform-carry regression (committed fixtures) |
| `make test-combat` | Committed combat/cell/globals/RNG/sound oracle fixtures (fast) |
| `make test-handlers` | Committed per-handler oracle fixtures (all 46 behaviours) |
| `make test-loader` | Committed exact loader-transition fixtures (hub -> caves and caves -> hub) |
| `make test-caves` | Committed full-frame integration fixtures for all 16 caves |
| `make test-cells` | Committed cell-event/contact/exit fixtures + transition/render checks |
| `make test-hud` | Oracle-exact HUD fixtures (real HUD routine) |
| `make test-messages` | In-game message trigger causes (positive/negative) |
| `make test-text` | Oracle-exact text-window frame/fill/text fixtures |
| `make test-shell` | Shell flow, text/PCX screens, save/restore, high scores, finale |
| `make test-persist` | Phase 7 save format/round-trips, high scores and config persistence |
| `make test-exactness` | P5.R5 timing/story/high-score/scan-code exactness fixtures |
| `make test-keymap` | Browser physical-key → DOS scan-code mapping |
| `make test-sound` | Sound driver, trace asset, JS synth and shell sound fixtures |
| `make test-browser` | Headless Chromium smoke test of `game.html` (Playwright; skips if absent) |
| `make test-browser-update` | Regenerate `wasm/browser_expectations.json` framebuffer hashes |
| `make perf` | wasm size + per-frame timing against the 57.1 ms game budget |
| `make test-oracle-sound` | Regenerate the Unicorn sound trace, then compare |
| `make test-oracle-exactness` | Regenerate shell exactness fixtures/header |
| `make test-oracle-shell` | Regenerate `notes/hud_fixtures.json` + `notes/text_fixtures.json`, then compare |
| `make test-oracle-cells` | Regenerate `notes/cell_fixtures.json` with Unicorn, then compare |
| `make test-oracle-physics` | Regenerate physics fixtures from the real routines, then compare (needs venv deps) |
| `make test-oracle-objects` | Compare C object/cell state with the Unicorn oracle, levels 2 and 3 (needs venv deps) |
| `make test-oracle-combat` | Regenerate `notes/combat_fixtures.json` from the real routines and compare (needs venv deps) |
| `make test-oracle-handlers` | Regenerate `notes/handler_fixtures.json` with Unicorn, then compare |
| `make test-oracle-loader` | Regenerate `notes/loader_fixtures.json` with Unicorn, then compare |
| `make test-oracle-caves` | Regenerate `notes/cave_fixtures.json` with Unicorn, then compare |
| `make startup` | Regenerate `notes/startup.json` + `wasm/startup_data.h` (needs venv deps) |
| `make assets` | Copy portable assets from `../out/portable/` into `wasm/assets/` |
| `make data` | Regenerate `wasm/startup_data.h` + `out/portable/` |

**Order matters:** `assets` must exist before `clang`/`emcc`. `make clang` creates
`assets/` automatically. `test-data` needs no wasm build.

**Toolchain:** Homebrew LLVM **and lld** (`brew install llvm lld`); the Makefile
uses `brew --prefix llvm` or pass `LLVM=/path/to/bin`. The clang build uses
`-Oz` (~152 KB `render.wasm`). Python oracle tools need
`tools/requirements.txt` (`capstone`, `unicorn`); use `../.venv/bin/python` (the
Makefile auto-detects it) or install system-wide. npm is **test-only**
(`wasm/package.json` + Playwright for `test-browser`); the game build never
needs it. CI is `.github/workflows/ci.yml` (Ubuntu: build + `test-all` +
size/perf summary, browser smoke, manual oracle regeneration).

## Data Pipeline

`extracted/` and `out/portable/` are committed, so builds/tests do not need the
original archive. To regenerate raw extraction you must supply `CCSW10A.SHR`
(not in repo), then run `python3 tools/extract_shr.py`.

1. `python3 tools/unlzexe.py` — `extracted/CC1-UNLZ.EXE` (184,288-byte load module)
2. `python3 tools/emulate_startup.py` — captures `0xC3E` startup + loader state to `notes/startup.json`
3. `python3 tools/emulate_load.py` — dumps full startup+loader state to `notes/oracle_objects.json`
4. `python3 tools/build_oracle_catalog.py` — produces `notes/oracle_catalog.json`
5. `python3 tools/make_startup_data.py` — generates `wasm/startup_data.h`
6. `python3 tools/check_oracle.py` — structural regression checks
7. `python3 tools/render_oracle.py 0..18` — regenerate static oracle PNGs
8. `python3 tools/export_level_overlay.py` — generates `out/portable/level_overlay.bin`
9. `wasm: make data` — exports `out/portable/{gfx,levels,cells,palette,mni,commands,objects,level_overlay,screens,shell_text,sound}`

## Level Model (important)

19 loader levels, not a linear game:

- **0/1** — intro/finale story screens (starfield, non-gameplay)
- **2** — main mineshaft/hub with **16 doors** (`kind 60`, handler `3F13`)
- **3–18** — the 16 playable caves; completion order is free

`tools/oracle_frame.py` encodes the loader argument mapping:
`level 0 -> 0xFFFF`, `level 1 -> 0xFFFE`, `level N>=2 -> N-2`.

## Key Conventions & Quirks

- **Handler offsets**: entry addresses are segment offsets; load offset = `0x1F9A0 + offset` (`notes/handlers.md`)
- **Load offset**: `file_offset = load_offset + 0x1E00`; game runs `CS=0`, `DS=0x2CD3`
- **Level boundaries**: not a data table — unrolled `load_map_row` calls in `setup_level` at `seg000:B1A9`
- **Tile codes**: `0x5B` = extended object (next byte = type); `0x6E` often companions it
- **Rows shared between maps** (usually first row); L7/L8/L14 have 23 rows
- **16-colour EGA**: 320x200 playfield; full level is 640x400 (40x25 cells of 16x16)
- **GFX**: 23 blocks x 8003 B; shape = 16 rows x 2 groups x 5 B `[mask][p0..p3]`; mask=opaque, colour = p0 + 2*p1 + 4*p2 + 8*p3, MSB=leftmost; indices are 1-based
- **Cell record**: 20 B = 10 words; `w2/w4` layer1 shape/block, `w3/w5` layer2, `w7/w6` layer3; byte `0x11`=solid, `0x13`=exit. Do not discard the other words at runtime — handlers mutate cells.
- **MNI**: ProGraphx 8x8 font/sprite; `CC1-F1/F2/SPL` are 50-glyph banks of 40 B after a 3-byte header; lowercase text is shifted by -6 in the original font encoding
- **Sound**: Crystal Caves Sound, 3 banks × 12 records × 610 B; driver is `0x1F9A:0x021F`, INT 1Ch ISR `0x0A3F` at 140.06 Hz, PIT divisor `1193181/Hz`, gate field `+604`; details in `notes/sound.md`
- **Borland Turbo Pascal** binary; TP runtime helpers live in segment `0x2BB5`
- **Normal frame order**: `0xE011 → 0xE36B → 0xE5D3 → 0xEBF2 → 0xEAEA → 0xD72A`.
- **Player fire** is `0xCF6C`, called from `0xEBF2`. The attack is object slot 2
  (`DS:0x4638`); `0x4652=p1a` selects normal/power, `0x4660=ready/hit lock`.
- **TP RNG** is at `0x2BB5:0x980`, state `DS:0x2A6/0x2A8`. The high word uses
  the original byte-wise add/shift sequence, not a plain 32-bit multiply.
- **Cell addressing**: `0x508C + (col+1)*500 + (row+1)*20`; slash/dug-block
  paths use the original +1 border indices (see `cell_border_*` in `objects.c`).
- **Cell events**: handler is `0x0FC5:0xBB26` (linear `0x1B776`).  Layer
  mapping is `(w4,w2)`, `(w5,w3)`, `(w6,w7)`; crystal cells are `w6=0xD`,
  `w7=1..4`.  Counters: `0x509E` health, `0x509C` ammo, `0x50A2` collected,
  `0xA0CC` remaining.  Cave flags live at `0x50A9..0x50B8`.
- **Level args**: 0=hub, 1..16=caves, 0xFFFF=intro, 0xFFFE=finale.

## Current Status (do not overstate)

- Static level rendering is **bit-exact on all 19 levels** (Python oracle + wasm).
- Loader/object snapshots and structural oracle checks are done.
- The 0xD72A update loop and **all 46 handler-table entries** are ported
  (including the dynamic `0x4EEB` phase). `notes/handler_fixtures.json`
  covers 235 scenarios / 376 frames; `make test-handlers` passes.
- Full mutable 20-byte cells are exported as `out/portable/cells.bin` and
  verified byte-for-byte by `test_oracle_objects.js`.
- All 16 caves pass committed full-frame integration fixtures
  (`notes/cave_fixtures.json`, 370 frames; `make test-caves`).
- Exact player/camera physics is validated frame-for-frame: 16 scenarios /
  670 frames. The vertical table is `DS:0xA111` indexed by `[0x5134]`; one
  game frame is 8 PIT ticks at divisor `0x2147` ≈ 17.51 Hz.
- Phase 2 combat/fire/slash/death state is oracle-exact (74 scenarios / 624
  frames in `notes/combat_fixtures.json`).
- Phase 3 cell/contact/exit state is implemented: `0xE5D3` contact,
  `0x0FC5:0xBB26` cell events, crystal/ammo/health counters, exit unlock,
  hub-door and cave-exit state machines, and dynamic cell rendering.
  `notes/cell_fixtures.json` covers direct cell branches plus full-frame
  contact/exit runs (17 scenarios).
- Transitions replay the exact loader occupancy overlay
  (`out/portable/level_overlay.bin`) generated from the real loader, including
  preserved source bytes, `[0x2d6]`-dependent shapes and RNG-selected spawn
  fields.  `notes/loader_fixtures.json` / `make test-loader` pass for
  hub -> each cave and every cave -> hub, including the hub's two RNG-driven
  background-decoration recipes and flag-driven door cells.
- Phase 5 is implemented: title/credits/title flow, main menu, PCX screens,
  oracle-exact text windows (`notes/text_fixtures.json`), oracle-exact HUD
  (`notes/hud_fixtures.json`), story/finale transitions, help/pause/quit/warp,
  save/restore 0–9, high scores and config persistence in `localStorage`.
  `make test-hud` / `make test-text` / `make test-messages` / `make test-shell`
  / `make test-exactness` / `make test-keymap` pass.
- P5.R5 exactness verification is implemented: PIT-tick screen timing,
  captured intro/finale page order, original high-score defaults/sort/tie and
  20-character name entry, DOS scan-code key redefine, and browser
  `KeyboardEvent.code` → set-1 scan-code mapping.
- Phase 7 is implemented: the save snapshot is `CCS2` v2 with a CRC and
  atomic validation, v1 `CCS1` slots migrate, and `wasm/persist.js` validates
  config/high-score/slot data before touching wasm state.  Mid-hub and
  mid-cave round-trips (including subsequent-frame determinism), corrupt/
  oversized slots, high-score reload and config/key-binding reload are
  covered by `notes/persist.md` / `make test-persist`.  The original hub-only
  save restriction, `CC1.HS` defaults/sort/tie rules and `CC1.CFG` fields are
  preserved; see `notes/persist.md` for the original file layouts.
- Known shell limitations: joystick uses the Gamepad API; the original
  blinking input cursor is not drawn in shell text windows; title scroll is
  approximated from the original tick-driven sequence.
- Phase 8 is implemented: `make test-all` covers the full committed-fixture
  suite with an unknown-handler guard, `make test-browser` runs
  title → hub → save/restore → finale in headless Chromium via real key
  events, `.github/workflows/ci.yml` builds/tests on Ubuntu, `make perf`
  reports wasm size and frame cost (~1.2 ms per level frame against the
  57.1 ms budget), and the build uses `-Oz` (~152 KB vs ~180 KB at `-O2`).
  `notes/regression.md` maps every subsystem to its fixture and target; the
  root `README.md` documents build/run/test.
- Phase 6 sound is implemented: all 36 Crystal Caves Sound effects are
  trace-captured from the real `0x1FBBF`/`0x0A3F` driver, rendered by
  `wasm/sound_synth.js`, and played through a single WebAudio channel.
  Priority/preemption, `[0x513E]` toggle semantics, shell page/high-score/
  ending effects, the level-start effect and the ID-26 finale wait are wired.
  `notes/sound_fixtures.json` has 36 exact tick traces; `make test-sound`
  passes.  Remaining coverage audit: IDs 2 and 7 have no C gameplay trigger
  (ID 7's static site is `0xF74C/0xF757`); full title/menu shell sound flow is
  still statically mapped rather than oracle-driven end-to-end.

## Tooling Entrypoints

| Tool | Purpose |
|---|---|
| `tools/emulate_startup.py` | Run real `0xC3E` + loader; dump startup/level state |
| `tools/make_startup_data.py` | Generate `wasm/startup_data.h` from `notes/startup.json` |
| `tools/emulate_load.py` | Run `0xC3E` + real loader `0xB1A9`; dump objects/cells |
| `tools/oracle_physics.py` | Exact player/camera/platform physics oracle |
| `tools/physics_fixtures.py` | Generate `notes/physics_fixtures.json` |
| `tools/combat_fixtures.py` | Generate `notes/combat_fixtures.json` (fire, slash, touch, death, RNG) |
| `tools/cell_fixtures.py` | Generate `notes/cell_fixtures.json` (cell events, contact, exits) |
| `tools/emulate_render.py` | Full EGA render oracle (real blitters + ports) |
| `tools/oracle_frame.py` | Low-level physics/collision sub-routine oracle |
| `tools/emulate_game.py` | Full update-loop + handler oracle (video/sound stubbed) |
| `tools/handler_fixtures.py` / `notes/handler_fixtures.json` | Per-handler oracle fixtures (`make test-handlers`) |
| `tools/loader_fixtures.py` / `notes/loader_fixtures.json` | Exact transition fixtures (`make test-loader`) |
| `tools/export_level_overlay.py` / `out/portable/level_overlay.bin` | Loader occupancy overlay asset (v2: decor + door recipes) |
| `tools/analyze_loader_tags.py` / `notes/loader_tags.json` | Regenerate source-dependent loader tags |
| `tools/cave_fixtures.py` / `notes/cave_fixtures.json` | All-cave full-frame fixtures (`make test-caves`) |
| `tools/trace_handlers.py` | Handler inventory (`notes/handlers.md/json/disasm`) |
| `tools/disasm.py` | 16-bit disassembler/xref helper (needs capstone) |
| `tools/export_assets.py` | Portable level/GFX/font asset export |
| `tools/export_shell_assets.py` | PCX screens, MNI banks, shell text/key/high-score export |
| `tools/oracle_shell.py` | Real HUD + text-window oracle (`notes/hud_fixtures.json`, `notes/text_fixtures.json`) |
| `tools/shell_exactness.py` | P5.R5 shell exactness capture (`notes/shell_exactness_fixtures.json`, high-score/keymap fixtures) |
| `tools/make_shell_exact_data.py` | Generate `wasm/shell_exact_data.h` from exactness fixtures |
| `tools/sound_banks.py` | Parse CC1-*.SND and build the tick-trace model |
| `tools/sound_fixtures.py` | Unicorn capture of the real sound driver/ISR (`notes/sound_fixtures.json`) |
| `tools/export_sound.py` | Export `sound.json`, `sound_data.h` and synth hashes |
| `tools/check_oracle.py` | Structural oracle validation |
| `tools/build_cells.py` / `make_cells_data.py` | Cell extraction/header generation |
| `tools/build_oracle_catalog.py` / `make_objects_data.py` | Object catalog/header generation |
| `tools/decode_cmd.py` / `decode_special.py` | Map command table decoding |
| `tools/pkware.py` / `unlzexe.py` | Archive/EXE decompression |

## Outputs

- `out/portable/gfx.bin`, `levels.bin`, `cells.bin`, `palette.json`, `mni_f*.bin`, `screens.bin`, `shell_text.json`, `sound.json`, `commands.json`, `objects.json`
- `out/emu_L*.png`, `out/oracle_L*.png` — reference renders (oracle renders come from startup+loader cells)
- `wasm/render.wasm` — build artifact (gitignored)
- `wasm/startup_data.h` — generated startup/table/level-start data
- `notes/startup.json`, `notes/physics_fixtures.json`, `notes/physics.md`
- `notes/combat_fixtures.json` — committed combat oracle trace (`make test-combat`)
- `notes/handler_fixtures.json` — committed per-handler trace (`make test-handlers`)
- `notes/loader_fixtures.json` — committed transition trace (`make test-loader`)
- `notes/text_fixtures.json` — committed text-window oracle trace (`make test-text`)
- `notes/shell_exactness_fixtures.json`, `notes/hiscore_fixtures.json`,
  `notes/keymap_fixtures.json` — P5.R5 shell exactness fixtures
  (`make test-exactness`, `make test-oracle-exactness`)
- `notes/cave_fixtures.json` — committed all-cave full-frame trace (`make test-caves`)
- `out/portable/level_overlay.bin` — loader transition occupancy asset
- `notes/cell_fixtures.json` — committed cell/contact/exit oracle trace (`make test-cells`)
- `notes/handlers.*`, `notes/oracle_catalog.json`, `notes/oracle_objects.json`, `notes/level_table.json`, `notes/gameplay.md`
- `notes/shell.md` — Phase 5 HUD/text/menu/save notes
- `notes/persist.md` — Phase 7 save format/validation and original CC1.HS/CC1.CFG/save-record layouts
- `notes/persist_v1_fixture.json` — real Phase 5 `CCS1` save for migration regression
- `wasm/persist.js` — browser localStorage config/high-score/slot adapter
- `wasm/test_persist.js` — Phase 7 persistence regression
- `notes/sound_fixtures.json` — committed 36-ID sound-driver trace (`make test-sound`)
- `notes/sound_synth_fixtures.json` — Python-reference JS synth hashes
- `notes/sound_shell_fixtures.json` — recovered shell sound call sites
- `notes/sound.md` — Phase 6 driver/format/browser notes
- `wasm/game.html` — full shell page
- `wasm/text.c/h`, `wasm/shell.c/h`, `wasm/render.h` — Phase 5 code
- `wasm/sound.c/h`, `wasm/sound_data.h`, `wasm/sound_synth.js`, `wasm/audio.js` — Phase 6 sound
- `wasm/test_browser.js` / `wasm/browser_expectations.json` — Phase 8 headless browser smoke
- `wasm/measure_perf.js` / `wasm/perf_baseline.json` — Phase 8 frame-budget/size checks
- `notes/regression.md` — Phase 8 coverage matrix (target → fixture → harness)
- `README.md` — build, run, controls, test and oracle-regeneration guide

## Known Constraints

- No `pip` project or `cargo`; the game build is Python + clang/emcc only.
  npm exists solely for the optional Playwright browser test
  (`wasm/package.json`).
- Static levels are baked from oracle dumps; `cells.bin` carries mutable 20-byte records, dynamic redraw is implemented, and the HUD/text windows are oracle-exact.
- Oracle catalog is the regression reference; never hand-edit generated headers
- Level layout is code-encoded — changing map heights requires editing binary/code assumptions
