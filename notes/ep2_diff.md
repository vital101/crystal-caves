# Episode 2 delta analysis (R0 spike)

Status: **R0 complete.**  Data source: registered Episode 2 "Slugging It Out"
files, committed under `extracted/ep2/` in this private fork (deployment stays
private).

Reproduce:

```sh
python3 tools/unlzexe.py extracted/ep2/CC2.EXE extracted/ep2/CC2-UNLZ.EXE
python3 tools/episode_diff.py extracted/CC1-UNLZ.EXE extracted/ep2/CC2-UNLZ.EXE \
    --assets-a extracted --assets-b extracted/ep2 --json notes/ep2_diff.json
```

`tools/episode_diff.py` masks MZ relocation words, matches 16-byte windows
(minimum run 32 B), maps every Episode-1 landmark, and compares data assets.
`notes/ep2_diff.json` holds the generated run/gap/landmark map.

## TL;DR

Episode 2 is the **same engine with new level and shell *data***, not a new
code base:

- **All 49 handler behaviours are byte-identical.**  The whole 41,438-byte
  block containing the sound driver, the INT 1Ch ISR, the animation/proximity
  helpers and every handler matches contiguously; only the handler *segment*
  moved (`0x1F9A` -> `0x1F72`).  Handler offsets are unchanged.
- **Sound, fonts and graphics are untouched**: all three `SND` banks, all three
  `MNI` banks, all 23 `GFX` blocks, plus `APG` and `CDT`, are byte-identical to
  Episode 1.
- **The level model is unchanged**: the same `setup_level` chain has the same
  17 gameplay arguments (hub + 16 caves) plus intro/finale, i.e. 19 levels, and
  the same object-kind table (kinds 1..0x18, same pointers).
- What changed is level content (map row data, per-level object/decoder
  immediates), the embedded order/shareware notice, the title/ending screens
  (`TTL`/`END`) and a ~600-byte code-constant cluster in the handler segment
  tail.
- Consequence for the port: **no gameplay, physics, combat, handler or sound
  work**.  The effort is data extraction plumbing, shell text/menu content and
  product integration.

## EXE container

| | Episode 1 | Episode 2 |
|---|---:|---:|
| Packed (`*.EXE`) | 73,256 B | 72,858 B |
| Packer | LZEXE 0.91 | LZEXE 0.91 |
| Header | `0x1E00` | `0x1E00` |
| Load module | 184,288 B | 183,296 B |
| Relocations | 1,867 | 1,855 |
| Entry `CS:IP` | `0000:EF9B` | `0000:EF25` |
| Initial `SS:SP` | `370C:4000` | `36CE:7D00` |

## Address and segment map

Relocations (the values differ, the code that loads them is identical):

| What | CC1 | CC2 | Delta |
|---|---|---|---:|
| Data segment `DS` | `0x2CD3` | `0x2C95` | -992 |
| Initial stack `SS` | `0x370C` | `0x36CE` | -992 |
| TP runtime unit (RNG etc.) | `0x2BB5` | `0x2B77` | -992 |
| TP library calls | `0x28B5` | `0x2877` | -992 |
| Object handler segment | `0x1F9A` | `0x1F72` | -640 |
| Cell-event call site | `0x0FC5:0xBAF4` | `0x0FBD:0xB8EB` | -649 linear |

Code layout (load-module offsets; the same delta applies across each run):

| CC1 range | Delta | Contents |
|---|---:|---|
| `0x00000-0x710C` | `0` | Startup stub, DS setup, early code — 28,936 B identical, only 4 B differ (resource-index immediates `0x16E8..0x17A5`) |
| `0x710C-0xB1A9` | mixed | Per-level map row data plus decoder skeleton; every row pointer/immediate moved |
| `0xB1A9-0xCF30` | -123..-118 | `setup_level` bodies (intro/finale + 17 gameplay levels), same instruction skeleton |
| `0xCEFB-0xF8C2` | -118 | Frame code (`0xE011..0xEFCF` region) |
| `0xFC4F-0x115D7` | -128 | 6,536 B contiguous match |
| `0x130CF-0x17980` | -585 | Code |
| `0x18982-0x1D151` | -611/-649 | Code, including the cell-event routine |
| `0x1D1D0-0x273AE` | -640 | **41,438 B contiguous**: sound driver, ISR, anim helpers, proximity test, all handlers |
| `0x274C8-0x27B68` | -640 | 1,696 B |
| `0x28423-0x2CFE0` | -992 | TP runtime/data |

