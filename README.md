# Crystal Caves — all three episodes in C / WebAssembly

A clean-room, decompilation-driven port of the 1991 shareware **Crystal Caves
trilogy** (Apogee Software) to freestanding C and WebAssembly.  There is no
DOSBox and no emulator in the loop: the original EXE was disassembled, the game
logic was re-implemented in C, and every subsystem is verified **frame-for-frame**
against the real binary running under a Unicorn-based oracle.

All three episodes are here and playable:

- **Episode 1 — Trouble in Twin Planetoids** (the shareware episode)
- **Episode 2 — Slugging It Out**
- **Episode 3 — Mylo vs. the Supernova** (with the original three-screen ending)

> [!TIP]
> **Play it now in your browser:** <https://crystal-caves.re-cycledair.com>
> — the launch page shows all three episodes to pick from.

![The mineshaft hub](out/oracle_L02.png)
![A crystal cave](out/oracle_L06.png)

The goal is fidelity, not a remake:

- static level rendering is **pixel-exact on all 19 levels of every episode**
  (see the Episode 2 and 3 shots below);
- player physics, combat, cell events, all 46 handler behaviours and level
  transitions are verified frame-for-frame against the original EXE;
- the HUD, text windows, shell flow, high scores, save format, key handling and
  all 36 sound effects follow the original routines;
- saves, high scores, key bindings and config use `localStorage` — each
  episode keeps its own `cc1.*` / `cc2.*` / `cc3.*` namespace, so saves,
  high scores and settings never clobber another episode's.

