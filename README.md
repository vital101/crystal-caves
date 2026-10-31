# Crystal Caves Episode 1 — C/WebAssembly port

A clean-room, decompilation-driven port of the shareware **Crystal Caves
Episode 1** to freestanding C and WebAssembly, playable in the browser.

The goal is fidelity, not a remake:

- static level rendering is **pixel-exact on all 19 levels**;
- player physics, combat, cell events, handlers and level transitions are
  verified **frame-for-frame** against the original EXE running under a
  Unicorn-based oracle;
- the HUD, text windows, shell flow, high scores, save format, key handling
  and all 36 sound effects follow the original routines;
- saves, high scores, key bindings and config use `localStorage`.

The original game binary and shareware data are not downloadable here.
`extracted/` and `out/portable/` are committed, so a checkout builds and runs
without the original archive.  Crystal Caves is © Apogee Entertainment; this
repository is a research/decompilation project.

## Requirements

| Purpose | Requirements |
|---|---|
| Build | Homebrew LLVM **and lld** on macOS (`brew install llvm lld`), or `clang` + `lld` on Linux |
| Tests | Node.js 18+ and Python 3.9+ (stdlib only) |
| Oracle regeneration | Python venv with `tools/requirements.txt` (`capstone`, `unicorn`) |
| Browser smoke test | npm + `playwright` (test-only; see `wasm/package.json`) |

The Emscripten build (`make emcc`) additionally needs `emcc` on `PATH`; it is a
secondary back-end and is not exercised by CI.

## Build

```sh
cd wasm
make              # same as: make clang
```

This copies the portable assets from `../out/portable/` into `wasm/assets/`
and produces `wasm/render.wasm` (about 152 KB, `-Oz`).

## Run

Serve the `wasm/` directory over HTTP (the page fetches the wasm module and
assets, so `file://` will not work):

```sh
python3 -m http.server -d wasm 8080
# open http://localhost:8080/game.html
```

`wasm/play.html` is a developer level-select harness.  `wasm/index.html` is a
minimal renderer smoke page.

### Controls

Defaults follow the original DOS bindings:

| Action | Key |
|---|---|
| Move | Arrow keys |
| Jump | Ctrl |
| Fire / action | Alt |
| Help | Esc or F1 |
| Pause | P |

Bindings can be redefined from the in-game help screen and are persisted.
Joystick support uses the Gamepad API and is off by default.

### Browser substitutions

DOS file I/O, BBS dialling and ordering are replaced by the original text
screens with no external calls.  Saves (slots 0–9), the ten-row high-score
table and `CC1.CFG`-equivalent settings live in `localStorage`; see
`notes/persist.md` for the format and validation rules.

## Tests

Run from `wasm/`:

```sh
make test-all          # full committed-fixture regression suite
make test-browser      # headless Chromium smoke test (Playwright)
make perf              # wasm size + frame-budget measurement
```

`make test-all` needs no virtualenv and no Unicorn: every fixture is committed
under `notes/`.  It covers rendering, physics, combat, handlers, transitions,
cells, HUD/text/messages, shell, save/restore, high scores, key mapping and
sound.  `notes/regression.md` maps each subsystem to its target, fixture and
harness.

The browser smoke test walks title → intro → hub, drives real keyboard input,
saves and restores through `localStorage`, reloads for config/high-score
persistence and runs the finale flow.  Install its tooling once:

```sh
cd wasm
npm install
npx playwright install chromium
```

Without Playwright, `make test-browser` prints `SKIP` and exits 0.

### Oracle regeneration (optional)

The `test-oracle-*` targets re-run the original routines under Unicorn and
rewrite the committed fixtures, then compare the C port against them.  They
need the Python dependencies and the original unpacked EXE
(`extracted/CC1-UNLZ.EXE`, committed):

```sh
python3 -m venv .venv
.venv/bin/pip install -r tools/requirements.txt
cd wasm
make test-oracle-physics
make test-oracle-combat
make test-oracle-cells
# ... see the Makefile for the full list
```

Regenerating raw extraction from the original archive additionally requires
`CCSW10A.SHR` and is documented in `AGENTS.md`.

## Continuous integration

`.github/workflows/ci.yml` runs on Ubuntu:

- **build + deterministic suite** — apt `clang`/`lld`, `make clang`,
  `make test-all`, then wasm size and frame budget in the job summary;
- **headless browser smoke** — `npm ci`, Playwright Chromium,
  `make test-browser`;
- **oracle regeneration** — manual `workflow_dispatch` job with
  capstone/unicorn for re-verifying fixtures.

## Layout

| Path | Contents |
|---|---|
| `wasm/` | C port, build, page shell, JS adapters and tests |
| `notes/` | Recovered routine notes, oracle fixtures and format docs |
| `tools/` | Extraction, oracle emulation, fixture generation and asset export |
| `out/portable/` | Build inputs: levels, GFX, cells, screens, fonts, sound |
| `out/oracle_L*.png` | Committed static render references (19 levels) |
| `extracted/` | Unpacked original EXE and assets |
| `plan.md` | Phase-by-phase implementation record |

Useful notes: `notes/physics.md` (timing/input), `notes/gameplay.md`
(update loop/handlers), `notes/shell.md` (HUD/text/menu), `notes/persist.md`
(save/config/high-score formats), `notes/sound.md` (driver/traces),
`notes/regression.md` (coverage matrix).
