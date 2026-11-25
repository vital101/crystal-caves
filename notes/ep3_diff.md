# Episode 3 delta analysis and port (R0-R4)

Status: **complete.**  Data source: registered Episode 3 "Mylo vs. the
Supernova" (`cc3.exe` + assets, 1996 build), committed under `extracted/ep3/`
in this private fork (deployment stays private).

Staged file set (1996 `cc3-*.sav`, `cc3.hs`, `cc3.cfg` ignored): `CC3.EXE`,
`CC3.GFX`, `CC3-1/2/3.SND`, `CC3-F1/F2/SPL.MNI`, `CC3.TTL`, `CC3.APG`,
`CC3.CDT`, `CC3-END1/2/3.END`.

## R0: staging and delta analysis

```sh
python3 tools/unlzexe.py extracted/ep3/CC3.EXE extracted/ep3/CC3-UNLZ.EXE
python3 tools/episode_diff.py extracted/CC1-UNLZ.EXE extracted/ep3/CC3-UNLZ.EXE \
    --assets-a extracted --assets-b extracted/ep3 --json notes/ep3_diff.json
```

The unpacked module is **189,472 bytes** (header `0x2000`, 1969 relocs) versus
Episode 1's 184,288 (`0x1E00`).  The relink matches **74.69 %** in 547 runs —
same class of delta as Episode 2, with real code additions.

Landmark map (Episode-1 -> Episode-3 load offsets):

| Landmark | ep1 | ep3 | Delta |
|---|---|---|---|
| startup `0xC3E` | `0xC3E` | `0xC3E` | 0 (identical) |
| loader `setup_level` | `0xB1A9` | `0xB0CE` | -219 |
| draw-map target (`load_end`) | `0xCEEC` | `0xCD05` | -487 |
| player fire | `0xCF6C` | `0xCD85` | -487 |
| object update loop | `0xD72A` | `0xD543` | -487 |
| frame steps (`horiz/jump/contact/status`) | `0xE011/...` | `0xDE2A/0xE184/0xE3EC/0xE903` | -487 |
| cell event | `0x1B776` | `0x1CE07` | +5777 |
| sound driver | `0x1F9A:0x021F` | `0x2104:0x021F` | +0x16A paragraphs |

* **Data segment** `0x2E17` (TP runtime prologue `mov dx,0x2E17; mov ds,dx`);
  stack `0x3850:0x7D00` from the MZ header.
* **Handler segment is `0x2104`** with Episode-1 offsets unchanged — the
  whole driver+handler block matches Episode 1 byte-for-byte at +0x16A0 within
  the segment (the diff tool renormalises it to `0x1F9A:offset+0x16A0`).
* Segments: TP runtime `0x2CF9`, blitter `0x29F9`, draw `0x282F`, shell/cell
  event `0x0FA6`.  RNG/spawn offsets inside the runtime segment are unchanged.
* The `horiz`/reload-clear markers flagged "moved/edited" are relocation words
  only; both routines are instruction-identical to Episode 1.
* New code sits before the sound driver (~5.8 KB) and after the handlers
  (~5.2 KB) in segment `0x1F9A`; the shell segment gained the ending sequence.

### Assets

`CC3-1/2/3.SND`, `CC3-F1/F2/SPL.MNI`, `CC3.GFX`, `CC3.APG` and `CC3.CDT` are
**byte-identical to Episode 1** (sha256).  `CC3.TTL` is new title art
(18,493 B vs 18,328 B).  `CC3.END` does not exist; the ending is split into
three PCX screens.

### The three-screen ending

`CC3-UNLZ.EXE` contains the Pascal string table
`cc3-end1.end` / `cc3-end2.end` / `cc3-end3.end`.  The ending routine loads
each screen in order (lcall `0x1ED8:0x1915`), waits for a key after each, and
then goes to the high-score flow.  Episode 1/2 show a single `CCx.END`; the port
parameterises the count in `episode_data.h` (`CC_EP_ENDING_SCREENS`).

### Crystal rule

The cell-event routine compares `w7` (`[di+0x509A]`) against `0x21`, `0x22`,
`0x24`, `0x23` with `w6 == 0x0D` — Episode 3 crystals are **`w7 = 0x21..0x24`**
(data confirms: 263/301/325/315 cells).  Every other pickup compare is
identical to Episode 1.

### Fixed hardcodes exposed by Episode 3

* `emulate_load.py` hardcoded the MZ header size `0x1E00`; it is now derived
  from the EXE header (`ep3` is `0x2000`).  `episode_config.apply()` propagates
  it.  This shifted the whole load image by 0x200 and produced garbage key
  names until fixed.
