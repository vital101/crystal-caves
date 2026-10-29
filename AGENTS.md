# Crystal Caves Decompilation — AGENTS.md

## Build & Test (run from `wasm/`)

There is no root Makefile; the build lives in `wasm/`.

| Command | Description |
|---|---|
| `make clang` | Build freestanding wasm via Homebrew LLVM (default) |
| `make emcc` | Emscripten build (needs `emcc` on PATH) |
| `make test-all` | Data checks + all-19 pixel-exact render + object/gameplay/physics smoke tests |
| `make test-render-all` | Pixel-diff every level against `out/oracle_L*.png` |
| `make test-data` | Run `../tools/check_oracle.py` (structural oracle assertions) |
| `make test-view` | 320x200 viewport crop regression |
| `make test-objects` | Object init/input/collision smoke test |
| `make test-gameplay` | Level-2 fixture smoke test |
| `make test-physics` | Exact player/camera/platform-carry regression (committed fixtures) |
| `make test-combat` | Committed combat/cell/globals/RNG/sound oracle fixtures (fast) |
| `make test-oracle-physics` | Regenerate physics fixtures from the real routines, then compare (needs venv deps) |
| `make test-oracle-objects` | Compare C object/cell state with the Unicorn oracle, levels 2 and 3 (needs venv deps) |
| `make test-oracle-combat` | Regenerate `notes/combat_fixtures.json` from the real routines and compare (needs venv deps) |
| `make startup` | Regenerate `notes/startup.json` + `wasm/startup_data.h` (needs venv deps) |
| `make assets` | Copy portable assets from `../out/portable/` into `wasm/assets/` |
| `make data` | Regenerate `wasm/startup_data.h` + `out/portable/` |

**Order matters:** `assets` must exist before `clang`/`emcc`. `make clang` creates
`assets/` automatically. `test-data` needs no wasm build.

**Toolchain:** Homebrew LLVM **and lld** (`brew install llvm lld`); the Makefile
uses `brew --prefix llvm` or pass `LLVM=/path/to/bin`. Python oracle tools need
`tools/requirements.txt` (`capstone`, `unicorn`); use `../.venv/bin/python` (the
Makefile auto-detects it) or install system-wide.

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
8. `wasm: make data` — exports `out/portable/{gfx,levels,cells,palette,mni,commands,objects}`

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
- **MNI**: ProGraphx 8x8 font/sprite; `CC1-F1/F2` are 50-glyph banks
- **Sound**: Crystal Caves Sound (modified inverse-frequency, PC speaker); not decoded yet
- **Borland Turbo Pascal** binary; TP runtime helpers live in segment `0x2BB5`
- **Normal frame order**: `0xE011 → 0xE36B → 0xEBF2 → 0xEAEA → 0xD72A`;
  `0xE5D3` hazard contact remains deferred.
- **Player fire** is `0xCF6C`, called from `0xEBF2`. The attack is object slot 2
  (`DS:0x4638`); `0x4652=p1a` selects normal/power, `0x4660=ready/hit lock`.
- **TP RNG** is at `0x2BB5:0x980`, state `DS:0x2A6/0x2A8`. The high word uses
  the original byte-wise add/shift sequence, not a plain 32-bit multiply.
- **Cell addressing**: `0x508C + (col+1)*500 + (row+1)*20`; slash/dug-block
  paths use the original +1 border indices (see `cell_border_*` in `objects.c`).

## Current Status (do not overstate)

- Static level rendering is **bit-exact on all 19 levels** (Python oracle + wasm).
- Loader/object snapshots and structural oracle checks are done.
- The 0xD72A update loop and 18 handlers are ported; level-2 object state
  (10 frames) and level-3 object state (120 frames) match the Unicorn oracle.
- Full mutable 20-byte cells are exported as `out/portable/cells.bin` and
  verified byte-for-byte by `test_oracle_objects.js`. The export now includes
  the `0xC3E` startup state (notably level-2 hub door IDs 1..16).
- Exact player/camera physics is validated frame-for-frame: 16 scenarios /
  670 frames, including level-2/3 platform carry and reversed gravity. The
  vertical table is `DS:0xA111` indexed by `[0x5134]`; one game frame is
  8 PIT ticks at divisor `0x2147` ≈ 17.51 Hz. See `notes/physics.md`.
- `0x20406` first interaction block (touch damage, pickups, kind-0x2C
  score/effect) and its 0x831 effect spawn are ported and tested.
- Still missing: 28 handlers, 0x20406 second-hit/projectile/cell-collection
  matrix, hazard/item/fire/exit frame paths, hub door flow, HUD, menus, sound,
  save.
- `test_gameplay.js` still asserts score/crystals/lives/level-complete stay
  zero; replace those assertions as real gameplay lands.

## Tooling Entrypoints

| Tool | Purpose |
|---|---|
| `tools/emulate_startup.py` | Run real `0xC3E` + loader; dump startup/level state |
| `tools/make_startup_data.py` | Generate `wasm/startup_data.h` from `notes/startup.json` |
| `tools/emulate_load.py` | Run `0xC3E` + real loader `0xB1A9`; dump objects/cells |
| `tools/oracle_physics.py` | Exact player/camera/platform physics oracle |
| `tools/physics_fixtures.py` | Generate `notes/physics_fixtures.json` |
| `tools/combat_fixtures.py` | Generate `notes/combat_fixtures.json` (fire, slash, touch, death, RNG) |
| `tools/emulate_render.py` | Full EGA render oracle (real blitters + ports) |
| `tools/oracle_frame.py` | Low-level physics/collision sub-routine oracle |
| `tools/emulate_game.py` | Full update-loop + handler oracle (video/sound stubbed) |
| `tools/trace_handlers.py` | Handler inventory (`notes/handlers.md/json/disasm`) |
| `tools/disasm.py` | 16-bit disassembler/xref helper (needs capstone) |
| `tools/export_assets.py` | Portable asset export |
| `tools/check_oracle.py` | Structural oracle validation |
| `tools/build_cells.py` / `make_cells_data.py` | Cell extraction/header generation |
| `tools/build_oracle_catalog.py` / `make_objects_data.py` | Object catalog/header generation |
| `tools/decode_cmd.py` / `decode_special.py` | Map command table decoding |
| `tools/pkware.py` / `unlzexe.py` | Archive/EXE decompression |

## Outputs

- `out/portable/gfx.bin`, `levels.bin`, `cells.bin`, `palette.json`, `mni_f*.bin`, `commands.json`, `objects.json`
- `out/emu_L*.png`, `out/oracle_L*.png` — reference renders (oracle renders come from startup+loader cells)
- `wasm/render.wasm` — build artifact (gitignored)
- `wasm/startup_data.h` — generated startup/table/level-start data
- `notes/startup.json`, `notes/physics_fixtures.json`, `notes/physics.md`
- `notes/combat_fixtures.json` — committed combat oracle trace (`make test-combat`)
- `notes/handlers.*`, `notes/oracle_catalog.json`, `notes/oracle_objects.json`, `notes/level_table.json`, `notes/gameplay.md`

## Known Constraints

- No `package.json`, `pip` project, or `cargo`; Python + clang/emcc only
- Static levels are baked from oracle dumps; `cells.bin` now carries mutable 20-byte records, but dynamic redraw/HUD are still pending.
- Oracle catalog is the regression reference; never hand-edit generated headers
- Level layout is code-encoded — changing map heights requires editing binary/code assumptions