Handler block details:

- `CC1 0x1F9A:0x021F` -> `CC2 0x1F72:0x021F` (sound driver, same offset).
- `CC1 0x1F9A:0x0A3F` -> `CC2 0x1F72:0x0A3F` (INT 1Ch ISR, same offset).
- All 49 offsets in `wasm/game/handlers_table.c` (`0x0831..0x70E4`) map into
  the contiguous identical block.  **`g_handlers[]` needs no change**; only
  code that materialises a handler *segment* (e.g. `handler_seg = 0x1F9A` in
  `cell_event.c`, `combat.c`, `handlers_actors_a.c`) needs an episode constant.

Caveat on "identical": the matcher treats MZ relocations as zeros, so a
matched word can still be a segment value that differs.  Byte percentages are
a lower bound on semantic identity, not an upper one — but every sampled code
"gap" outside the loader decodes as the *same* instruction sequence with a
different row/table pointer immediate, and the loader bodies are visibly the
same code with new row sources and level header values.

Byte-level distribution (matched union = 75.5% of the load module):

| Region | Size | Changed | % |
|---|---:|---:|---:|
| Startup + early code (`0x0-0x710C`) | 28,940 | 4 | 0.0% |
| Map/loader data band (`0x710C-0xB1A9`) | 16,541 | 16,151 | 97.6% |
| Loader bodies (`0xB1A9-0xCF30`) | 7,559 | 6,684 | 88.4% |
| Post-loader code + data (`0xCF30-0x1D1D0`) | 66,208 | 21,515 | 32.5% |
| Handler block (`0x1D1D0-0x273AE`) | 41,438 | 0 | 0.0% |
| Handler-tail cluster (`0x273AE-0x283B2`) | 4,100 | 591 | 14.4% |
| TP runtime/data (`0x283B2-0x2CFE0`) | 19,502 | 231 | 1.2% |

## Level model

`setup_level` dispatch chain (identical in both binaries):

- `arg 0xFFFF`: intro, `arg 0xFFFE`: finale (bodies identical except pointers).
- `arg 0..16`: hub + 16 caves.  Both episodes have exactly these 17 arguments.
- Each gameplay body issues 25 `call load_map_row` sites (row index + far
  source pointer); the decoder is CC1 `0x125F0`, unchanged.
- **Anomaly:** CC2 arg 2 (= level 4, cave 2) issues **27** row calls: one extra
  copy of row 6, a blank-row clear (`0x6F09` source) and one extra row at the
  end, with sources shifted by `0x29` bytes.  All indices stay in `0..24`, so
  the 25-row map buffer is unchanged and the later load of each duplicate wins
  — no variable-height support is needed.  The resulting grid still comes from
  the oracle dump (R2).
- The intro/finale object setup walks kinds `1..0x18` with the same data
  pointers in both episodes — the object catalogue is unchanged.

## Changed content

| Area | Size | Notes |
|---|---:|---|
| Map row data + decoder immediates | ~16 KB | Band `0x710C-0xB1A9`; row sources moved, data replaced |
| Inline decoder sites | scattered | `0xCF30-0x1D1D0` band; same skeleton, new row/table pointers |
| Order/shareware notice | ~0.8 KB | CC1 `"= HOW TO ORDER THE FINAL TWO AMAZING EPI..."` vs CC2 `"@ NOTICE: THIS GAME IS NOT SHAREWARE, FR..."` at `0x18683` |
| Handler-tail cluster | ~591 B | Within CC1 `0x273AE-0x27B68` (segment `0x79F0+`, inside a function at `0x27390`); per-kind constants differ (e.g. `[es:di-6] = 0` -> `= 2`); no direct xref found yet |
| `CC2.TTL` | 18,201 B (vs 18,328) | Title/credits screen bank |
| `CC2.END` | 27,486 B (vs 19,625) | Ending/story bank, ~8 KB more content |

## Data assets

