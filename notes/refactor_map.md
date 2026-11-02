# Gameplay refactor map (Phase 1 implemented)

This map records the mechanical split of `wasm/objects.c` into
`wasm/game/*.c`.  Line ranges refer to the original monolith at the
time of the split; match by function name after future edits.

## Modules

| Module | Original line ranges | Functions |
|---|---|---|
| `wasm/game/state.c` | 1-346 | `memcpy_`, `wadd`, `wsub`, `wmul`, `wneg`, `score_add`, `cc_set_cells`, `cell_rec`, `cell_byte_cur`, `cell_put`, `cell_word_cur`, `cell_word_put`, `cell_word_raw`, `cell_word_raw_put`, `cave_flag_get`, `cave_flag_set`, `cell_solid_cur` |
| `wasm/game/physics.c` | 347-849 | `anim_next`, `anim_select_shape`, `mark_redraw`, `visible_obj`, `proximity_test`, `proximity_mode2`, `cc_sound_note`, `play_sound`, `cc_input_set`, `input_commit`, `cc_cell_collide`, `player_clear`, `cell_solid_px`, `cell_special_px`, `player_anim_next`, `player_select_shape`, `sync_player_object`, `update_walk_speed`, `cc_vertical`, `cc_jump_vertical`, `cc_horizontal` |
| `wasm/game/handlers_platforms.c` | 850-1264 | `cell_solid_at`, `h_1fb9`, `h_2850`, `h_2877`, `h_6ff6`, `h_70e4`, `h_3f13`, `h_3d23`, `h_6afa`, `h_6d31`, `h_831`, `base_cell_stamp`, `reuse_free_slot`, `spawn_template_2002d`, `spawn_object_47d`, `spawn_effect` |
| `wasm/game/combat.c` | 1265-1708 | `damage_life`, `rect_overlap`, `transform_object`, `slash_gate`, `obj_safe`, `slash_state1`, `slash_state2`, `player_interact` |
| `wasm/game/handlers_interaction.c` | 1709-2084 | `h_3e70`, `h_4a4b`, `h_4a82`, `h_1fc2`, `h_323c`, `h_2176`, `cell_type_collectible`, `cell_test_200fe`, `push_player`, `tp_long_mul`, `tp_long_div`, `proximity_obj_dir`, `h_5855`, `h_5bbe`, `h_3be8`, `dig_cell`, `h_251d`, `cell_border_byte`, `cell_border_put`, `cell_w0_is_dig`, `h_2241` |
| `wasm/game/cell_event.c` | 2085-2619 | `helper_a110_clear`, `helper_a59d`, `helper_toggle_gravity`, `helper_toggle_kind`, `switch_pair`, `cell_event`, `hazard_cell`, `contact_frame` |
| `wasm/game/handlers_actors_a.c` | 2620-3066 | `h_6681`, `h_672b`, `h_69d3`, `h_298a`, `h_403a`, `h_55ec`, `h_3a2e`, `h_3f6c`, `h_2e25`, `h_2f83`, `h_4ad3`, `h_36b0`, `h_4eeb`, `h_52e7`, `h_6232`, `h_4c92` |
| `wasm/game/handlers_actors_b.c` | 3067-3550 | `h_5c8f`, `h_62f2`, `h_3416`, `h_2d37`, `h_2acf`, `h_2bd4`, `h_3904`, `h_47b9`, `h_32f6`, `cell_blocked_4145`, `h_4145`, `h_48fe`, `h_6458`, `h_4fd4`, `handler_for` |
| `wasm/game/update.c` | 3551-3927 | `step_objects`, `count_crystal_cells`, `load_arg`, `door_entry_frame`, `exit_to_hub_frame`, `cc_fire`, `cc_fire_timers`, `cc_status_timers`, `cc_objects_step_objects`, `cc_test_fill_source`, `cc_test_call_handler` |
| `wasm/game/update_death.c` | 3928-4205 | `cc_unknown_handler_hits`, `cc_test_spawn_2002d`, `cc_test_cell_event`, `cc_test_contact`, `cc_test_spawn_47d`, `cc_objects_step_physics`, `cc_objects_step`, `restore_checkpoint`, `reload_current_level`, `fade_latch`, `restart_sequence`, `death_animation`, `death_state1`, `death_state2`, `death_state3`, `cc_objects_step_full`, `cc_player_draw_sound`, `cc_collide_player` |
| `wasm/game/level.c` | 4206-4669 | `apply_startup_state`, `apply_level_state`, `cc_set_level_overlay`, `ov_rd16`, `ov_rd32`, `ov_obj_byte`, `apply_level_overlay`, `load_level`, `cc_objects_init`, `cc_objects_init_silent`, `cc_warp_level`, `cc_level_exit`, `cc_load_arg`, `cc_sound_set_callback`, `cc_sound_log_clear`, `cc_sound_log_count`, `cc_sound_log_total`, `cc_sound_log_at` |
| `wasm/game/api_accessors.c` | 4670-4779, 5112-5223 | `cc_obj_count`, `cc_obj_state`, `cc_obj_x`, `cc_obj_y`, `cc_obj_kind`, `cc_obj_shape`, `cc_obj_tile`, `cc_obj_p10`, `cc_obj_draw_shape`, `cc_obj_p12`, `cc_obj_p14`, `cc_obj_p16`, `cc_obj_p18`, `cc_obj_p1a`, `cc_obj_ready`, `cc_obj_vx`, `cc_obj_vy`, `cc_obj_speed`, `cc_obj_p24`, `cc_obj_cursor`, `cc_obj_set_cursor`, `cc_cur_level`, `cc_cell_byte`, `cc_cell_set`, `cc_obj_get`, `cc_obj_set`, `cc_set_player`, `cc_player_x`, `cc_player_y`, `cc_cam_x`, `cc_cam_y`, `cc_move_dir`, `cc_score`, `cc_score_lo`, `cc_score_hi`, `cc_cooldown`, `cc_g_2da`, `cc_g_a0c4`, `cc_g_a0ff`, `cc_collectibles_total`, `cc_collectibles_left`, `cc_lives`, `cc_health`, `cc_ammo`, `cc_crystals_collected`, `cc_crystals_remaining`, `cc_level_arg`, `cc_exit_unlocked`, `cc_exit_active`, `cc_door_active`, `cc_cave_flag`, `cc_level_complete`, `cc_phys_get`, `cc_phys_set` |
| `wasm/game/api_globals.c` | 4780-5033 | `cc_global_get`, `cc_global_set` |
| `wasm/game/rng.c` | 5034-5078 | `rng_next_hi`, `cc_rng_next`, `cc_rng_seed`, `cc_rng_lo`, `cc_rng_hi` |
| `wasm/game/handlers_table.c` | 5079-5111 | `cc_obj_handler`, `is_real_handler`, `cc_handler_coverage_num` |
| `wasm/game/save_state.c` | 5224-5370 | `rd16_`, `rd32_`, `wr16_`, `wr32_`, `crc32_`, `state_valid_`, `cc_state_size`, `cc_state_save`, `cc_state_load_ex`, `cc_state_load`, `cc_state_last_error` |

