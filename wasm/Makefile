# Crystal Caves Episode 1 - wasm build (clean-room original)
#
# Two back-ends:
#   make clang   freestanding module via Homebrew LLVM (default; no Emscripten)
#   make emcc    Emscripten build (needs emcc on PATH)
#
# Portable assets come from ../tools/export_assets.py into ../out/portable,
# hence copied into assets/ for the browser loader.

LLVM   ?= $(shell brew --prefix llvm 2>/dev/null || echo /opt/homebrew/opt/llvm)/bin
EMCC   ?= emcc
PYTHON ?= $(if $(wildcard ../.venv/bin/python),../.venv/bin/python,python3)
OUT    ?= render.wasm

EXPORTS = cc_alloc cc_set_assets cc_set_palette cc_render_level cc_render_view cc_render_cells cc_render_view_cells cc_set_cells cc_set_level_overlay cc_cell_byte cc_cell_set cc_framebuffer cc_view_framebuffer cc_view_width cc_view_height cc_tex_width cc_tex_height \
	  cc_set_screens cc_draw_screen cc_view_clear cc_view_fill_rect cc_view_set_pixel cc_view_draw_glyph \
	  cc_text_set_fonts cc_text_draw_string cc_text_draw_block cc_text_draw_frame cc_text_block_lines cc_hud_render \
	  cc_shell_init cc_shell_step cc_shell_key cc_shell_render cc_shell_state cc_shell_overlay cc_shell_hud_active cc_shell_key_for_action cc_shell_set_key cc_shell_key_redefine_active cc_shell_key_redefine_step \
	  cc_shell_request_pending cc_shell_request_kind cc_shell_request_slot cc_shell_request_done \
	  cc_shell_sound_on cc_shell_joystick_on cc_shell_video_alt cc_shell_set_config \
	  cc_shell_hiscore_count cc_shell_hiscore_name cc_shell_hiscore_score cc_shell_hiscore_set cc_shell_hiscore_add cc_shell_hiscore_qualifies cc_shell_finale_done cc_shell_message cc_shell_warp \
	  cc_state_size cc_state_save cc_state_load \
	  cc_objects_init cc_level_exit cc_load_arg cc_objects_step cc_objects_step_full cc_objects_step_physics cc_objects_step_objects cc_test_cell_event cc_test_contact cc_test_call_handler cc_test_fill_source cc_unknown_handler_hits cc_test_spawn_2002d cc_test_spawn_47d cc_obj_count cc_obj_get cc_obj_set cc_obj_state cc_obj_x cc_obj_y \
	  cc_obj_kind cc_obj_shape cc_obj_tile cc_obj_p10 cc_obj_p12 cc_obj_p14 cc_obj_p16 cc_obj_p18 cc_obj_p1a cc_obj_ready cc_obj_vx cc_obj_vy cc_obj_speed cc_obj_p24 cc_obj_cursor cc_obj_set_cursor cc_obj_handler cc_cur_level \
	  cc_input_set cc_set_player cc_player_x cc_player_y cc_cam_x cc_cam_y cc_move_dir cc_collide_player \
	  cc_draw_object cc_draw_object_view cc_sound_set_callback cc_sound_log_clear cc_sound_log_count cc_sound_log_total cc_sound_log_at \
	  cc_global_get cc_global_set cc_rng_next cc_rng_seed cc_rng_lo cc_rng_hi \
	  cc_score cc_score_lo cc_score_hi cc_cooldown cc_g_2da cc_g_a0c4 cc_g_a0ff cc_collectibles_total cc_collectibles_left \
	  cc_lives cc_health cc_ammo cc_crystals_collected cc_crystals_remaining cc_level_arg cc_exit_unlocked cc_exit_active cc_door_active cc_cave_flag cc_level_complete cc_handler_coverage_num cc_phys_get cc_phys_set

clang: assets $(OUT)

$(OUT): render.c render.h objects.c objects.h objects_data.h cells_data.h startup_data.h text.c text.h shell.c shell.h shell_text_data.h
	@test -x "$(LLVM)/clang" || { echo "clang not found at $(LLVM)/clang"; \
	  echo "Install Homebrew LLVM (brew install llvm) or pass LLVM=/path/to/bin"; exit 1; }
	$(LLVM)/clang --target=wasm32 -O2 -nostdlib -ffreestanding -fno-builtin \
	  -Wl,--no-entry -Wl,--no-gc-sections -Wl,--stack-first -Wl,-z,stack-size=65536 \
	  -Wl,--export-memory -Wl,--initial-memory=16777216 -Wl,-z,stack-size=65536 \
	  $(foreach s,$(EXPORTS),-Wl,--export=$(s)) -o $(OUT) render.c objects.c text.c shell.c

assets: assets/gfx.bin assets/levels.bin assets/palette.json assets/cells.bin assets/level_overlay.bin assets/screens.bin assets/mni_f1.bin assets/mni_f2.bin assets/mni_spl.bin

assets/gfx.bin: ../out/portable/gfx.bin
	@mkdir -p assets
	cp $< $@
assets/levels.bin: ../out/portable/levels.bin
	@mkdir -p assets
	cp $< $@
