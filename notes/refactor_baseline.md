# Refactor baseline (Phase 0)

Captured before any source refactoring, as required by
[`refactor/phase-0-safety-net.md`](../refactor/phase-0-safety-net.md) §0.1.
This file is the size/performance/export reference for every later phase.

## Checkout and toolchain

| Item | Value |
|---|---|
| Baseline commit | `76ff4fd71c8517c660f77cc3e2e33ce2da651060` |
| Branch | `refactor` (baseline tag recommended: `refactor-baseline`) |
| Date | 2026-11-02 |
| Platform | Darwin arm64 (macOS) |
| Compiler | Homebrew clang 23.1.4 (`/opt/homebrew/opt/llvm/bin/clang`) |
| Node | v26.9.0 |
| Python | 3.12.1 |

Phase 0 itself changed no gameplay behavior.  The only source edits were the
two dead `static` functions in `shell.c` (removed) and one added export
(`cc_warp_level`, already declared in `objects.h`, needed by the digest test).

## Build size

| Build | `render.wasm` |
|---|---:|
| Baseline commit, before Phase 0 edits | 159,030 bytes |
| Phase 0 harness (layout asserts, dead-code removal, digest export) | 159,047 bytes |

`make perf` thresholds (`wasm/perf_baseline.json`): 524,288 bytes maximum.

## Frame timings

`make perf` runs the same per-frame work as `game.html` (level view cells,
object blits, HUD, shell render) for 300 iterations after 30 warm-up frames.
Timings are machine-dependent; compare trends on the same host, not absolute
numbers across machines.

Baseline commit (single run):

```text
render.wasm: 159030 bytes
memory: 256 pages (16777216 bytes)
game frame budget: 57.1 ms (17.51 Hz, 8 x 0x2147 PIT ticks)
  level 2 frame      avg 1.199 ms  worst 1.618 ms
  level 3 frame      avg 1.149 ms  worst 1.425 ms
  shell render       avg 0.079 ms  worst 0.106 ms
PASS: wasm size and frame budget within thresholds
```

Phase 0 harness (same host; the 4.9 ms worst case is OS scheduling noise):

```text
render.wasm: 159047 bytes
memory: 256 pages (16777216 bytes)
  level 2 frame      avg 1.195 ms  worst 4.883 ms
  level 3 frame      avg 1.184 ms  worst 1.611 ms
  shell render       avg 0.086 ms  worst 1.258 ms
PASS: wasm size and frame budget within thresholds
```

Use `avg` for regression decisions; `worst` is advisory on a shared machine.
The budget is 57.1 ms per game frame and the guardrails are 10 ms average /
50 ms worst.

## Test suite result

`make test-all` on the Phase 0 tree: **PASS** (exit 0, 456 individual PASS
lines).  Targets exercised: `test-data`, `test-render-all` (all 19 levels
pixel-exact), `test-view`, `test-objects`, `test-gameplay`, `test-integration`,
`test-physics`, `test-combat`, `test-handlers`, `test-loader`, `test-caves`,
`test-cells`, `test-digest`, `test-hud`, `test-messages`, `test-text`,
`test-menus`, `test-shell`, `test-shell-flow`, `test-cursor`, `test-persist`,
`test-exactness`, `test-sound`.

Fixture coverage at the baseline:

| Fixture / test | Coverage |
|---|---|
| `test_render_all.js` | 19 static levels pixel-exact |
| `test_physics.js` | 19 level starts, 17 scenarios / 693 frames |
| `test_combat.js` | 74 scenarios / 624 frames |
| `test_handlers.js` | 235 scenarios / 376 frames |
| `test_loader.js` | 32 transitions |
| `cave_fixtures.json` | 16 caves / 370 frames |
| `test_cells.js` | 23 scenarios / 37 frames |
| `test_refactor_digest.js` | 929 SHA-256 snapshots (new in Phase 0) |

`make test-browser` also passes on this host (Playwright/Chromium), including
title → hub → save/restore → finale and the touch-control flow.

The digest was validated by a temporary one-frame behavior mutation
(`walk_speed` + 1): it failed immediately with per-snapshot state mismatches,
and passed again after the source was restored.  The golden master therefore
catches real gameplay drift, not just hash-file edits.

## Public wasm exports