## Notes

- `wasm/objects.c` is removed from the tree; the modules above are the
  gameplay core.  Public wasm exports remain in the same names and are
  still declared in `wasm/objects.h`.
- `wasm/game/game_internal.h` is the shared umbrella header generated by
  `refactor/split_objects.py`.  Phase 4 replaces it with per-module headers.
- Handler dispatch remains a single `handler_for()` table in
  `wasm/game/handlers_table.c`; all 46 handlers are reachable.
- Pre-existing long functions moved unchanged in Phase 1 (`cell_event()`,
  `slash_state1()`, `cc_global_get/set()`, `apply_startup_state()`,
  `apply_level_overlay()`, `cc_horizontal()`); they were split in Phase 3,
  see the Phase 3 map below.
- The mechanical split grouped functions by contiguous line ranges, so some
  ownership boundaries differ from the target diagram in `refactor/README.md`
  (for example, cell accessors sit in `state.c`, collision helpers in
  `physics.c`, spawn helpers in `handlers_platforms.c`, and `handler_for()` in
  `handlers_actors_b.c`).  Semantic regrouping is Phase 3/4 work; the dispatch
  point remains single and all 46 handlers are reachable.
- `cc_sound_note` and the sound attempt log moved to `wasm/sound.c` in
  Phase 2.4; see the Phase 2 map below.

