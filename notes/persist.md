# Crystal Caves Ep.1 — Phase 7 persistence notes

This note documents the hardened browser save/config/high-score layer and the
original DOS file semantics it replaces.  Addresses are load-module offsets
(`file = load + 0x1E00`).

## Save state (browser)

The deterministic snapshot is produced by `cc_state_save()` in
`wasm/objects.c`.  `cc_state_t` contains only gameplay state: 60 object slots,
the full mutable 20-byte cell image, player/camera, RNG, cave flags,
checkpoints and all Phase 2/3 counters.  Host-only fields (sound callback and
event log) live outside the struct so the payload is deterministic and never
contains pointers.

### v2 format

| Offset | Size | Meaning |
|---|---|---|
| 0 | 4 | magic `CCS2` |
| 4 | 2 | version `2` (little-endian) |
| 6 | 2 | payload size `23010` |
| 8 | 4 | CRC-32/ISO-HDLC of the payload |
| 12 | 23010 | raw `cc_state_t` image |

- CRC: reflected polynomial `0xEDB88320`, init/xorout `0xFFFFFFFF`.
- `cc_state_save()` returns **23022** bytes (12 + 23010).
- `cc_state_size()` returns **23548**, the maximum of the v2 buffer
  (23022) and the legacy v1 buffer (8 + 23540).  A single allocation therefore
  accepts both formats.
- `_Static_assert(sizeof(cc_state_t) == 23010)` freezes the layout.  Any
  field change must bump the version and add a migration.

### v1 migration

Phase 5 wrote an 8-byte `CCS1` header followed by a 23,540-byte payload.  The
first 23,010 bytes of that payload are layout-identical to the v2 payload; the
tail held host-only sound fields and dead collectible fields.  `cc_state_load`
accepts `CCS1` v1 with size 23,540 and copies the deterministic prefix.  v1 has
no checksum, so it is structurally validated but not CRC-checked; re-saving
upgrades the slot to v2.

### Validation and atomicity

`cc_state_load_ex()` parses into a temporary `cc_state_t` and only commits
after validation.  A failed load leaves the live state untouched and sets
`cc_state_last_error()`:

| Code | Meaning |
|---|---|
| 0 | OK |
| 1 | bad argument/length |
| 2 | bad magic |
| 3 | bad version |
| 4 | bad size/truncated |
| 5 | bad CRC |
| 6 | invalid state (level/cursor/flags/coords) |
| 7 | cave save rejected by `CC_STATE_LOAD_HUB_ONLY` |

Validated fields include `cur_level` (0..18), `next_level` (-2..18),
`obj_cursor` (<60), `cave_flags` (0/1), spawn/exit cell coordinates and the
death/interaction state (`0..3`).  `CC_STATE_LOAD_HUB_ONLY` additionally
requires `cur_level == 2`, matching the original hub-only save rule.

The browser handshake is in `wasm/persist.js`:

- `cc1.save.<slot>` stores the base64 of the v2 (or migrated v1) bytes.
- `saveSlot()` checks the returned length before encoding.
- `loadSlot()` rejects oversized/empty/non-string values **before** writing to
  wasm memory, then calls `cc_state_load_ex(..., hubOnly)`.

`make test-persist` covers mid-hub and mid-cave round-trips, subsequent-frame
determinism, save-does-not-mutate, v1 migration, hub-only rejection, corrupt
slots, oversized slots and atomicity.  `notes/persist_v1_fixture.json` is a
real Phase 5 (`CCS1`) save captured from commit `33f292f`; the test loads it
and compares the migrated v2 payload SHA-256 against the same scenario
replayed by the current build.

## Original DOS save file

The original save routine is `0x0FC5:0x6C87`; restore is `0x0FC5:0x6FE6`.
The file is `CC1-<slot>.sav` and contains exactly **0x21 = 33 bytes** from
`DS:0x5098` (`level_block`).

Before writing, the save routine patches the first two words with the player's
cell:

```
col = player_x / 16;  if (player_x % 16 >= 8) col++;
row = player_y / 16;
```

The 33-byte block is:

