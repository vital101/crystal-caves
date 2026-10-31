# Phase 6 — Sound

Status: implemented for the 36 Crystal Caves Sound effects, the in-game
single-channel priority/preemption rules, the sound toggle, shell effects and
the all-caves finale wait.  The exact source of truth is the committed
oracle trace; the browser synthesizes samples from it at load time.

## Driver map

| Item | Address / value |
|---|---|
| `play_sound_or_effect(id)` | `0x1F9A:0x021F` (linear `0x1FBBF`) |
| Bank loader | linear `0x1B340` (loads `CC1-1.SND`..`CC1-3.SND`) |
| Bank far pointers | `DS:0x510C`, `DS:0x5110`, `DS:0x5114` (array base `0x5108`, 1-based) |
| Sound ISR | `0x0A3F`, hooked to INT 1Ch |
| PIT channel 0 | divisor `0x2147` → 140.061 Hz (`0xFC24`) |
| Set frequency | `0x287F6`: `divisor = 1193181 / Hz`; Hz ≤ 18 is ignored |
| Silence | `0x28823`: clear port `0x61` bits 0–1 |
| PIT channel 2 mode | `0xB6`, LSB then MSB to port `0x42` |
| Enable flag | `[0x513E]` |
| Playing flag | `[0x513F]` |
| Slot / note index | `[0x5136]` / `[0x5138]` |
| Current bank | `[0xA0BC]` |
| Level exit clears playing | `0xEFCF` |
| Shell sound toggle | `0x115B6` / `0x115C2` |
| Finale wait | `0xFB33` (play ID 26, loop until `[0x513F] == 0`) |

## SND bank format

Each file is **12 records × 610 bytes**:

| Offset | Meaning |
|---:|---|
| `0x000..0x254` | note words (`uint16` Hz), terminated by `0xFFFF` |
| `0x256` | unused / second terminator |
| `0x258` (600) | priority |
| `0x25A` (602) | unused metadata |
| `0x25C` (604) | gate period (every Nth note is silenced) |
| `0x25E/0x260` | unused metadata (last slot contains garbage) |

The loader maps `id` to `bank = (id-1)/12 + 1` and
`slot = (id-1)%12 + 1`.  All 36 records are valid; IDs 1–12 are bank 1,
13–24 bank 2, 25–36 bank 3.

## Tick model

The real driver is captured by `tools/sound_fixtures.py`, which runs
`0x1FBBF` and the real INT 1Ch ISR in Unicorn.  For every tick it records the
PIT divisor last written and whether the speaker gate is on.  The model in
`tools/sound_banks.py` matches all 36 traces tick-for-tick:

```text
for each tick k = 1..N:
    note = notes[k-1]
    if note > 18:
        divisor = 1193181 // note
        speaker = 1
    if (k + 1) % gate == 0:
        speaker = 0
    if k == N:
        speaker = 0
    emit [divisor, speaker]
```

The first note is commonly silenced immediately by the gate; that is why the
audible alternation in ID 1 starts on note 2.  Do not “fix” this by dropping
the gate.

`out/portable/sound.json` stores the resulting trace; `wasm/sound_synth.js`
renders it with integer phase/boundary math.  The JS synth is checked against
the Python reference hashes in `notes/sound_synth_fixtures.json` at 44.1 and
48 kHz.

## Priority / preemption

`cc_sound_play(id)` in `wasm/sound.c` mirrors `0x1FBBF`:

- invalid ID → ignored/counted (`cc_sound_unknown_hits`);
- `[0x513E] == 0` → log the attempt, but do not start;
- not playing → always start;
- playing and `new_priority < current_priority` → ignore;
- playing and `new_priority >= current_priority` → stop current and start new.

The original is single-channel; the browser keeps one `AudioBufferSourceNode`.

## Sound toggle

The help `T` key and persisted config call `cc_sound_set_enabled()`.
The exact original semantics are preserved: turning sound off blocks future
effects but the current effect finishes.  The browser does not cut the current
buffer on toggle.

## Shell effects

`notes/sound_shell_fixtures.json` records the recovered call sites.  The
runtime mapping is:

| Event | ID |
|---|---:|
| Main menu N / Space / Enter (new game) | 33 |
| Text window / page rendered | 35 |
| High-score screen / name entry | 34 |
| Ending screen | 32 |
| All-caves fanfare (then wait) | 26 |
| Level start | 36 |

The full title/menu flow is not driven end-to-end by the P5 oracle; the
call sites are statically recovered and `wasm/test_sound_shell.js` asserts the
C integration.  ID 33 is played on new-game selection but the immediately
following story load clears `[0x513F]`, so it is usually cut in the fast
browser load, matching the original loader reset.

The finale fanfare is played after the last finale story page.  The shell
holds (`g_wait_sound`) until the driver's playing flag clears, then enters
`CC1.END`.  If sound is disabled the wait exits immediately, matching the
original `play_sound` early return.

## ID table