The original game binary and shareware data are not downloadable here.
`extracted/` and `out/portable/` are committed, so a fresh checkout builds and
runs without the original archive.  Crystal Caves is © Apogee Entertainment;
see [License](#license) for the scope of this project's own code.

## Quick start

```sh
cd wasm
make                    # copies assets and builds render.wasm (~155 KB)

python3 -m http.server 8080
# open http://localhost:8080/select.html — the episode picker
```

The page fetches the wasm module and asset files, so it must be served over
HTTP — opening `game.html` from `file://` will not work.  `game.html` is the
direct link into an episode (`game.html?ep=2`, `game.html?ep=3`); a small
"Episode N" button in the toolbar switches without losing your place.

### Play Episode 2

Episode 2 uses the same engine and its registered data is committed in this
private fork.  The built module and browser assets are committed too, so
serving the checkout is enough:

![Episode 2 — the mineshaft hub](out/ep2/oracle_L02.png)
![Episode 2 — a crystal cave](out/ep2/oracle_L06.png)

```sh
python3 -m http.server 8080 -d wasm # open /select.html and pick Episode 2
make -C wasm test-ep2               # render/data/transition/browser checks
```

To rebuild from the committed sources (needed only after engine changes):

```sh
make -C wasm ep2 ep2-assets         # render_ep2.wasm + assets_ep2/
```

The browser stores Episode 2 config, high scores and saves under `cc2.*`,
separate from Episode 1's `cc1.*`.  The R0 delta analysis, address map and
artifact inventory are documented in `notes/ep2_diff.md`.

### Play Episode 3

Episode 3 ("Mylo vs. the Supernova") is committed the same way:

![Episode 3 — the mineshaft hub](out/ep3/oracle_L02.png)
![Episode 3 — a crystal cave](out/ep3/oracle_L06.png)

```sh
python3 -m http.server 8080 -d wasm # open /select.html and pick Episode 3
make -C wasm test-ep3               # render/data/transition/browser checks
```

Rebuild after engine changes with `make -C wasm ep3 ep3-assets`.  Episode 3
stores its config, high scores and saves under `cc3.*`; its delta analysis and
the three-screen ending are documented in `notes/ep3_diff.md`.

Nothing installed locally, or building on Windows?  Every command in this
README also runs in the dev container — see
[Develop in Docker](#develop-in-docker-any-platform).

## Develop in Docker (any platform)

Windows hosts, and any machine without a local LLVM/Node/Python toolchain, can
run everything in a container instead of installing the requirements below:

```sh
docker compose build                                    # once, from the repo root
docker compose run --rm dev make -C wasm clang          # build render.wasm
docker compose run --rm dev make -C wasm test-all        # full regression suite
docker compose run --rm dev make -C wasm test-browser    # Playwright smoke test
docker compose run --rm dev make -C wasm test-oracle-physics   # Unicorn fixtures
docker compose run --rm dev make -C wasm perf lint
docker compose run --rm dev bash                        # interactive shell
docker compose up web                                   # http://localhost:8080/game.html
```

`compose.yaml` builds `docker/Dockerfile.dev`: Debian `clang` + `lld`, Node.js 22
(same as CI), a `capstone`/`unicorn` virtualenv at `/opt/cc1/venv`, and Playwright
Chromium — i.e. every target in the Requirements table, with nothing to install on
the host.  The image exports `LLVM`, `PYTHON`, `NODE_PATH` and
`PLAYWRIGHT_BROWSERS_PATH`, which override the Makefile's Homebrew defaults, so no
target needs `LLVM=...` inside the container.  The checkout is mounted at `/src`
and build outputs (`wasm/render.wasm`, `wasm/assets/`) land in your working tree
as usual, so the wasm you just built is the wasm the browser serves.

The first build pulls a base image, the toolchain and Chromium: a few minutes
and about 2.5 GB.  After that it is cached and only rebuilds when
`wasm/package.json`, `wasm/package-lock.json` or `tools/requirements.txt`
change.  Inside the container a full rebuild takes ~3 s and the whole
`make test-all` suite about 10 s, so the container is not the slow part.

Two notes for Windows users:

- `.gitattributes` pins makefiles and shell scripts to LF.  A checkout made
  before it existed may hold a CRLF `wasm/Makefile`, which GNU make inside the
  container misreads (the dev container warns when it sees one).  git reports
  such a file as clean, so a plain `git checkout` will not rewrite it: delete it
  once and git re-checks it out as LF,
  `rm wasm/Makefile && git checkout -- wasm/Makefile`.
- The container runs as uid 1000, so files it writes stay usable from the host.

This is the **development** image.  The root `Dockerfile` is a separate, much
smaller production image (nginx, no toolchain, no test data) — see
[Deploy with Dokku](#deploy-with-dokku).

## Requirements

| Purpose | Requirements |
|---|---|
| Build | Homebrew LLVM **and lld** on macOS (`brew install llvm lld`), or `clang` + `lld` on Linux |
| Tests | Node.js 18+ and Python 3.9+ (stdlib only) |
| Oracle regeneration | Python venv with `tools/requirements.txt` (`capstone`, `unicorn`) |
| Browser smoke test | npm + `playwright` (test-only; see `wasm/package.json`) |
| None of the above locally | Docker Desktop (Windows) or Docker 20.10+ — see [Develop in Docker](#develop-in-docker-any-platform) |
| Production image | Docker 20.10+ (multi-stage build; no local toolchain needed) |

There is no Homebrew on Windows, and the build is not portable to MSVC: on
Windows use the dev container above, or `clang`/`lld` inside WSL.

The Emscripten build (`make emcc`) additionally needs `emcc` on `PATH`; it is a
secondary back-end and is not exercised by CI.

## Build

```sh
cd wasm
make                    # same as: make clang
```

This copies the portable assets from `../out/portable/` into `wasm/assets/` and
produces `wasm/render.wasm` (about 155 KB at `-Oz`).  Without a local LLVM, the
same build is `docker compose run --rm dev make -C wasm clang`.

`wasm/play.html` is a developer level-select harness.  `wasm/index.html` is a
minimal renderer smoke page.  `make social-card` regenerates
`wasm/social-card.png` (1200x630) from the original title screen for link
previews.

### Native desktop executables (Windows/macOS/Linux)

The same C core also builds as a standalone SDL3 executable — see
[`native/README.md`](native/README.md):

```sh
cmake -S native -B native/build -DCMAKE_BUILD_TYPE=Release
cmake --build native/build
./native/build/caves        # single self-contained binary, assets embedded
```

Launching `caves` shows the **episode picker first** (drawn with the original
in-game text renderer).  Episode 1 boots in place; Episode 2/3 entries appear
when the `caves_ep2` / `caves_ep3` binaries sit next to it and selecting one
starts them.  `caves --ep N` skips the picker and boots an episode directly.

The native build is verified against the same golden-master digests as the
wasm build: `make -C native test-digest` runs the 929 SHA-256 snapshots from
`notes/refactor_digests.json` inside the native binary and must match.  CI
builds the three Windows/Linux/macOS binaries **on every version tag** and
uploads them as artifacts (one archive per OS with `caves`, `caves_ep2` and
`caves_ep3`), together with the complete web bundle.

Episode 2 builds the same way:

```sh
make -C wasm ep2 ep2-assets        # generated data headers + assets_ep2/
make -C native test-smoke-ep2      # builds/runs native/build/caves_ep2 headless
```

`caves_ep2` embeds `out/portable_ep2`, sets an episode-specific window title
and keeps its config, high scores and save slots in `cc2.*` files next to the
Episode 1 `cc1.*` files — all three binaries share one pref directory and the
per-episode file namespaces keep the saves apart (`make -C native
test-persist-isolation` proves it).  `caves_ep3` is built the same way
(`make -C wasm ep3 ep3-assets`, then `make -C native test-smoke-ep3`) and
keeps its data in `cc3.*` files.

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
| Fullscreen | Fullscreen button (Esc exits) |

Bindings can be redefined from the in-game help screen and are persisted.
Joystick support uses the Gamepad API and is off by default.

On macOS, holding Ctrl and pressing Left/Right triggers the system Spaces
shortcut. Use Space or Up Arrow to jump when steering through the air with
arrow keys.

### Touch controls

On phones and tablets the page adds a semi-transparent virtual pad: ◀ ▶ at the
bottom left and JUMP/FIRE at the bottom right. A soft-keyboard strip (Esc,
Enter, Help, Pause) stays visible in menus and text entry, and a compact
on-screen keyboard slides up for save slots and high-score name entry.
Tapping the canvas advances the read-only "any key" screens (logo, story,
messages, pause, high scores, ending).

The controls auto-detect a coarse pointer and can be toggled from the button
next to Fullscreen. The choice is remembered in `localStorage`
(`cc1.touch.v1`), separate from the `CC1.CFG`-equivalent config.

On iOS Safari, where the element Fullscreen API is unavailable, the button
becomes an **Immersive** toggle that hides the page chrome. Adding the page to
the Home Screen (via `manifest.webmanifest`) gives a chromeless standalone app.

Known limits: iOS mutes WebAudio when the ringer switch is on; the in-browser
URL bar cannot be hidden on iOS (use Add to Home Screen); key redefinition on
touch uses the on-screen keyboard keys only.

### Browser substitutions

DOS file I/O, BBS dialling and ordering are replaced by the original text
screens with no external calls.  Saves (slots 0–9), the ten-row high-score
table and `CC1.CFG`-equivalent settings live in `localStorage`; see
`notes/persist.md` for the format and validation rules.  The server is
therefore completely stateless — all player progress stays in the browser.

## Deploy with Dokku

The repo ships a `Dockerfile` that builds the wasm modules of **all three
episodes** with Debian `clang`/`lld` and serves them from `nginx:alpine`.
This is the **production** image — small, no toolchain, no test data;
`docker/Dockerfile.dev` is the separate development toolchain image described
in [Develop in Docker](#develop-in-docker-any-platform).  Only `select.html`
(the launch picker), `game.html`, their browser scripts (`game.js` plus
`game/*.js` and the small adapters), the favicon set, the link-preview card,
the episode screenshots, the three wasm modules and the three asset packs are
copied into the runtime image; no toolchain, test data or original EXE is
present in the result.  That is about 5 MB of game files on top of
`nginx:alpine` (≈70 MB total).  The picker is what `/` serves, so a fresh
visit starts with the episode choice; registered Episode 2/3 data is
committed in this private fork and deployed deliberately.

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
make test-exports       # built wasm exports match the Makefile EXPORTS list
make test-digest        # golden-master state/view/sound digests
make test-browser       # headless Chromium smoke test (Playwright)
make perf               # wasm size + frame-budget measurement
make lint               # source-size guardrail report (`lint-strict` fails)
```

Native desktop build (see `native/README.md`):

```sh
make -C native build          # CMake/SDL3: native/build/caves + cc_digest
make -C native test-digest    # native binary vs the same 929 wasm digests
make -C native test-smoke     # headless SDL host run (dummy drivers)
```

On a host without the toolchain installed, the same targets run in the dev
container: `docker compose run --rm dev make -C wasm test-all` (see
[Develop in Docker](#develop-in-docker-any-platform)).

`make test-all` needs no virtualenv and no Unicorn: every fixture is committed
under `notes/`.  It covers rendering, physics, combat, handlers, transitions,
cells, HUD/text/messages, the ordering/foreign/BBS/about/video/quit screens,
shell, save/restore, high scores, key mapping, sound, the exact title/credits
scroll (`test-shell-flow`), the input cursor animation (`test-cursor`) and the
refactor digest (`test-digest`).  `notes/regression.md` maps each subsystem to
its target, fixture and harness; `wasm/ARCHITECTURE.md` documents the module
boundaries and the digest-update procedure.

The browser smoke test walks title → intro → hub, drives real keyboard input,
saves and restores through `localStorage`, reloads for config/high-score
persistence and runs the finale flow.  Install its tooling once:

```sh
cd wasm
npm install
npx playwright install chromium
```

Without Playwright, `make test-browser` prints `SKIP` and exits 0.  Inside the
dev container the dependency and its Chromium are already baked into the image,
so the test always runs there.

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

The dev container already has that virtualenv (`/opt/cc1/venv`, exported as
`PYTHON`), so there the same targets are just
`docker compose run --rm dev make -C wasm test-oracle-physics`.

Regenerating raw extraction from the original archive additionally requires
`CCSW10A.SHR` and is documented in `AGENTS.md`.

## Continuous integration

`.forgejo/workflows/ci.yml` runs on Forgejo Actions only (GitHub never reads
that directory, so nothing runs on github.com).  Actions are loaded from
`data.forgejo.org` and referenced fully qualified.  The workflow has no
`permissions:` block — Forgejo ignores it and grants capabilities through
Authorized Integrations, while the jobs only need the default read token:

**On every push to main / every PR** — the fast regression jobs:

- **build + deterministic suite** — apt `clang`/`lld`, `make clang`,
  `make test-all`, `make lint-strict`, then wasm size and frame budget in the
  job summary;
- **headless browser smoke** — `npm ci`, Playwright Chromium,
  `make test-browser` (which also checks the launch picker at `/`).

**On every version tag** (and `workflow_dispatch`) — the release builds of
**all three episodes**, each job attaching its bundle to the release that
carries the tag:

- **native build** — matrix over Ubuntu/Windows/macOS: a bootstrap step
  installs CMake/compiler/Python/git when a runner image lacks them, CMake
  builds `caves`, `caves_ep2` and `caves_ep3` (SDL3 via package or
  FetchContent), the 929 golden-master digests re-run inside the native
  binary against `notes/refactor_digests.json`, headless SDL smoke runs
  cover all three episodes, the launcher picker and the per-episode
  save-isolation check run, the executables are uploaded as artifacts
  through Forgejo's `forgejo/upload-artifact` fork (the upstream
  `actions/upload-artifact@v4` refuses any non-github.com server), and the
  three binaries are attached to the release as one per-OS bundle
  (`caves-linux-<arch>-<tag>.tar.gz`, `caves-macos-<arch>-<tag>.tar.gz`,
  `caves-windows-<arch>-<tag>.zip`);
- **web bundle** — `make -C wasm bundle-web` builds all three wasm modules
  and asset packs and packages the complete site (launch picker, game pages,
  `render.wasm` / `render_ep2.wasm` / `render_ep3.wasm` and the asset
  directories) into one downloadable tarball artifact, also attached to
  the release (`crystal-caves-web-<tag>.tar.gz`);
- **oracle regeneration** — manual `workflow_dispatch` job with
  capstone/unicorn for re-verifying fixtures.

Release uploads speak to the Forgejo API with the job's built-in token
(`secrets.GITHUB_TOKEN`) — the same mechanism `candid_app`'s release
workflow uses, so no extra secret is required.  A release created through
the Forgejo interface gets its assets directly; a plain `git push` of a tag
creates a draft release to attach them to.

## Layout

| Path | Contents |
|---|---|
| `wasm/` | C port, build, launch picker (`select.html`), page shell, JS adapters and tests |
| `native/` | SDL3 desktop host (Windows/macOS/Linux) + episode launcher + digest/isolation harnesses |
| `notes/` | Recovered routine notes, oracle fixtures and format docs |
| `tools/` | Extraction, oracle emulation, fixture generation and asset export |
| `out/portable/` | Build inputs: levels, GFX, cells, screens, fonts, sound |
| `out/oracle_L*.png` | Committed static render references (19 levels × 3 episodes) |
| `extracted/` | Unpacked original EXE and assets |
| `refactor/` | Phased C/WASM refactoring plan |
| `Dockerfile` | Multi-stage wasm build (all three episodes) + nginx runtime image (**production**) |
| `docker/Dockerfile.dev`, `compose.yaml` | Development toolchain container: clang/lld, Node 22, capstone/unicorn venv, Playwright Chromium |
| `.gitattributes` | LF pin for makefiles/shell scripts, needed by GNU make in that container |
| `deploy/nginx.conf` | Static host config (picker at `/`, MIME, gzip, cache) |

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