# Phase 2: shell/text/render split (implemented)

Path-only commits (`b8e96d2`) moved `shell.c`, `text.c` and `render.c` into
`wasm/shell/`, `wasm/text/` and `wasm/render/`; the splits below are
mechanical (commits `530b7e0`, `f3289df`, `a72853e`, `3fe447c`).  Public
headers `shell.h`, `text.h`, `render.h` (via `objects.h`) and `sound.h` keep
the same exports; `game.html` and the tests are untouched.

## shell/ (from shell.c, 1,244 lines)

Shared file-scope state and cross-module prototypes live in
`wasm/shell/shell_internal.h` (Phase 1 umbrella-header pattern).  The
file-scope `g_state` was renamed `g_shell_state` to avoid a link collision
with the gameplay `cc_state_t g_state`.

| Module | Functions |
|---|---|
| `shell/shell.c` | thin facade (documentation + single include) |
| `shell/state.c` | `slen`, `scopy`, `cursor_reset`, `cursor_step`, `clear_pages`, `add_block_pages`, `set_pages`, `set_story_pages`, `show_block`, `prompt_block`, `enter_high_score`, `enter_ending`, `begin_ending_fanfare`, `cc_shell_init`, `cc_shell_state`, `cc_shell_step`, `cc_shell_step_ticks`, `cc_shell_message_block`, `cc_shell_page_*`, `cc_shell_overlay`, `cc_shell_hud_active`, `cc_shell_message`, `cc_shell_notify_*`, `cc_shell_finale_done` |
| `shell/input.c` | `lower`, `scan_from_ascii`, `start_intro_story`, `start_video_prompt`, `start_quit_confirm`, `start_instructions`, `message_escape_only`, `text_key`, `cc_shell_key`, `cc_shell_key_scan`, `cc_shell_warp` |
| `shell/render.c` | `draw_hiscore_screen`, `draw_name_entry`, `draw_block_centered`, `draw_prompt_text`, `cc_shell_render` |
| `shell/hiscore.c` | `cc_shell_hiscore_count/name/score/set/add/qualifies` |
| `shell/config.c` | `cc_shell_sound_on`, `cc_shell_joystick_on`, `cc_shell_video_alt`, `cc_shell_config_dirty/clear_dirty`, `cc_shell_key_redefine_active/step`, `cc_shell_key_for_action`, `cc_shell_action_for_scan`, `cc_shell_key_name`, `cc_shell_set_key`, `cc_input_scan`, `cc_shell_set_config` |
| `shell/persist.c` | `cc_shell_request_pending/kind/slot/done` |
| `shell/scroll.c` | `scroll_reset`, `scroll_enter_next`, `scroll_advance`, `cc_shell_scroll_row/dir/phase` |

## text/ (from text.c, 381 lines)

| Module | Functions |
|---|---|
| `text/text.c` | thin facade (documentation + module includes) |
| `text/font.c` + `font.h` | `glyph`, `code_for_char`, `glyph_for_code`, `font_for_char`, shared F1/F2/SPL banks |
| `text/window.c` + `window.h` | `draw_frame_cells`, `cc_text_draw_frame`, `draw_window_string`, `cc_text_draw_string`, `cc_text_block_lines`, `cc_text_draw_block`, `cc_text_draw_lines`, `cc_text_draw_lines_cursor`, `cc_text_cursor_draw` |
| `text/hud.c` + `hud.h` | `u32_digits`, `format_u32`, `cc_hud_tick`, `hud_render_sub`, `cc_hud_render`, `cc_hud_render_screen` |

## render/ (from render.c, 1,015 lines)

Shared geometry defines (`TILE_W/TILE_H/SCREEN_W/SCREEN_H`), palette and the
two framebuffers live in `wasm/render/render_internal.h`; cell reads go
through `cc_cell_byte` in `objects.h` (the ad-hoc local `extern` is gone).