| Asset | CC1 | CC2 | Result |
|---|---:|---:|---|
| `CC*-1/2/3.SND` | 7,320 x3 | 7,320 x3 | **byte-identical** |
| `CC*-F1/F2/SPL.MNI` | 2,048 x3 | 2,048 x3 | **byte-identical** |
| `CC*.GFX` | 184,069 | 184,069 | **byte-identical** (23 blocks x 8,003 B) |
| `CC*.APG` | 16,304 | 16,304 | **byte-identical** |
| `CC*.CDT` | 26,717 | 26,717 | **byte-identical** |
| `CC*.TTL` | 18,328 | 18,201 | differs |
| `CC*.END` | 19,625 | 27,486 | differs |
| `ORDER.FRM`/`VENDOR.DOC` | present | absent | shareware-only files |

Input checksums (SHA-256) for reproducibility:

```
2688aec30dac64fcbfdf071ae725ed008a1cdeafa35a43b94ce5657838b912ff  CC2.EXE
3bb6895d668a61b7438656718f9600bb31548ede4f7d88bbd6d20c1031ac6363  CC2.GFX
72d5ea50fa4d9bedcf86f2a2737e6fcd2f8b360d0cb0306f7f9978cae45a528b  CC2-1.SND
96a11eb084c2ed3c6f6a774779ba9513297d1c7740b0612b096a5605f1018247  CC2.TTL
95d25811c7e52135146ac613521d0854fbc3c313c12d5c65cbd9376701f3c9be  CC2.END
3771282444c47ed875af0e0c788a310014f03e00f09f6d9026573cc8dc8f87bf  CC2-UNLZ.EXE
```

## Consequences for the port plan

Refined phases (the earlier R1-R5 sketch still holds; scope is much smaller):

- **R1 — episode plumbing.**  Add an episode descriptor (EXE path, load
  offsets, DS/SS/segment constants, handler segment, asset prefix) and
  parameterise the oracle tools/generators.  Episode 1's 929 digests must stay
  byte-identical throughout.  Expected code change: small.
- **R2 — data extraction.**  Run the existing startup/loader/game oracle
  against `CC2-UNLZ.EXE` with the mapped addresses; produce `startup.json`,
  object/cell dumps, the loader overlay and exported assets for episode 2.
  Verify static level renders against a CC2 render oracle (renderer and GFX
  are unchanged, so parity follows cell data).
- **R3 — verification and shell.**  Gameplay fixtures should pass with
  Episode-1 expectations unchanged (same code); add CC2 data-dependent
  scenarios (spawns, level 4's extra rows, the handler-tail cluster if
  reachable).  Parameterise the shell screen/text pipeline for `TTL`/`END`,
  the order notice and episode naming; scope saves/high scores per episode.
- **R4 — product.**  Episode selection (`?ep=2`), per-episode asset packs and
  `localStorage` namespaces, native-build episode flag, docs and the browser
  smoke test.
- **R5 — Episode 3.**  Same tool against `CC3-UNLZ.EXE`; expect similar deltas
  but verify the segment constants and level chain first — do not assume.

## Harness validation (R1 first increment)

- `tools/episode_config.py` centralises per-episode constants; `--episode ep2`
  is accepted by `tools/emulate_startup.py` and `tools/emulate_load.py`
  (defaults are unchanged).
- Episode-1 regression: `emulate_startup.py` regenerates `notes/startup.json`
  byte-identically, and `emulate_load.py` level 2 reproduces the committed
  objects/cells/spawns/cursor exactly.
- Episode 2: startup plus all 19 loader runs complete in ~72 s (the same
  runtime as Episode 1) and produce `notes/ep2/startup.json` (committed).
  The vertical displacement table is identical; the hub player spawn (32,64)
  matches, cave 1's spawn differs.
- Object/cell dumps for the hub and cave 1 are sane: every handler offset
  comes from the existing 49-entry dispatch table (no new behaviours); the
  layouts differ substantially (469/1000 and 960/1000 cells, 26->34 and
  31->33 spawns), as expected for new level data.
- Fixed a latent JSON serialization bug in `emulate_load.py` (`tail_ref`
  deque) that blocked oracle-dump regeneration.
- Episode 2 outputs stay under `notes/ep2/` (committed).

