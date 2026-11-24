# Episode 2 delta analysis (R0 spike)

Status: **R0 complete.**  Data source: registered Episode 2 "Slugging It Out"
files supplied by the developer (bring-your-own-data; staged in
`extracted/ep2/`, which is git-ignored — never commit registered data).

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
  end, with sources shifted by `0x29` bytes.  The resulting grid must come from
  the oracle dump (R2), not from a fixed 25-row assumption.
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

## Open questions

1. Identify the `0x273AE` handler-tail cluster (function at CC1 `0x27390`) and
   whether it is reachable; capture a fixture if so.
2. Determine cave 2's effective grid dimensions from the CC2 oracle run
   (27 row loads, duplicate/clear/extra row).
3. Confirm `TTL`/`END` share Episode 1's screen-bank format (generalise
   `tools/export_shell_assets.py`).
4. Find which shell code renders the order/shareware notice and episode name,
   and whether menu code changed at all (large regions match; check the small
   code gaps with a CC2 fixture).
5. Confirm `CC2.EXE` authenticity against a second source before baking
   fixtures (checksums above are of the staged copy).
6. Decide the fidelity policy for `handler_seg` in CC2 fixtures/saves
   (`0x1F72` vs the C port's hard-coded `0x1F9A`).

## Data policy

`extracted/ep2/` (and `extracted/ep3/`) are git-ignored.  Registered episode
data has no shareware redistribution grant; only structural facts (offsets,
sizes, hashes) are committed here.  Derived fixtures may need the same
bring-your-own-data treatment — decide before R3.
