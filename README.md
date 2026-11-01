# Crystal Caves Episode 1 — C / WebAssembly port

A clean-room, decompilation-driven port of the 1991 shareware **Crystal Caves
Episode 1** (Apogee Software) to freestanding C and WebAssembly.  There is no
DOSBox and no emulator in the loop: the original EXE was disassembled, the game
logic was re-implemented in C, and every subsystem is verified **frame-for-frame**
against the real binary running under a Unicorn-based oracle.

![The mineshaft hub](out/oracle_L02.png)
![A crystal cave](out/oracle_L06.png)

The goal is fidelity, not a remake:

- static level rendering is **pixel-exact on all 19 levels**;
- player physics, combat, cell events, all 46 handler behaviours and level
  transitions are verified frame-for-frame against the original EXE;
- the HUD, text windows, shell flow, high scores, save format, key handling and
  all 36 sound effects follow the original routines;
- saves, high scores, key bindings and config use `localStorage`.

The original game binary and shareware data are not downloadable here.
`extracted/` and `out/portable/` are committed, so a fresh checkout builds and
runs without the original archive.  Crystal Caves is © Apogee Entertainment;
see [License](#license) for the scope of this project's own code.

## Quick start

```sh
cd wasm
make                    # copies assets and builds render.wasm (~155 KB)

python3 -m http.server 8080
# open http://localhost:8080/game.html
```

The page fetches the wasm module and asset files, so it must be served over
HTTP — opening `game.html` from `file://` will not work.

## Requirements

| Purpose | Requirements |
|---|---|
| Build | Homebrew LLVM **and lld** on macOS (`brew install llvm lld`), or `clang` + `lld` on Linux |
| Tests | Node.js 18+ and Python 3.9+ (stdlib only) |
| Oracle regeneration | Python venv with `tools/requirements.txt` (`capstone`, `unicorn`) |
| Browser smoke test | npm + `playwright` (test-only; see `wasm/package.json`) |
| Docker image | Docker 20.10+ (multi-stage build; no local toolchain needed) |

The Emscripten build (`make emcc`) additionally needs `emcc` on `PATH`; it is a
secondary back-end and is not exercised by CI.

## Build

```sh
cd wasm
make                    # same as: make clang
```

This copies the portable assets from `../out/portable/` into `wasm/assets/` and
produces `wasm/render.wasm` (about 155 KB at `-Oz`).

`wasm/play.html` is a developer level-select harness.  `wasm/index.html` is a
minimal renderer smoke page.

## Controls

Defaults follow the original DOS bindings, with Space and Up Arrow added as
browser jump aliases:

| Action | Key |
|---|---|
| Move | Left/Right arrows |
| Jump | Ctrl (DOS default), Space or Up Arrow |
| Fire / action | Alt |
| Help | Esc or F1 |
| Pause | P |

Bindings can be redefined from the in-game help screen and are persisted.
Joystick support uses the Gamepad API and is off by default.

On macOS, holding Ctrl and pressing Left/Right triggers the system Spaces
shortcut. Use Space or Up Arrow to jump when steering through the air with
arrow keys.

### Browser substitutions

DOS file I/O, BBS dialling and ordering are replaced by the original text
screens with no external calls.  Saves (slots 0–9), the ten-row high-score
table and `CC1.CFG`-equivalent settings live in `localStorage`; see
`notes/persist.md` for the format and validation rules.  The server is
therefore completely stateless — all player progress stays in the browser.

## Deploy with Dokku

The repo ships a `Dockerfile` that builds the wasm module with Debian
`clang`/`lld` and serves it from `nginx:alpine`.  Only `game.html`, its four
JS adapters, the favicon set, `render.wasm` and `assets/` are copied into the
runtime image; no toolchain, test data or original EXE is present in the
result.  That is about 1.6 MB of game files on top of `nginx:alpine`
(≈65 MB total).

Dokku auto-detects the root `Dockerfile` and needs no ports configured: the
image exposes `80`, so the host's nginx proxies straight to it.

```sh
# once, on the Dokku host
dokku apps:create crystal-caves

# once, in this checkout
git remote add dokku dokku@your-host:crystal-caves

# every deploy
git push dokku main
```

The app is then available at the default vhost (`http://crystal-caves.your-host/`).
Add `dokku letsencrypt:enable crystal-caves` on the host for HTTPS.  There are
no environment variables, volumes, migrations or background processes.

Build and run the image locally the same way CI would:

```sh
docker build -t crystal-caves .
docker run --rm -p 8080:80 crystal-caves
# open http://localhost:8080/
```

## Tests

Run from `wasm/`:

```sh
make test-all           # full committed-fixture regression suite
make test-browser       # headless Chromium smoke test (Playwright)
make perf               # wasm size + frame-budget measurement
```

`make test-all` needs no virtualenv and no Unicorn: every fixture is committed
under `notes/`.  It covers rendering, physics, combat, handlers, transitions,
cells, HUD/text/messages, the ordering/foreign/BBS/about/video/quit screens,
shell, save/restore, high scores, key mapping, sound, the exact title/credits
scroll (`test-shell-flow`) and the input cursor animation (`test-cursor`).
`notes/regression.md` maps each subsystem to its target, fixture and harness.

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
| `Dockerfile` | Multi-stage wasm build + nginx runtime image |
| `deploy/nginx.conf` | Static host config (MIME, gzip, cache) |

Useful notes: `notes/physics.md` (timing/input), `notes/gameplay.md`
(update loop/handlers), `notes/shell.md` (HUD/text/menu), `notes/persist.md`
(save/config/high-score formats), `notes/sound.md` (driver/traces),
`notes/regression.md` (coverage matrix).

## License

The port's own source — `wasm/`, `tools/`, `deploy/` and the documentation — is
released under the [MIT License](LICENSE).

Crystal Caves is © 1991 Apogee Entertainment.  The original game data,
disassembly inputs and derived renders in `extracted/`, `out/` and the
`assets/` generated from them are included only so the decompilation can be
built and verified, are **not** covered by the MIT license, and remain the
property of their respective owners.  This is a research project, not a
redistribution of the game.