## Episode 2 level data appendix

All 19 loader levels run successfully.  Cell counts compare the Episode-2
oracle dump against Episode 1 (1,000 cells per level); the full dumps are committed in
`notes/ep2/oracle_objects.json`.

| Level | Spawns ep1 -> ep2 | Cells changed | Live objects |
|---:|---:|---:|---:|
| 0 (intro) | 5 -> 5 | 0 | 58 |
| 1 (finale) | 5 -> 5 | 0 | 58 |
| 2 (hub) | 26 -> 34 | 469 | 53 |
| 3 | 31 -> 33 | 960 | 60 |
| 4 | 37 -> 32 | 960 | 59 |
| 5 | 33 -> 28 | 960 | 53 |
| 6 | 24 -> 41 | 960 | 56 |
| 7 | 20 -> 43 | 726 | 56 |
| 8 | 25 -> 32 | 960 | 59 |
| 9 | 27 -> 26 | 960 | 56 |
| 10 | 39 -> 16 | 700 | 60 |
| 11 | 21 -> 29 | 960 | 57 |
| 12 | 29 -> 30 | 960 | 58 |
| 13 | 32 -> 33 | 960 | 55 |
| 14 | 20 -> 35 | 960 | 58 |
| 15 | 20 -> 28 | 960 | 60 |
| 16 | 25 -> 41 | 960 | 55 |
| 17 | 37 -> 40 | 960 | 60 |
| 18 | 28 -> 23 | 960 | 60 |

Notes:

- No episode-2 spawn uses a handler offset outside the existing 49-entry
  table; `handler_seg` is `0x1F72` throughout.
- The intro/finale cell maps are identical to Episode 1 (shared story-screen
  background); every cave layout differs.
- Levels 7 and 10 share more cells with Episode 1 (726 / 700 changed) than the
  other caves (960 changed = 24 of 25 rows), consistent with the shared-row
  rule and partly reused layouts.

## R2: extraction and render parity

The asset exporter and render harnesses now take `--episode`:

```sh
python3 tools/export_assets.py --episode ep2        # out/portable_ep2/
python3 tools/render_oracle.py --episode ep2 $(seq 0 18)
python3 tools/emulate_render.py --episode ep2 --startup 2 3
python3 tools/analyze_loader_tags.py --episode ep2  # slow
python3 tools/export_level_overlay.py --episode ep2
```

- **Episode 1 regression:** the files `export_assets.py` owns (palette,
  gfx, fonts, levels, cells, commands) are byte-identical to `out/portable/`;
  `emulate_render.py` reproduces the committed cold-DS `out/emu_L02.png`, and
  the new startup-seeded mode reproduces `out/oracle_L02.png` bit-for-bit.
- **Episode 2 portable set** (`out/portable_ep2/`, committed): `cells.bin`
  380,000 B = 19 x 1,000 x 20, `levels.bin` 114,000 B = 19 x 1,000 x 6 with
  zero cell->level projection mismatches, gfx/fonts/palette identical to
  Episode 1, `objects.json` `handler_segment` = `0x1F72`.  No `commands.json`:
  the decoded command tables are an Episode-1 analysis artifact and are unused
  at runtime.
- **Static render parity:** the real EGA blitter executing CC2
  (`emulate_render.py --episode ep2 --startup`) produces PNGs **bit-identical**
  to the independent Python renderer (`render_oracle.py --episode ep2`) on
  **all 19 levels** (0 warnings, every run ends at the loader's draw-map
  target).  Cave renders also match in cold-DS mode; the hub only matches when
  startup-seeded because its decorations depend on the RNG state the 0xC3E
  initializer establishes (this was already true for Episode 1).
- **The unmodified wasm build already renders Episode 2:** fed with
  `out/portable_ep2/`, the same `render.wasm` reproduces all 19 ep2 oracle
  PNGs **pixel-exactly** (`make -C wasm test-render-ep2`, ~3 s; it skips when
  the data is absent).  No C or wasm changes were needed — the renderer is
  entirely data-driven.
