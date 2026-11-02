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
- Pre-existing long functions moved unchanged; they are Phase 3 targets:
  `cell_event()`, `slash_state1()`, `cc_global_get/set()`,
  `apply_startup_state()`, `apply_level_overlay()`, `cc_horizontal()`.
- The mechanical split grouped functions by contiguous line ranges, so some
  ownership boundaries differ from the target diagram in `refactor/README.md`
  (for example, cell accessors sit in `state.c`, collision helpers in
  `physics.c`, spawn helpers in `handlers_platforms.c`, and `handler_for()` in
  `handlers_actors_b.c`).  Semantic regrouping is Phase 3/4 work; the dispatch
  point remains single and all 46 handlers are reachable.
- `cc_sound_note` still lives in `wasm/game/physics.c` even though it is
  declared in `sound.h`; moving it to the sound layer is Phase 2 work.
