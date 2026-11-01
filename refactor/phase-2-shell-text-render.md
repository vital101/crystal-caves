# Phase 2 — Split shell, text and render

## Goal

Apply the same mechanical discipline to the remaining large files. No renaming
or behavior changes in this phase.

Start each file with a path-only commit:

```sh
git mv wasm/shell.c wasm/shell/shell.c
git mv wasm/text.c  wasm/text/text.c
git mv wasm/render.c wasm/render/render.c
```

Update the Makefile and include paths. Build and run the relevant tests before
splitting each file.

## 2.1 `shell.c` (1,244 lines) → `wasm/shell/`

| File | Contents |
|---|---|
| `shell/state.c` | `cc_shell_init`, `cc_shell_step`, `cc_shell_step_ticks`, page setup, story/message helpers, `notify_*` |
| `shell/input.c` | `cc_shell_key`, per-state key handling, `text_key`, `scan_from_ascii`, `cc_shell_key_scan`, key redefine |
| `shell/render.c` | `cc_shell_render`, draw helpers, overlay/page accessors |
| `shell/hiscore.c` | high-score table, name entry, sorting, draw |
| `shell/config.c` | sound/joystick/video config and dirty flag |
| `shell/persist.c` | save/restore request handshake |
| `shell/scroll.c` | title/credits scroll + input cursor animation |
| `shell/shell.c` | thin public facade (~150 lines) |

### Function mapping

- **state.c:** `cursor_reset`, `cursor_step`, `scopy`, `clear_pages`,
  `add_block_pages`, `set_pages`, `set_story_pages`, `show_block`,
  `enter_high_score`, `enter_ending`, `begin_ending_fanfare`,
  `cc_shell_init`, `cc_shell_state`, `cc_shell_message_block`,
  `prompt_block`, `cc_shell_page_*`, `cc_shell_overlay`,
  `cc_shell_hud_active`, `cc_shell_message`, `cc_shell_notify_*`,
  `cc_shell_finale_done`.
- **input.c:** `lower`, `scan_from_ascii`, `start_intro_story`,
  `start_video_prompt`, `start_quit_confirm`, `start_instructions`,
  `start_finale_story`, `message_escape_only`, `text_key`, `cc_shell_key_scan`,
  `cc_shell_key`, `cc_shell_warp`.
- **render.c:** `draw_hiscore_screen`, `draw_name_entry`,
  `draw_block_centered_frame`, `draw_block_centered`,
  `draw_lines_centered_frame`, `draw_lines_centered`, `draw_prompt_text`,
  `cc_shell_render`.
- **hiscore.c:** `cc_shell_hiscore_*`, `g_hiscores`, `g_name`, `g_name_len`,
  sort/tie logic.
- **config.c:** `cc_shell_sound_on`, `cc_shell_joystick_on`,
  `cc_shell_video_alt`, `cc_shell_set_config`, `cc_shell_config_dirty`,
  `cc_shell_config_clear_dirty`, key tables.
- **persist.c:** `cc_shell_request_*`, save/restore state transitions.
- **scroll.c:** `scroll_reset`, `scroll_enter_next`, `scroll_advance`,
  `cc_shell_scroll_row/dir/phase`, cursor animation state.

### Shared shell state

The shell has many file-scope globals. For the mechanical split, create
`shell/shell_internal.h` with `extern` declarations. Do not redesign the state
machine yet; that belongs in Phase 3.

Keep `shell.h` unchanged. The public API and enum values must remain stable for
`game.html` and the tests.

## 2.2 `text.c` (381 lines) → `wasm/text/`

| File | Contents |
|---|---|
| `text/font.c/.h` | `cc_text_set_fonts`, `glyph`, `code_for_char`, `glyph_for_code`, `font_for_char` |
| `text/window.c/.h` | `g_scroll`, `draw_frame_cells`, `cc_text_draw_frame`, `draw_window_string`, `cc_text_draw_string`, `cc_text_block_lines`, `text_len`, `window_layout`, `draw_window_lines`, `window_cursor_cell`, `cc_text_cursor_draw`, `cc_text_draw_block_cursor`, `cc_text_draw_lines_cursor`, `cc_text_draw_block`, `cc_text_draw_lines` |
| `text/hud.c/.h` | `u32_digits`, `format_u32`, `gs`, `cc_hud_tick`, `hud_render_sub`, `cc_hud_render`, `cc_hud_render_screen` |
| `text/text.c` | thin public facade if needed |

Keep `text.h` unchanged.

## 2.3 `render.c` (310 lines) → `wasm/render/`

| File | Contents |
|---|---|
| `render/runtime.c/.h` | `memset`, `memcpy`, `g_heap`, `cc_alloc` |
| `render/blit.c/.h` | `shape_ptr`, `blit_to`, `blit`, `cc_draw_object`, `cc_draw_object_view`, `cc_view_draw_glyph` |
| `render/screens.c/.h` | `g_screens`, `cc_set_screens`, `view_pixel`, `cc_view_clear`, `cc_view_fill_rect`, `cc_view_set_pixel`, `cc_draw_screen`, `cc_draw_screen_rows` |
| `render/render.c` | asset setup, framebuffer accessors, `render_level_buf`, `render_cells_buf`, `cc_render_cells`, `cc_render_view_cells`, `cc_render_level`, `cc_render_view` |

Keep `render.h` unchanged. Replace the ad-hoc `extern int cc_cell_byte(...)` in
`render.c` with a proper include of `game/cell.h` or the public `objects.h`.

## 2.4 Move the sound attempt log

`cc_sound_note` is declared in `sound.h` but implemented in `objects.c`. Move
the implementation and its log/callback state into `sound.c`:

- `g_sound_callback`
- `g_sound_log`
- `g_sound_log_count`
- `g_sound_log_total`
- `cc_sound_set_callback`
- `cc_sound_log_clear`
- `cc_sound_log_count`
- `cc_sound_log_total`
- `cc_sound_log_at`

Keep `cc_sound_note` as the single logging entry point. `cc_sound_play`
already calls it; gameplay should call `cc_sound_play` (or the existing wrapper)
exactly as before. All `test_sound*`, `test_cells`, `test_handlers` and
`test_persist` expectations must stay green.

## 2.5 Test ladder

After each split:

```sh
make clang
make test-digest
make test-shell
make test-shell-flow
make test-menus
make test-hud
make test-text
make test-persist
make test-sound
```

Then:

```sh
make test-all
make test-browser        # if available
make perf
```

## Exit criteria

- `shell.c` is under 300 lines; `text.c`/`render.c` are under 250 lines.
- No shell/text/render file exceeds 600 lines.
- `shell.h`, `text.h`, `render.h` and `sound.h` public APIs are unchanged.
- All shell/text/HUD/menu/save/sound tests pass.
- `make test-digest` passes.
- No new compiler warnings.