| ID | Bank | Slot | Priority | Ticks | Duration (s) |
|---:|---:|---:|---:|---:|---:|
| 1 | 1 | 1 | 5 | 29 | 0.207 |
| 2 | 1 | 2 | 5 | 12 | 0.086 |
| 3 | 1 | 3 | 15 | 184 | 1.314 |
| 4 | 1 | 4 | 15 | 221 | 1.578 |
| 5 | 1 | 5 | 25 | 280 | 1.999 |
| 6 | 1 | 6 | 10 | 50 | 0.357 |
| 7 | 1 | 7 | 5 | 15 | 0.107 |
| 8 | 1 | 8 | 10 | 15 | 0.107 |
| 9 | 1 | 9 | 12 | 101 | 0.721 |
| 10 | 1 | 10 | 12 | 43 | 0.307 |
| 11 | 1 | 11 | 10 | 71 | 0.507 |
| 12 | 1 | 12 | 13 | 28 | 0.200 |
| 13 | 2 | 1 | 5 | 19 | 0.136 |
| 14 | 2 | 2 | 5 | 28 | 0.200 |
| 15 | 2 | 3 | 6 | 55 | 0.393 |
| 16 | 2 | 4 | 5 | 25 | 0.178 |
| 17 | 2 | 5 | 5 | 25 | 0.178 |
| 18 | 2 | 6 | 10 | 293 | 2.092 |
| 19 | 2 | 7 | 10 | 89 | 0.635 |
| 20 | 2 | 8 | 22 | 33 | 0.236 |
| 21 | 2 | 9 | 10 | 119 | 0.850 |
| 22 | 2 | 10 | 6 | 57 | 0.407 |
| 23 | 2 | 11 | 5 | 25 | 0.178 |
| 24 | 2 | 12 | 4 | 13 | 0.093 |
| 25 | 3 | 1 | 6 | 56 | 0.400 |
| 26 | 3 | 2 | 5 | 149 | 1.064 |
| 27 | 3 | 3 | 20 | 298 | 2.128 |
| 28 | 3 | 4 | 6 | 44 | 0.314 |
| 29 | 3 | 5 | 10 | 127 | 0.907 |
| 30 | 3 | 6 | 6 | 31 | 0.221 |
| 31 | 3 | 7 | 5 | 40 | 0.286 |
| 32 | 3 | 8 | 0 | 94 | 0.671 |
| 33 | 3 | 9 | 0 | 251 | 1.792 |
| 34 | 3 | 10 | 10 | 285 | 2.035 |
| 35 | 3 | 11 | 7 | 23 | 0.164 |
| 36 | 3 | 12 | 30 | 76 | 0.543 |

Trigger coverage (all 36 IDs classified):

- 34 IDs are emitted by the C gameplay/shell code and covered by the combat,
  handler, cell, cave and shell fixtures.
- **ID 7** is a player sprite-draw effect at `0x0F74C/0x0F757` (the second
  player-draw path taken when the animation counter `[0x45E0]` is non-zero;
  `[0x2C0]` is the player index, `[0xA0EA]` the invulnerability timer).  The
  original plays it while drawing the animated sprite, not from the update
  routine, so the committed step fixtures (which exclude the draw) do not
  contain it.  The host renderer calls `cc_player_draw_sound()` once per game
  step; `wasm/test_sound_shell.js` covers the hook.
- **ID 2** has no far call site, no near call site (`push cs; call 0x021F`
  in segment `0x1F9A`) and no data-table reference in the load module.  It is
  an unused bank entry; `wasm/test_sound_shell.js` asserts no tested path emits it.
- The title/credits attract loop (`0x0FC5:0x8265`) is silent; the Apogee logo
  plays **ID 32** at `0x1AAEB` before its 721-tick hold, and menu accept plays
  **ID 33** at `0x17773`.  `notes/sound_shell_fixtures.json` records both.

## Browser integration

`game.html`:

- fetches `assets/sound.json` with the other assets;
- creates `CCAudio` on first key/pointer gesture (autoplay policy);
- polls `cc_sound_event_*` after each game step and plays accepted IDs;
- advances `cc_sound_tick()` from a 140.06 Hz wall-clock accumulator,
  independent of the 17.5 Hz game frame;
- stops the current buffer when the C driver reports not playing, and on
  level load/exit/restore/quit;
- keeps audio through pause/help, matching the original timer ISR;
- `onended` calls `cc_sound_end(id)` as a safety net.

`audio.js` renders buffers at the context sample rate and keeps a single
source.  `sound_synth.js` has no browser dependencies and is also used by the
Node regression.

## Tests

```sh
cd wasm
make test-sound            # C driver + assets + JS synth + shell integration
make test-oracle-sound     # regenerate the Unicorn trace and re-run
make test-all
```

Committed data:

- `notes/sound_fixtures.json` — 36 exact tick traces, priorities, selection
  matrix, EXE hash;
- `notes/sound_synth_fixtures.json` — Python-reference synth hashes at 44.1
  and 48 kHz;
- `notes/sound_shell_fixtures.json` — statically recovered shell call sites;
- `out/portable/sound.json` — runtime trace asset;
- `wasm/sound_data.h` — generated priority/tick-count table.
