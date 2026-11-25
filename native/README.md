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

Controls match the browser build: arrows to move, Ctrl/Space/Up to jump, Alt
to fire, Esc/F1 help, P pause; redefinable from the help screen.  A gamepad
works when joystick is enabled.  Saves (slots 0–9), the high-score table and
config are stored as files in the per-user pref directory (`SDL_GetPrefPath`,
e.g. `~/.config/crystal-caves/`, `~/Library/Application Support/crystal-caves/`,
`%APPDATA%\crystal-caves\`) using the same formats as the browser's
`localStorage` keys (`cc1.save.<slot>.b64`, `cc1.hiscores.v1.json`,
`cc1.config.v2.json` — see `notes/persist.md` and `persist.c`).

Headless smoke test (CI path, dummy video/audio drivers):

```sh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./native/build/caves --smoke 120
```

## Tests

```sh
make -C native test-digest   # native vs wasm golden-master digests (929 snapshots)
make -C native test-smoke    # headless run of the full SDL host path
```

`test-digest` runs `cc_digest` once per scenario group (`static_render`,
`gameplay`, `transitions`, `warp`, `shell`, `save_round_trip`) — each in a
fresh process, mirroring the fresh `WebAssembly.Instance` per group in
`wasm/test_refactor_digest.js` — and `compare_digests.js` checks the assembled
JSON against `notes/refactor_digests.json`.  `cc_digest` needs no SDL.

## Files

| File | Role |
|---|---|
| `CMakeLists.txt` | ccore static lib (wasm core, `-DCC_HOST_LIBC -ffreestanding`), embedded assets, `caves` + `cc_digest` targets, SDL3 discovery/FetchContent |
| `host.c` | SDL3 host: window/integer-scaled blit, fixed-step 140.06 Hz loop, input, gamepad, audio pump, save/restore handshake — port of `wasm/game.js` + `wasm/game/*.js` |
| `keymap.c` | SDL scancode → DOS set-1 scan code — port of `wasm/keymap.js` |
| `persist.c` | pref-dir files for config/high scores/save slots — port of `wasm/persist.js` |
| `synth.c` | PC-speaker trace renderer + single-channel player — port of `wasm/sound_synth.js` + `wasm/audio.js`; traces render at the output device's native rate (`host.c` queries it) to avoid SDL resampling artefacts |
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