* `shell_exactness.py` sliced its own `MODULE` copy at `0x1E00`; it now uses
  `E.load_module()`.
* `export_shell_assets.py` bounded the instruction-page run at the ep1 offset
  `0x14908`; it now stops at the next sentinel-terminated block.
* The screen list is per episode: ep1/ep2 `(APG, TTL, CDT, END)`, ep3
  `(APG, TTL, CDT, END1, END2, END3)`.

## R1: config and oracle captures

`tools/episode_config.py` gained the `ep3` entry (all values above plus the
shell block).  Captures:

```sh
.venv/bin/python tools/emulate_startup.py --episode ep3 --out notes/ep3/startup.json
.venv/bin/python tools/emulate_load.py --episode ep3 --out notes/ep3/oracle_objects.json
.venv/bin/python tools/build_oracle_catalog.py --episode ep3
```

All 19 levels run to `load_end`; the vertical displacement table is identical
to Episode 1, the hub spawn is `(32,64)` and cave spawns differ.

## R2: export, build, render parity

```sh
python3 tools/export_assets.py --episode ep3
python3 tools/render_oracle.py --episode ep3 $(seq 0 18)
.venv/bin/python tools/analyze_loader_tags.py --episode ep3   # ~7 min
.venv/bin/python tools/export_level_overlay.py --episode ep3  # ~8 min, 486,990 B
make -C wasm ep3 ep3-assets
```

* The unmodified wasm renders all 19 levels **pixel-exactly**
  (`make test-render-ep3`).
* The ep3 build reproduces the loader's objects, start state, cursor, cells
  and globals for all 19 levels (`CC_EP=ep3 node test_ep2_data.js`), including
  a real crystal pickup through the cell-event handler.
* Sound banks are identical, so the portable set reuses Episode 1 `sound.json`.

## R3: shell content

`tools/shell_exactness.py --episode ep3 [--menus]` produced
`notes/ep3/{menu,shell_exactness,hiscore,keymap}_fixtures.json`:

* Intro: 4 pages, `SHIP'S LOG - STARDATE 2121.93` + `GALACTIC ENCYCLOPEDIA`.
* Finale: **12 pages** across 8 routines (vacation purchase -> Rip's ship ->
  the supernova -> two weeks/six months later -> Mylo Burger -> congratulations).
* High-score defaults: `Play all 3 episodes / of Crystal Caves. / Call
  1-800-852-5659 / and use Visa or MC.` (same as Episode 2).
* 50 named keys; key table bytes identical to Episode 1.

`export_shell_assets.py --episode ep3` writes the six-screen `screens.bin`
(384,000 B) and `shell_text_data.h`; `make_shell_exact_data.py --episode ep3`
writes `shell_exact_data.h` (intro/finale pages, ending/hiscore flow).

Port change: `shell/state.c`, `shell/render.c` and `shell/input.c` now advance
through `CC_EP_ENDING_SCREENS` ending screens (one key press each) before the
name-entry/high-score flow.  Episodes 1/2 keep a single screen and unchanged
behaviour (929 digests unaffected).

## R4: product and tests

* Browser: `game.html?ep=3` loads `render_ep3.wasm` + `assets_ep3/`,
  namespaces storage as `cc3.config.v2` / `cc3.hiscores.v1` / `cc3.save.*`,
  and the episode button cycles 1 -> 2 -> 3 -> 1.
* `wasm/test_ep3_browser.js` covers title -> intro (2121.93) -> hub (32,64) ->
  save/restore under `cc3.*` -> config/high-score reload -> finale (12 pages)
  -> three ending screens -> navigation, with hashes in
  `wasm/browser_expectations_ep3.json`.
* Native: `caves_ep3` (same core, plane title "Mylo vs. the Supernova",
  `cc3.*` files).  `make -C native test-smoke-ep3` passes with a distinct
  framebuffer hash (`5c71f9ca` vs ep1 `fe4bd8c8`, ep2 `e768dc28`).
* Transitions: `tools/loader_fixtures.py --episode ep3` captured all 32
  poisoned hub<->cave transitions; the C overlay replay matches every cell,
  object, global, cursor and RNG value (`make test-ep3-transitions`).
* `make -C wasm test-ep3` runs render, compiled data, transitions and browser;
  it skips cleanly when the data is absent.  Episode 1 `test-all`/digests and
  the Episode 2 suite still pass unchanged.