- **Transition overlay:** `analyze_loader_tags.py --episode ep2` classifies every
  source-dependent write (no unclassified entries), and
  `export_level_overlay.py --episode ep2` produces a 487,439-byte version-2
  `CCLO` overlay for all 19 levels.  Its structure matches Episode 1's pattern
  (the hub record has the same 18,991 cell literals, 53 globals, 177 decor
  entries and 16 door entries).  End-to-end transition replay is exercised by
  the R4 wasm tests; both the tags and the overlay are committed
  under `notes/ep2/` and `out/portable_ep2/`.
- `emulate_render.py --startup` is new: it seeds DS from the real 0xC3E
  initializer, mirroring the wasm port's startup+loader sequence.
- **Shell screens:** all four Episode 2 PCX files (`APG`, `TTL`, `CDT`, `END`)
  are single 320x200 4-plane PCX images and decode with the existing
  `decode_pcx` unchanged; only their content (and the `END` RLE size) differs.
  Text-block discovery in `export_shell_assets.py` is Episode-1-specific
  (hardcoded page offsets and menu fixtures) and stays R3 work.

## R4: per-episode build and gameplay validation

The data generators accept `--episode`
(`make_startup_data.py`, `build_oracle_catalog.py`, `make_objects_data.py`,
`make_cells_data.py`), plus the new `make_episode_data.py` for the tiny
cell-rule/segment constants.  `make -C wasm ep2` compiles the same C sources
with `-Iep2_data` and links `render_ep2.wasm` (~161.7 KB); portable assets stay
runtime-loaded.

- **Compiled data:** `wasm/test_ep2_data.js` boots `render_ep2.wasm` with the
  ep2 cells and checks all 19 levels against the oracle: every object record
  (60 slots x 23 fields), player/camera start, cursor, key globals and all
  380,000 cell bytes match.  It also picks up a crystal through the real
  cell-event handler on levels 3 and 14, verifying the counters, score and cell
  clear.
- **Frame stepping:** `emulate_game.py --episode=ep2` runs the real update
  loop, and `test_oracle_objects.js` (now `CC_WASM`/`CC_CELLS` aware) matches
  the oracle for 60 frames on level 3, 60 on level 14 and **120 frames on
  level 10 with fire input**.
- **Two engine-data deltas handled:**
  - the crystal pickup's layer-3 shape range differs (ep1 `w7 = 1..4`, ep2
    `w7 = 0x0D..0x10`), recovered from disassembling both cell-event handlers;
    `episode_data.h` supplies `CC_EP_CRYSTAL_W7_LO/HI` and the crystal count
    now matches the loader's `a0cc` exactly;
  - runtime spawns store `handler_seg` = `0x1F72` in ep2 (`CC_EP_HANDLER_SEG`),
    not the hard-coded `0x1F9A`.
- **Latent Episode 1 bug found by the ep2 fixtures:** the formation mover
  (`0x1F9A:0x36B0` at `0x2315D`) must restore `speed = 8` when it reverses to
  `+1`; the port only handled the `-1` case.  Fixed.  The 929 Phase-0 digests
  changed only for the `gameplay.level04` frames that had recorded the bug and
  were regenerated with `make test-digest-update` (documented behaviour fix);
  the oracle-exact suites (caves/combat/handlers) and `make test-all` pass.

## R3/R4: shell content and browser episode selection

- **Shell exactness capture** is parameterized (`tools/shell_exactness.py
  --episode ep2`): the shared page renderer is hooked once, and per-episode
  menu/story/high-score/key-name addresses come from `episode_config`.  The
  order-based capture reproduces the committed Episode-1 menu fixture
  byte-identically; the Episode-2 capture found the real content deltas:
  intro is 4 pages starting `SHIP'S LOG - STARDATE 2121.47`, finale is 4 pages
  with the new epilogue, high-score defaults are "Play all 3 episodes / Call
  1-800-852-5659 ...", and the video page has a one-space change.  Every other
  menu/info page is identical.
- `export_shell_assets.py --episode` emits Episode-2 `screens.bin`
  (`out/portable_ep2/`) plus `shell_text_data.h`; `make_shell_exact_data.py
  --episode` emits `shell_exact_data.h`.  The ep2 build compiles them from
  `wasm/ep2_data/`.
