# Phase 4 — Structural and build cleanups

## Goal

Remove the remaining structural friction: generated-header coupling,
duplicated declarations, fragile build inputs and missing automated checks.
This phase also covers optional hardening that is worth doing once the code is
already split.

## 4.1 Header hygiene

### Split the generated object header

Currently `objects_data.h` contains both the `ObjectEntry` type and all
per-level tables, and `objects.h` includes it. This leaks data into every
translation unit that includes the public API.

Target:

- `game/object.h` — hand-written `ObjectEntry` type, `OBJ_MAX`, field offsets.
- `objects_data.h` — generated data arrays only; includes `game/object.h`.

Update `tools/make_objects_data.py` to emit this shape, regenerate with
`make data`, and update `tools/check_oracle.py` to look for the type in
`game/object.h`.

### Cell dimensions and flags

Make `cells_data.h` include `game/cell.h` instead of redefining `CELL_COLS`,
`CELL_ROWS` and flag offsets. Ensure the renderer and gameplay use the same
definitions.

### Include ownership

- Only the data-owner `.c` file includes each generated data header.
- Public headers (`objects.h`, `shell.h`, `text.h`, `render.h`, `sound.h`)
  contain no generated tables.
- Remove ad-hoc `extern` declarations from `.c` files.
- Add include guards to every hand-written header if missing.

### `ObjectEntry` field renames

If Phase 3 renamed the generated fields, ensure the generator and
`check_oracle.py` are the single source of truth. Add `offsetof` asserts for
the renamed fields.

## 4.2 Build system

### Explicit source lists

Replace the hard-coded file lists in both the clang and emcc rules with
variables:

```make
GAME_SRC  = $(wildcard game/*.c game/handlers/*.c)
SHELL_SRC = $(wildcard shell/*.c)
TEXT_SRC  = $(wildcard text/*.c)
RENDER_SRC= $(wildcard render/*.c)
SRC       = $(GAME_SRC) $(SHELL_SRC) $(TEXT_SRC) $(RENDER_SRC) sound.c
```

Keep the source order deterministic (sort the wildcard results if necessary).
Update `$(OUT)` dependencies accordingly.

### Include paths

Add `-I.` (and any subdirectory include paths) so `#include "game/state.h"` and
`#include "objects.h"` both work from any source file.

### Dependency files

Add `-MMD -MP` (or an explicit header list) so moving a header cannot produce a
stale build. If the one-shot compile/link command makes depfiles awkward,
consider compiling to object files first:

```make
OBJ = $(SRC:.c=.o)
$(OUT): $(OBJ)
    $(LLVM)/clang ... -o $@ $(OBJ)
```

Measure build time and wasm size before/after. Keep the build reproducible.

### Required checks in CI

Add to `.github/workflows/ci.yml` (or the local equivalent):

- `make lint`
- `make test-digest`
- `make test-all`
- `make perf`
- optional: `make test-browser`

### Export stability

Add a check that the built wasm exports exactly the intended names. The
Makefile already lists `EXPORTS`; compare the built module against that list or
a committed expected file. This catches accidental renames/removals.

## 4.3 Optional higher-leverage techniques

Do these only after the core phases are green and the team wants them.

### LTO

If splitting into multiple translation units causes a size or frame-time
regression, try `-flto` and measure against the Phase 0 baseline. The game
budget has a lot of headroom (~1.2 ms/frame vs 57.1 ms), so this is likely
unnecessary.

### Native ASan/UBSan harness

Compile the gameplay modules for the host with a tiny libc shim and run the
digest/fixtures under AddressSanitizer. This catches out-of-bounds access
introduced during refactors.

Notes:

- The code deliberately relies on 16-bit wraparound, so UBSan will be noisy.
  Start with ASan only, or disable signed-integer-overflow checks.
- A native build is a new maintenance surface; treat it as optional tooling,
  not part of the shipping build.

### Complexity checks

Extend `tools/check_source_limits.py` to report a simple cyclomatic-complexity
score (branch counting) and fail on functions above a threshold. Alternatively,
use `clang-tidy` with `readability-function-size` and a narrow set of
bugprone checks. Do not enable a broad clang-tidy profile; it will drown the
real issues in style noise.

### Coverage checks

Ensure every handler offset appears in:

- `notes/handlers.md`;
- the dispatch table;
- at least one fixture scenario.

A small Python check can compare the 46 offsets from `notes/handlers.json` with
the table in `game/handlers/table.c`.

## 4.4 Documentation updates

- Finish `wasm/ARCHITECTURE.md`:
  - final module map and dependency rules;
  - state/save invariants;
  - handler table;
  - cell-rule table;
  - naming conventions;
  - how to add a handler or cell rule;
  - test ladder and digest procedure.
- Add `notes/refactor_map.md`: old function/line ranges → new files, plus the
  commit hashes for each phase.
- Update `AGENTS.md`:
  - new source layout;
  - file/function size budgets;
  - "no behavior changes in structural commits" rule;
  - generated-header rules;
  - test commands.
- Update `README.md` only if the build commands or paths changed.

## Test ladder

```sh
make clean
make clang
make lint
make test-digest
make test-all
make perf
make test-browser        # if available
```

On Linux CI as well, to catch path/include differences.

## Exit criteria

- No generated data is included through a public header.
- Only the data-owner translation unit includes each generated data header.
- The Makefile has one source list per backend and correct dependencies.
- CI runs lint, digest and the full suite.
- `make clean && make clang` produces the same exports and equivalent size/perf.
- `ARCHITECTURE.md`, `notes/refactor_map.md` and `AGENTS.md` are up to date.