| Offset | Original | Meaning |
|---|---|---|
| 0x00 | `[0x5098]` | player cell column (patched) |
| 0x02 | `[0x509A]` | player cell row (patched) |
| 0x04 | `[0x509C]` | ammo |
| 0x06 | `[0x509E]` | health/lives |
| 0x08 | `[0x50A0]` | restart checkpoint |
| 0x0A | `[0x50A2]` | crystals collected |
| 0x0C | `[0x50A4]` | level/checkpoint byte |
| 0x0D | `[0x50A5]` | score low word |
| 0x0F | `[0x50A7]` | score high word |
| 0x11 | `[0x50A9..0x50B8]` | 16 cave-completion flags |

The original restore reloads the hub and applies only this block; it does not
save objects or cells.  The browser snapshot is a deliberate superset so a
mid-hub restore is exact (including collected crystals, opened doors, spawned
objects and RNG state).  The shell still enforces the original hub-only save
restriction and messages.

## Original CC1.HS semantics

The high-score file is **0x1400 = 5,120 bytes**:

- 10 names, each a 256-byte Pascal string, at file offset `0x000`;
- a 0x300-byte gap;
- 10 score strings, each a 256-byte Pascal string, at file offset `0xA00`.

In memory the names start at `DS:0x330` and the scores at `DS:0xD30`.  The
original initializer `0x0FC5:0x8738` writes the default table when the file is
missing or unreadable.  The observable rules are:

- default rows: `150000 Play all 3 episodes` down to `60000 J. Kirk`;
- qualification is `score > last row` (strictly greater);
- descending stable sort; equal scores keep the existing row first;
- name entry accepts up to 20 characters; an empty name is preserved.

The browser stores a JSON array of exactly ten `{ name, score }` rows under
`cc1.hiscores.v1`.  `wasm/persist.js` requires exactly ten valid rows; a
missing/corrupt table is replaced by the default ten rows (the original
all-or-nothing file read).  Scores are 32-bit (`0..0xFFFFFFFF`); names are
sanitized to printable ASCII and truncated to 20 characters.  The screen
format and defaults are unchanged, so `notes/hiscore_fixtures.json` and
`make test-exactness` still apply.

## Original CC1.CFG semantics

`CC1.CFG` is **12 bytes** at `DS:0x1730`:

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | sound on/off `[0x513E]` |
| 1 | 1 | joystick on/off `[0x338A]` |
| 2 | 2 | move-left scan code `[0x2B6C]` |
| 4 | 2 | move-right scan code `[0x2B6E]` |
| 6 | 2 | jump scan code `[0x2B70]` |
| 8 | 2 | video mode `[0xA0EC]` |
| 10 | 2 | fire scan code `[0x2B7A]` |

Defaults: sound 1, joystick 0, left `0x4B`, right `0x4D`, jump `0x1D`
(Ctrl), video 0, fire `0x38` (Alt).

The browser stores the same fields as JSON under `cc1.config.v2` with
`version: 2`.  `wasm/persist.js` validates sound/joystick/video as 0/1 and the
four keys as integers in `0..0x7F`; invalid fields fall back to defaults.
`cc1.config.v1` (Phase 5, browser `keyCode` values) is migrated by preserving
sound/joystick/video and resetting the bindings to the DOS defaults.  A
future version under the same key is ignored.

## Browser substitutions

- DOS file I/O, BBS dialing, ordering and foreign-order network calls are not
  performed.  The original screens and page sequences are shown unchanged.
- Quit to DOS shows the original "Quit to DOS (Y-N)" confirmation.  On Y the
  browser stops at an exit screen (the original returns to the DOS prompt);
  any key restarts the attract sequence.
- Joystick uses the Gamepad API and is gated by the persisted joystick toggle.
- Video is a real persisted setting: the compatibility screen's Y/N prompt
  writes the alternate-video flag (`[0xA0EC]`).  The browser renderer has no
  alternate mode, so the flag does not change the output.
- The `^` marker is the input position; the original 5-frame cursor animation
  (F2 glyphs 45..49, one frame every 21 ticks) is drawn there by the shell
  renderer.  Glyph 49 is a byte-for-byte duplicate of glyph 46 in the original
  F2 bank, so the cycle has four distinct images.

## Tests

```sh
cd wasm
make test-persist          # Phase 7 save/high-score/config suite
make test-shell            # shell flow + save/restore + messages
make test-exactness        # original high-score defaults/sort/tie/name entry
make test-all              # full regression
```

`make test-save` and `make test-hiscore` are compatibility aliases for the
Phase 7 suite plus the original high-score rules.
