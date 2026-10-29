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

EXPORTS = cc_alloc cc_set_assets cc_set_palette cc_render_level cc_render_view cc_set_cells cc_cell_byte cc_cell_set cc_framebuffer cc_view_framebuffer cc_view_width cc_view_height cc_tex_width cc_tex_height \
	  cc_objects_init cc_objects_step cc_objects_step_full cc_objects_step_physics cc_objects_step_objects cc_test_spawn_2002d cc_test_spawn_47d cc_obj_count cc_obj_get cc_obj_set cc_obj_state cc_obj_x cc_obj_y \
	  cc_obj_kind cc_obj_shape cc_obj_tile cc_obj_p10 cc_obj_p12 cc_obj_p14 cc_obj_p16 cc_obj_p18 cc_obj_p1a cc_obj_ready cc_obj_vx cc_obj_vy cc_obj_speed cc_obj_p24 cc_obj_cursor cc_obj_set_cursor cc_obj_handler cc_cur_level \
	  cc_input_set cc_set_player cc_player_x cc_player_y cc_cam_x cc_cam_y cc_move_dir cc_collide_player \
	  cc_draw_object cc_draw_object_view cc_sound_set_callback cc_sound_log_clear cc_sound_log_count cc_sound_log_total cc_sound_log_at \
	  cc_global_get cc_global_set cc_rng_next cc_rng_seed cc_rng_lo cc_rng_hi \
	  cc_score cc_score_lo cc_score_hi cc_cooldown cc_g_2da cc_g_a0c4 cc_g_a0ff cc_collectibles_total cc_collectibles_left \
	  cc_lives cc_level_complete cc_handler_coverage_num cc_phys_get cc_phys_set

clang: assets $(OUT)

$(OUT): render.c objects.c objects.h objects_data.h cells_data.h startup_data.h
	@test -x "$(LLVM)/clang" || { echo "clang not found at $(LLVM)/clang"; \
	  echo "Install Homebrew LLVM (brew install llvm) or pass LLVM=/path/to/bin"; exit 1; }
	$(LLVM)/clang --target=wasm32 -O2 -nostdlib -ffreestanding -fno-builtin \
	  -Wl,--no-entry -Wl,--no-gc-sections -Wl,--stack-first -Wl,-z,stack-size=65536 \
	  -Wl,--export-memory -Wl,--initial-memory=16777216 -Wl,-z,stack-size=65536 \
	  $(foreach s,$(EXPORTS),-Wl,--export=$(s)) -o $(OUT) render.c objects.c

assets: assets/gfx.bin assets/levels.bin assets/palette.json assets/cells.bin

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

# Regenerate the portable intermediate from the committed extracted assets.
data:
	$(PYTHON) ../tools/make_startup_data.py
	$(PYTHON) ../tools/export_assets.py

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

test-interaction: clang
	node test_interaction.js

test-physics: clang
	node test_physics.js

# Committed combat fixtures (fast; regenerate with test-oracle-combat).
test-combat: clang
	node test_combat.js

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

test-all: test-data test-render-all test-view test-objects test-gameplay test-integration test-interaction test-physics test-combat

clean:
	rm -f $(OUT) app.js app.wasm out_wasm_level*.png oracle_level*.json

.PHONY: clang emcc assets data startup test test-render-all test-data test-objects test-gameplay test-integration test-view test-interaction test-physics test-combat test-oracle-physics test-oracle-combat test-oracle-objects test-all clean