| Module | Functions |
|---|---|
| `render/render.c` | `cc_set_assets`, `cc_set_palette`, `cc_framebuffer`, `cc_tex_width/height`, `cc_view_framebuffer`, `cc_view_width/height`, `render_level_buf`, `cell_word`, `render_cells_buf`, `cc_render_cells`, `cc_render_view_cells`, `cc_render_level`, `cc_render_view` |
| `render/runtime.c` + `runtime.h` | `memset`, `memcpy`, `cc_alloc` (freestanding runtime) |
| `render/blit.c` + `blit.h` | `shape_ptr`, `blit`, `cc_draw_object`, `cc_draw_object_view`, `cc_view_draw_glyph` |
| `render/screens.c` + `screens.h` | `cc_set_screens`, `view_pixel`, `cc_view_clear`, `cc_view_fill_rect`, `cc_view_set_pixel`, `cc_draw_screen`, `cc_draw_screen_rows` |

## sound (Phase 2.4)

`cc_sound_note`, `g_sound_callback` and the 256-entry attempt log
(`g_sound_log*`) moved out of `game/physics.c`, `game/state.c` and
`game/level.c` into `wasm/sound.c`, together with the host accessors
`cc_sound_set_callback` / `cc_sound_log_clear/count/total/at`.  The
declarations stay in `objects.h` (host API) and `sound.h` (`cc_sound_note`);
the `game_internal.h` externs and duplicate prototype are gone.

## Phase 2 verification

- `make test-digest`: the same 929 Phase 0 digest snapshots pass before and
  after every split (shell, text and render).
- `make test-render-digest` (`wasm/test_render_digest.js`,
  `notes/refactor_render_digests.json`): 155 additional digests over the
  static/cell renders (19 levels, multiple cameras), raw view primitives,
  text drawing, PCX screens and the credits scroll.  The goldens were
  generated from the pre-split HEAD build and match the split builds.
- `make test-all`, `make test-browser` and `make perf` pass at each
  milestone; ~1.0 ms per level frame, 164,026-byte `render.wasm`.

# Phase 3: readability (implemented)

One seam per commit, each guarded by `make test-digest` (the 929 goldens
never changed) plus the subsystem suite for the touched area.

| Seam | Commit(s) | What moved |
|---|---|---|
| Sound effect IDs | `81639ac` | `wasm/sound_ids.h` enumerates the 36 `SND_*` ids; 77 raw literals in `play_sound()`/`cc_sound_play()` replaced |
| Cell-record fields | `7259fcc` | `wasm/game/cell_fields.h`: `CELL_W0..W9` word indices and `CELL_FLAG_*` bytes; every raw offset argument of the cell helpers (gameplay and renderer) replaced |
| Handler naming + dispatch | `05b378b` | every `h_XXXX` renamed descriptively; one `HandlerEntry g_handlers[]` table in `game/handlers_table.c` (offset → fn → original label); `handler_for()`/`handler_name()`/coverage all resolve through it; `notes/handlers.md` regenerated |
| `cc_state_t` renames | `183d778` | understandable names for the decoded fields (DS offsets kept as comments); layout asserts untouched; genuinely-unknown latches keep `g_a0XX`-style names with comments |
| `cell_event()` rules | `5ca609a` | magic-value `if`/`goto` chain → static rule table (`cell_rule_t`) + small per-rule helpers |
| Globals table | `e85b2d7` | `cc_global_get`/`cc_global_set` switch arms → `GlobalField g_global_fields[]` (DS offset, field, sign, width auto-derived from the field); player/attack DS aliases and the cave-flag range stay explicit rows/arms; fixed latent `0x5132` (signed) and `0xA0BE` (unsigned) read mismatches |
| Oversized functions | `2b3b2d1`…`6251846` | `cc_horizontal` → impulse/walk/carry helpers; `slash_state1` → per-kind arm/kill functions; `apply_startup_state` → state sections; `apply_level_overlay` → overlay-section functions (`OvCellTag`/`OvObjTag`); `cc_shell_key` → per-state handlers; `cc_shell_render` → `render_key_redefine`/`render_key_confirm`/`render_dos_screen` |
| Browser test hook | `a43234a` | `game.html` test-hook `stateHash` saves into a dedicated scratch buffer so it cannot clobber the buffer `CCPersist.saveSlot` uses |

After Phase 3 `make lint` reports zero function-length failures (CI runs
`lint-strict`); `cell_event.c` (583) and `physics.c` (517) remain over the
500-line *warning* threshold but under the 600-line hard cap — shrinking
them further is Phase 4 territory.
