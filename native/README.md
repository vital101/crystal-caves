# Native desktop build (Windows / macOS / Linux)

The game core in `../wasm/` is freestanding C; this directory adds an SDL3
host that replaces the browser shell (`game.html` + JS adapters) with a native
executable.  The same core sources compile for both targets — the wasm build
stays untouched, and the native build is held to the **same 929 golden-master
SHA-256 digests** (`notes/refactor_digests.json`) as the wasm build.

## Build

```sh
cmake -S native -B native/build -DCMAKE_BUILD_TYPE=Release
cmake --build native/build
./native/build/caves        # the game (Linux/macOS; caves.exe on Windows)
```

Requirements: CMake 3.20+, a C11 compiler, Python 3, and SDL3.  SDL3 is found
via `find_package` (Homebrew `brew install sdl3`, `apt install libsdl3-dev`,
vcpkg, …); if not found, CMake `FetchContent` builds SDL3 from source, so a
bare toolchain also works.  All game assets from `out/portable/` are embedded
into the executable by `tools/embed_assets.py` at build time — the result is
a single self-contained binary.

Or simply `make -C native` (wrapper around the two CMake commands).

## Run

```sh
./native/build/caves
```

Launching `caves` shows the **episode picker first** — a text window drawn
with the original MNI font by `launcher.c` (Episode 1's embedded assets).
Episode 1 boots in place; Episode 2/3 entries appear when `caves_ep2` /
`caves_ep3` sit next to the `caves` binary and selecting one executes the
sibling.  The picker understands Up/Down + Enter, the number keys `1`/`2`/`3`
and Esc to quit.

```sh
./native/build/caves --ep 2     # skip the picker, execute caves_ep2
./native/build/caves --ep 1     # skip the picker, boot Episode 1
```

`caves_ep2` / `caves_ep3` boot straight into their episode and never show the
picker.  All other flags (`--smoke`, `--dump-sound`) skip the picker too, so
CI runs stay deterministic.

Controls match the browser build: arrows to move, Ctrl/Space/Up to jump, Alt
to fire, Esc/F1 help, P pause; redefinable from the help screen.  A gamepad
works when joystick is enabled.  Saves (slots 0–9), the high-score table and
config are stored as files in the per-user pref directory (`SDL_GetPrefPath`,
e.g. `~/.config/crystal-caves/`, `~/Library/Application Support/crystal-caves/`,
`%APPDATA%\crystal-caves\`) using the same formats as the browser's
`localStorage` keys (`cc1.save.<slot>.b64`, `cc1.hiscores.v1.json`,
`cc1.config.v2.json` — see `notes/persist.md` and `persist.c`).  All three
binaries share the directory; the per-episode `cc1.*` / `cc2.*` / `cc3.*`
namespaces keep their saves, high scores and config apart (see the isolation
test below).

Headless smoke test (CI path, dummy video/audio drivers):

```sh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./native/build/caves --smoke 120
```

## Tests

```sh
make -C native test-digest             # native vs wasm golden-master digests (929 snapshots)
make -C native test-smoke              # headless run of the full SDL host path (episode 1)
make -C native test-smoke-ep2          # same for caves_ep2
make -C native test-smoke-ep3          # same for caves_ep3
make -C native test-menu               # headless launcher picker check
make -C native test-persist-isolation  # per-episode save namespaces in one shared dir
```

`test-digest` runs `cc_digest` once per scenario group (`static_render`,
`gameplay`, `transitions`, `warp`, `shell`, `save_round_trip`) — each in a
fresh process, mirroring the fresh `WebAssembly.Instance` per group in
`wasm/test_refactor_digest.js` — and `compare_digests.js` checks the assembled
JSON against `notes/refactor_digests.json`.  `cc_digest` needs no SDL.

`test-menu` runs `caves --menu-smoke`: the picker renders headless (dummy
drivers), a scripted Down+Enter selects the second listed entry, and the
printed `MENU OK: selected <ep> of <n>` line is checked against whichever
sibling binaries exist next to `caves` (`selected 2 of 3` when both are
built; `selected 1 of 1` for a lone Episode 1 binary).  `--menu-smoke` and a
mismatched `--ep` are rejected by the Episode 2/3 binaries instead of being
silently ignored.

`test-persist-isolation` runs the three `persist_test_epN` variants
(`persist_test.c` + `persist.c`, one per `CC_EPISODE`) against one shared
temp directory: each writes its config, high scores and save slot 0, reloads
them, and asserts both that its own files appeared and that every file
another episode had written stayed byte-identical.

## Files

| File | Role |
|---|---|
| `CMakeLists.txt` | ccore static lib (wasm core, `-DCC_HOST_LIBC -ffreestanding`), embedded assets, `caves` + `cc_digest` + per-episode binaries and persist-test variants, SDL3 discovery/FetchContent |
| `host.c` | SDL3 host: window/integer-scaled blit, fixed-step 140.06 Hz loop, input, gamepad, audio pump, save/restore handshake — port of `wasm/game.js` + `wasm/game/*.js` |
| `launcher.c/h` | episode picker shown by `caves` (original text renderer, sibling-binary execution, `--ep N`, `--menu-smoke`) |
| `keymap.c` | SDL scancode → DOS set-1 scan code — port of `wasm/keymap.js` |
| `persist.c` | pref-dir files for config/high scores/save slots — port of `wasm/persist.js` |
| `persist_test.c` | per-episode save-file isolation check (shared dir, byte-identical cross-episode assert) |
| `synth.c` | PC-speaker trace renderer + single-channel player — port of `wasm/sound_synth.js` + `wasm/audio.js`; traces render at the output device's native rate (`host.c` queries it) and pass through the same 5.5 kHz small-speaker low-pass as the browser |
| `digest.c` | golden-master digest harness — port of `wasm/test_refactor_digest.js` |
| `sha256.c` | SHA-256 for the digest harness |
| `compare_digests.js` | assembles `cc_digest` group outputs and diffs them against the fixture |
| `cc_host.h` | core exports not declared in the public `wasm/*.h` headers |

## Notes

- The core is compiled with `-ffreestanding -fno-builtin` and `-DCC_HOST_LIBC`:
  under that macro `wasm/render/runtime.c` uses the platform libc's
  `memset`/`memcpy` instead of its own freestanding copies (the wasm build is
  unchanged).
- `cc_state_t` is pointer-free (enforced by `_Static_assert`s in
  `wasm/game/state.c`), so 64-bit native builds keep the exact same save
  format and state hashes as wasm32.
- Mobile (Android/iOS) is a future phase: SDL3's mobile templates would reuse
  this host with a touch layer replacing `wasm/touch.js`.