assets/palette.json: ../out/portable/palette.json
	@mkdir -p assets
	cp $< $@
assets/cells.bin: ../out/portable/cells.bin
	@mkdir -p assets
	cp $< $@
assets/level_overlay.bin: ../out/portable/level_overlay.bin
	@mkdir -p assets
	cp $< $@
assets/screens.bin: ../out/portable/screens.bin
	@mkdir -p assets
	cp $< $@
assets/mni_f1.bin: ../out/portable/mni_f1.bin
	@mkdir -p assets
	cp $< $@
assets/mni_f2.bin: ../out/portable/mni_f2.bin
	@mkdir -p assets
	cp $< $@
assets/mni_spl.bin: ../out/portable/mni_spl.bin
	@mkdir -p assets
	cp $< $@

# Regenerate the portable intermediate from the committed extracted assets.
data:
	$(PYTHON) ../tools/make_startup_data.py
	$(PYTHON) ../tools/export_assets.py
	$(PYTHON) ../tools/export_shell_assets.py
	$(PYTHON) ../tools/export_level_overlay.py

# Recover the exact 0xC3E startup state (Unicorn; writes notes/startup.json).
startup:
	$(PYTHON) ../tools/emulate_startup.py
	$(PYTHON) ../tools/make_startup_data.py

# Emscripten path (produces app.js + app.wasm)
emcc: assets
	$(EMCC) render.c objects.c -O2 -o app.js -s WASM=1 \
	  -s EXPORTED_FUNCTIONS='[$(foreach s,$(EXPORTS),_$(s)),_malloc,_free]' \
	  -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","HEAPU8"]' \
	  -s ALLOW_MEMORY_GROWTH=1

test: clang
	for l in 0 1 9 13 18; do node test_render.js $$l; done

# Pixel-exact comparison against the checked-in oracle renders (all 19 levels).
test-render-all: clang
	node test_render_all.js

test-data:
	$(PYTHON) ../tools/check_oracle.py

test-objects: clang
	node test_objects.js

test-gameplay: clang
	node test_gameplay.js 2

test-integration: clang
	node test_integration.js 2

test-view: clang
	node test_view.js

test-physics: clang
	node test_physics.js

# Phase 5 HUD/shell/state regressions.
test-hud: clang
	node test_hud.js

test-shell: clang
	node test_shell.js

test-oracle-shell: clang
	$(PYTHON) ../tools/oracle_shell.py
	node test_hud.js

# Compatibility aliases: save/high-score coverage lives in test_shell.js.
test-save: test-shell
test-hiscore: test-shell

# Committed combat fixtures (fast; regenerate with test-oracle-combat).
test-combat: clang
	node test_combat.js

# Full-frame per-cave integration fixtures (Phase 4).
test-caves: clang
	node test_handlers.js ../notes/cave_fixtures.json

test-oracle-caves: clang
	$(PYTHON) ../tools/cave_fixtures.py
	node test_handlers.js ../notes/cave_fixtures.json

# Exact loader transition fixtures (Phase 4).
test-loader: clang
	node test_loader.js

test-oracle-loader: clang
	$(PYTHON) ../tools/loader_fixtures.py
	node test_loader.js

# Per-handler oracle fixtures (Phase 4).
test-handlers: clang
	node test_handlers.js

test-oracle-handlers: clang
	$(PYTHON) ../tools/handler_fixtures.py
	node test_handlers.js

# Cell/contact/exit fixtures and functional level transitions.
test-cells: clang
	node test_cells.js
	node test_levels.js
	node test_cells_render.js

test-oracle-cells: clang
	$(PYTHON) ../tools/cell_fixtures.py
	node test_cells.js
	node test_levels.js
	node test_cells_render.js

# Regenerate the combat oracle fixtures (needs Unicorn) and compare.
test-oracle-combat: clang
	$(PYTHON) ../tools/combat_fixtures.py
	node test_combat.js

# Regenerate the physics oracle fixtures (needs Unicorn) and compare.
test-oracle-physics: clang
	$(PYTHON) ../tools/physics_fixtures.py
	node test_physics.js

# Needs the Python oracle deps (tools/requirements.txt) and a venv.
test-oracle-objects: clang
	$(PYTHON) ../tools/emulate_game.py 2 10 --json > oracle_level2.json
	node test_oracle_objects.js oracle_level2.json
	$(PYTHON) ../tools/emulate_game.py 3 120 --json > oracle_level3.json
	node test_oracle_objects.js oracle_level3.json

test-all: test-data test-render-all test-view test-objects test-gameplay test-integration test-physics test-combat test-handlers test-loader test-caves test-cells test-hud test-shell

clean:
	rm -f $(OUT) app.js app.wasm out_wasm_level*.png oracle_level*.json

.PHONY: clang emcc assets data startup test test-render-all test-data test-objects test-gameplay test-integration test-view test-physics test-combat test-handlers test-loader test-caves test-cells test-hud test-shell test-oracle-shell test-save test-hiscore test-oracle-cells test-oracle-physics test-oracle-combat test-oracle-handlers test-oracle-loader test-oracle-caves test-oracle-objects test-all clean