184 exports (183 functions + `memory`), sorted:

```text
cc_alloc cc_ammo cc_cam_x cc_cam_y cc_cave_flag cc_cell_byte cc_cell_set
cc_collectibles_left cc_collectibles_total cc_collide_player cc_cooldown
cc_crystals_collected cc_crystals_remaining cc_cur_level cc_door_active
cc_draw_object cc_draw_object_view cc_draw_screen cc_exit_active
cc_exit_unlocked cc_framebuffer cc_g_2da cc_g_a0c4 cc_g_a0ff cc_global_get
cc_global_set cc_handler_coverage_num cc_health cc_hud_render
cc_hud_render_screen cc_hud_tick cc_input_scan cc_input_set cc_level_arg
cc_level_complete cc_level_exit cc_lives cc_load_arg cc_move_dir cc_obj_count
cc_obj_cursor cc_obj_draw_shape cc_obj_get cc_obj_handler cc_obj_kind
cc_obj_p10 cc_obj_p12 cc_obj_p14 cc_obj_p16 cc_obj_p18 cc_obj_p1a cc_obj_p24
cc_obj_ready cc_obj_set cc_obj_set_cursor cc_obj_shape cc_obj_speed
cc_obj_state cc_obj_tile cc_obj_vx cc_obj_vy cc_obj_x cc_obj_y
cc_objects_init cc_objects_step cc_objects_step_full cc_objects_step_objects
cc_objects_step_physics cc_phys_get cc_phys_set cc_player_draw_sound
cc_player_x cc_player_y cc_render_cells cc_render_level cc_render_view
cc_render_view_cells cc_rng_hi cc_rng_lo cc_rng_next cc_rng_seed cc_score
cc_score_hi cc_score_lo cc_set_assets cc_set_cells cc_set_level_overlay
cc_set_palette cc_set_player cc_set_screens cc_shell_action_for_scan
cc_shell_config_clear_dirty cc_shell_config_dirty cc_shell_finale_done
cc_shell_hiscore_add cc_shell_hiscore_count cc_shell_hiscore_name
cc_shell_hiscore_qualifies cc_shell_hiscore_score cc_shell_hiscore_set
cc_shell_hud_active cc_shell_init cc_shell_joystick_on cc_shell_key
cc_shell_key_for_action cc_shell_key_name cc_shell_key_redefine_active
cc_shell_key_redefine_step cc_shell_key_scan cc_shell_message
cc_shell_message_block cc_shell_overlay cc_shell_page_count
cc_shell_page_index cc_shell_page_line cc_shell_page_line_count
cc_shell_render cc_shell_request_done cc_shell_request_kind
cc_shell_request_pending cc_shell_request_slot cc_shell_scroll_dir
cc_shell_scroll_phase cc_shell_scroll_row cc_shell_set_config
cc_shell_set_key cc_shell_sound_on cc_shell_state cc_shell_step
cc_shell_step_ticks cc_shell_video_alt cc_shell_warp cc_sound_current_bank
cc_sound_current_id cc_sound_current_priority cc_sound_current_slot
cc_sound_current_ticks cc_sound_enabled cc_sound_end cc_sound_event_at
cc_sound_event_clear cc_sound_event_count cc_sound_init cc_sound_log_at
cc_sound_log_clear cc_sound_log_count cc_sound_log_total cc_sound_play
cc_sound_playing cc_sound_reset cc_sound_set_callback cc_sound_set_enabled
cc_sound_stop cc_sound_tick cc_sound_unknown_hits cc_state_last_error
cc_state_load cc_state_load_ex cc_state_save cc_state_size
cc_test_call_handler cc_test_cell_event cc_test_contact cc_test_fill_source
cc_test_spawn_2002d cc_test_spawn_47d cc_tex_height cc_tex_width
cc_text_block_lines cc_text_draw_block cc_text_draw_frame cc_text_draw_lines
cc_text_draw_string cc_text_set_fonts cc_unknown_handler_hits cc_view_clear
cc_view_draw_glyph cc_view_fill_rect cc_view_framebuffer cc_view_height
cc_view_set_pixel cc_view_width cc_warp_level memory
```

Guardrail: **do not rename or remove these exports**.  Adding one is allowed
when a test hook is genuinely required; record it here when it happens.