- **Browser product:** `game.html?ep=2` loads `render_ep2.wasm` and
  `assets_ep2/` (built by `make -C wasm ep2-assets`), namespaces storage as
  `cc2.config.v2` / `cc2.hiscores.v1` / `cc2.save.<slot>`, and offers an
  episode switch button.  Episode-1 keys are untouched.
- **Tests:** `make -C wasm test-ep2` runs the 19-level static render check, the
  compiled-data oracle check and the browser smoke
  (`wasm/test_ep2_browser.js`): title -> attract -> menu -> intro (4 pages,
  2121.47 text) -> hub (32,64) -> save/restore under `cc2.*` -> config and
  high-score reload -> finale (4 pages) -> ending, with framebuffer hashes in
  `wasm/browser_expectations_ep2.json`.  All targets skip cleanly without the
  data; Episode-1 browser/digest suites still pass.
- **Native:** CMake builds `caves_ep2` from the same core compiled against
  `wasm/ep2_data` with `out/portable_ep2` embedded; config/high-score/save
  files use `cc2.*` names and the window title is episode-specific.
  `make -C native test-smoke-ep2` runs the headless SDL smoke (the target is
  skipped at configure time when the data is absent).
- **Transitions:** `tools/loader_fixtures.py --episode ep2` captured all 32
  poisoned hub<->cave transitions from the real loader; the C overlay replay
  (`test_loader.js` with `CC_WASM`/`CC_CELLS`/`CC_OVERLAY`, wired as
  `make test-ep2-transitions`) matches every cell, object, global, cursor and
  RNG value.  The fixture is committed under `notes/ep2/` (13 min Unicorn
  regeneration).

## Episode 3 (blocked: no data supplied)

Episode 3 data has been supplied and is staged under `extracted/ep3/`
(committed in this private fork).  The pipeline is the same as Episode 2:

1. Stage the registered Episode 3 files under `extracted/ep3/`.
2. `python3 tools/unlzexe.py extracted/ep3/CC3.EXE extracted/ep3/CC3-UNLZ.EXE`
3. `python3 tools/episode_diff.py extracted/CC1-UNLZ.EXE
   extracted/ep3/CC3-UNLZ.EXE --assets-a extracted --assets-b extracted/ep3
   --json notes/ep3_diff.json` (the tool is episode-agnostic) and inspect the
   delta report.
4. Add an `ep3` entry to `tools/episode_config.py` from the run map: DS/loader/
   handler/TP segments, frame-routine addresses, shell addresses, handler
   segment and the crystal `w7` range (re-derive the cell-event rule, do not
   assume Episode 2's).
5. Follow R1-R4: `emulate_startup/load/game --episode ep3`, the asset/shell
   exporters with `--episode ep3`, `make_episode_data.py --episode ep3`,
   `make -C wasm ep3` and the corresponding tests.

## Open questions

1. ~~Identify the `0x273AE` handler-tail cluster~~ — resolved: the function at
   CC1 `0x27390` initializes a 6-byte-stride cell template (three words per
   record; CC2 changes the first word from 0 to 2).  No near call, near jump,
   far call or far-pointer table references it anywhere in either image, and
   it is outside the 40x25 visible map, so it is linked-but-unreachable
   procedure code (or border-only initialization) and has no handler role.
2. ~~Determine cave 2's effective grid dimensions~~ — resolved: the extra row
   loads stay within indices `0..24` and overwrite earlier loads, so the map
   stays 25 rows.
3. ~~Confirm `TTL`/`END` share Episode 1's screen-bank format~~ — resolved: all
   four screens are single 320x200 4-plane PCX images and decode with the
   existing `decode_pcx`; the text-block discovery in
   `tools/export_shell_assets.py` is Episode-1-specific and needs R3 work.
4. Find which shell code renders the order/shareware notice and episode name,
   and whether menu code changed at all (large regions match; check the small
   code gaps with a CC2 fixture).
5. Confirm `CC2.EXE` authenticity against a second source before baking
   fixtures (checksums above are of the staged copy).
6. Decide the fidelity policy for `handler_seg` in CC2 fixtures/saves
   (`0x1F72` vs the C port's hard-coded `0x1F9A`).

## Data policy

`extracted/ep2/` (and `extracted/ep3/`) and their derived fixtures are
committed in this private fork; keep the deployment private and the public
deployment Episode-1-only.
