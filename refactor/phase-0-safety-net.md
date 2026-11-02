# Phase 0 — Safety net

Status: **implemented**.  The captured baseline lives in
[`notes/refactor_baseline.md`](../notes/refactor_baseline.md); the golden
master is [`notes/refactor_digests.json`](../notes/refactor_digests.json)
(`make test-digest`), and the architecture skeleton is
[`wasm/ARCHITECTURE.md`](../wasm/ARCHITECTURE.md).

## Goal

Establish a trustworthy baseline and automated checks **before any source
change**. This phase produces no refactoring; it makes the rest of the plan
safe.

## 0.1 Baseline

Run from a clean checkout:

```sh
cd wasm
make clang
make test-all
make test-browser        # if Playwright is installed; otherwise it SKIPs
make perf
```

On a host without a local toolchain (Windows), the same commands run in the dev
container: `docker compose run --rm dev make -C wasm <target>`.  Record the
toolchain version (`clang --version`) either way, since the baseline's byte
size and timings depend on it.

Record in `notes/refactor_baseline.md`:

- current git commit hash;
- `render.wasm` byte size;
- per-frame timings from `make perf` (average and worst);
- pass/fail output of `make test-all`;
- the exported symbol list (`wasm-objdump -x render.wasm` or the Makefile
  `EXPORTS` list).

This file is the reference for size/perf regressions later.

## 0.2 Add a characterization "digest" test

The existing fixtures are point tests. Add a broad golden-master test that
hashes complete deterministic state over scripted runs.

New files:

- `wasm/test_refactor_digest.js`
- `notes/refactor_digests.json`
- `make test-digest` target (also add it to `test-all`)

### Scenarios

1. **Static render:** every level 0–18. Call `cc_render_cells` and hash the
   framebuffer.
2. **Gameplay:** every playable level 2–18. Run 240–600 frames with a
   deterministic input schedule (idle / walk / jump / fire / reverse). Every
   10 frames snapshot:
   - SHA-256 of `cc_state_save()` bytes;
   - SHA-256 of `cc_view_framebuffer()` bytes.
3. **Transitions:** hub → each cave → hub, plus `cc_warp_level`.
4. **Shell flow:** logo → title → menu → story pages → help → high score →
   save/restore request → finale.
5. **Save round-trip:** save, mutate, load, compare state and subsequent
   frames.

Store hashes only, so the fixture stays small. Generate the fixture from the
**current** build, commit it, and require byte-identical results after every
refactor.

### Determinism notes

- Call `cc_sound_init(); cc_sound_set_enabled(0)` so shell waits on the sound
  driver resolve immediately and audio never affects state hashes.
- Clear the sound attempt log between scenarios if it is inspected.
- Do not include host pointers or memory addresses in hashes.
- `cc_state_save` intentionally includes the raw `cc_state_t` image; that is
  the point. Any layout or value change will be detected.

### Update procedure

`node test_refactor_digest.js --update` rewrites
`notes/refactor_digests.json`. An intentional behavior change updates the
fixture in its **own** commit with a clear message; a refactor must never
update it.

## 0.3 Add compile-time layout asserts

In `object.h` / `game_state.h` (created in Phase 1, but add the asserts at the
same time):

```c
_Static_assert(sizeof(ObjectEntry) == 0x2D, "object record layout changed");
_Static_assert(offsetof(ObjectEntry, shape) == 0x1C, "field moved");
_Static_assert(sizeof(cc_state_t) == 23010, "save payload changed");
_Static_assert(offsetof(cc_state_t, cells) == ..., "state layout changed");
```

Add asserts for each major state section (object array, cells, scores, cave
flags, checkpoints). This turns an accidental layout change into a build
failure instead of a save-corruption bug.

## 0.4 Add source-size and warning checks

New file: `tools/check_source_limits.py`.

Rules:

- fail if any hand-written `wasm/**/*.c` exceeds 600 lines;
- warn above 500 lines;
- fail if any function exceeds 120 lines (brace/comment-aware; ignore
  generated data headers and test JS);
- allowlist generated `.h` files.

Add `make lint` and run it in CI. Start as a warning-only check for existing
violations, then flip to failing once Phase 1/2 bring files under the limits.

Also add key warning flags to the build:

```make
-Werror=implicit-function-declaration
-Werror=incompatible-pointer-types
-Werror=return-type
```

Fix the two existing unused-function warnings (`draw_lines_centered`,
`start_finale_story`) in a behavior-neutral commit. Either remove dead code or
wire it in; do not leave `-Wall` noise.

## 0.5 Add the architecture skeleton

Create `wasm/ARCHITECTURE.md` now, even if incomplete. It will drive the split.
Initial sections:

- layer diagram: public API → gameplay modules → shared state;
- ownership rules (which module owns which state);
- save-format invariants;
- original-address convention (`/* CC1 0x1F9A:0x2877 */`);
- handler taxonomy table placeholder;
- "how to add a handler" checklist;
- test ladder and digest-update procedure.

Phase 5 completes it.

## 0.6 Commit discipline

- Create a branch, e.g. `refactor/structure`.
- Optionally use a worktree:
  `git worktree add ../crystal-caves-refactor -b refactor/structure`.
- Tag the baseline: `git tag refactor-baseline`.
- Every commit after this point must build and pass the relevant tests.

## Exit criteria

- Baseline documented.
- `make test-digest` exists and passes against the current build.
- Layout asserts are in place.
- `make lint` exists and reports the current violations.
- `wasm/ARCHITECTURE.md` skeleton exists.
- No source behavior changed.
