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
	  cc_text_set_fonts cc_text_draw_string cc_text_draw_block cc_text_draw_lines cc_text_draw_frame cc_text_block_lines cc_hud_tick cc_hud_render cc_hud_render_screen \
	  cc_shell_init cc_shell_step cc_shell_step_ticks cc_shell_key cc_shell_key_scan cc_shell_scroll_row cc_shell_scroll_dir cc_shell_scroll_phase cc_shell_render cc_shell_state cc_shell_message_block cc_shell_page_count cc_shell_page_index cc_shell_page_line_count cc_shell_page_line cc_shell_overlay cc_shell_hud_active cc_shell_key_for_action cc_shell_action_for_scan cc_shell_key_name cc_shell_set_key cc_input_scan cc_shell_key_redefine_active cc_shell_key_redefine_step \
	  cc_shell_request_pending cc_shell_request_kind cc_shell_request_slot cc_shell_request_done \
	  cc_shell_sound_on cc_shell_joystick_on cc_shell_video_alt cc_shell_set_config \
	  cc_shell_config_dirty cc_shell_config_clear_dirty \
	  cc_shell_hiscore_count cc_shell_hiscore_name cc_shell_hiscore_score cc_shell_hiscore_set cc_shell_hiscore_add cc_shell_hiscore_qualifies cc_shell_finale_done cc_shell_message cc_shell_warp \
	  cc_state_size cc_state_save cc_state_load cc_state_load_ex cc_state_last_error \
	  cc_objects_init cc_level_exit cc_load_arg cc_warp_level cc_objects_step cc_objects_step_full cc_player_draw_sound cc_objects_step_physics cc_objects_step_objects cc_test_cell_event cc_test_contact cc_test_call_handler cc_test_fill_source cc_unknown_handler_hits cc_test_spawn_2002d cc_test_spawn_47d cc_obj_count cc_obj_get cc_obj_set cc_obj_state cc_obj_x cc_obj_y \
	  cc_obj_kind cc_obj_shape cc_obj_tile cc_obj_p10 cc_obj_draw_shape cc_obj_p12 cc_obj_p14 cc_obj_p16 cc_obj_p18 cc_obj_p1a cc_obj_ready cc_obj_vx cc_obj_vy cc_obj_speed cc_obj_p24 cc_obj_cursor cc_obj_set_cursor cc_obj_handler cc_cur_level \
	  cc_input_set cc_set_player cc_player_x cc_player_y cc_cam_x cc_cam_y cc_move_dir cc_collide_player \
	  cc_draw_object cc_draw_object_view cc_sound_set_callback cc_sound_log_clear cc_sound_log_count cc_sound_log_total cc_sound_log_at \
	  cc_sound_init cc_sound_reset cc_sound_set_enabled cc_sound_enabled cc_sound_play cc_sound_tick cc_sound_end cc_sound_stop \
	  cc_sound_playing cc_sound_current_id cc_sound_current_priority cc_sound_current_bank cc_sound_current_slot cc_sound_current_ticks cc_sound_unknown_hits \
	  cc_sound_event_count cc_sound_event_at cc_sound_event_clear \
	  cc_global_get cc_global_set cc_rng_next cc_rng_seed cc_rng_lo cc_rng_hi \
	  cc_score cc_score_lo cc_score_hi cc_cooldown cc_g_2da cc_g_a0c4 cc_g_a0ff cc_collectibles_total cc_collectibles_left \
	  cc_lives cc_health cc_ammo cc_crystals_collected cc_crystals_remaining cc_level_arg cc_exit_unlocked cc_exit_active cc_door_active cc_cave_flag cc_level_complete cc_handler_coverage_num cc_phys_get cc_phys_set

# One source list per area (Phase 4): wildcard + sort keeps the link order
# deterministic.  DATA_SRC is empty until the generated-data definition .c
# files are added by the header-hygiene step; $(wildcard) picks them up then
# without touching this file again.
GAME_SRC   = $(sort $(wildcard game/*.c))
SHELL_SRC  = $(sort $(wildcard shell/*.c))
TEXT_SRC   = $(sort $(wildcard text/*.c))
RENDER_SRC = $(sort $(wildcard render/*.c))
DATA_SRC   = $(wildcard startup_data.c shell_text_data.c shell_exact_data.c)
SRC = $(RENDER_SRC) $(GAME_SRC) $(TEXT_SRC) $(SHELL_SRC) sound.c $(DATA_SRC)

CFLAGS_BASE = --target=wasm32 -Oz -nostdlib -ffreestanding -fno-builtin \
	 -Werror=implicit-function-declaration -Werror=incompatible-pointer-types \
	 -Werror=return-type
CFLAGS = $(CFLAGS_BASE) -I.
# Episode 2 build: the generated data headers live in ep2_data/ and must win
# over wasm/ for "startup_data.h" / "objects_data.h" / "cells_data.h".
CFLAGS_EP2 = $(CFLAGS_BASE) -Iep2_data -I.
CFLAGS_EP3 = $(CFLAGS_BASE) -Iep3_data -I.
LDFLAGS = -Wl,--no-entry -Wl,--no-gc-sections -Wl,--stack-first \
	  -Wl,--export-memory -Wl,--initial-memory=16777216 -Wl,-z,stack-size=65536 \
	  $(foreach s,$(EXPORTS),-Wl,--export=$(s))

OBJ = $(addprefix obj/,$(SRC:.c=.o))

clang: assets $(OUT)

$(OUT): $(OBJ)
	@test -x "$(LLVM)/clang" || { echo "clang not found at $(LLVM)/clang"; \
	  echo "Install Homebrew LLVM (brew install llvm) or pass LLVM=/path/to/bin"; exit 1; }
	$(LLVM)/clang $(CFLAGS) $(LDFLAGS) -o $@ $(OBJ)

# Compile each source to an object with dependency files, so moving or
# renaming a header cannot leave a stale $(OUT) behind.
obj/%.o: %.c
	@mkdir -p $(dir $@)
	$(LLVM)/clang $(CFLAGS) -MMD -MP -c $< -o $@

-include $(OBJ:.o=.d)

# --- Episode 2 build ------------------------------------------------------
# Same sources compiled against the generated Episode 2 data headers.  The
# portable assets (levels/cells/gfx/overlay/screens) stay runtime-loaded; the
# ep2 asset set is fed to the harnesses from ../out/portable_ep2.
EP2_DATA = ep2_data
EP2_OUT  = render_ep2.wasm
EP2_OBJ  = $(addprefix obj_ep2/,$(SRC:.c=.o))

ep2: $(EP2_OUT)

$(EP2_OUT): $(EP2_OBJ)
	@test -x "$(LLVM)/clang" || { echo "clang not found at $(LLVM)/clang"; \
	  echo "Install Homebrew LLVM (brew install llvm) or pass LLVM=/path/to/bin"; exit 1; }
	$(LLVM)/clang $(CFLAGS_EP2) $(LDFLAGS) -o $@ $(EP2_OBJ)

obj_ep2/%.o: %.c
	@mkdir -p $(dir $@)
	$(LLVM)/clang $(CFLAGS_EP2) -MMD -MP -c $< -o $@

-include $(EP2_OBJ:.o=.d)

$(EP2_OBJ): $(EP2_DATA)/startup_data.h $(EP2_DATA)/objects_data.h \
	    $(EP2_DATA)/cells_data.h $(EP2_DATA)/episode_data.h \
	    $(EP2_DATA)/shell_text_data.h $(EP2_DATA)/shell_exact_data.h

$(EP2_DATA)/shell_text_data.h: ../extracted/ep2/CC2-UNLZ.EXE \
	    ../notes/ep2/menu_fixtures.json
	@mkdir -p $(EP2_DATA)
	$(PYTHON) ../tools/export_shell_assets.py --episode ep2

$(EP2_DATA)/shell_exact_data.h: ../notes/ep2/shell_exactness_fixtures.json
	@mkdir -p $(EP2_DATA)
	$(PYTHON) ../tools/make_shell_exact_data.py --episode ep2

# Episode 2 browser asset pack (assets_ep2/).  Copy-only: the portable set is
# produced by the tools/ ep2 pipeline (see notes/ep2_diff.md).
EP2_ASSETS = assets_ep2
EP2_ASSET_FILES = gfx.bin levels.bin cells.bin level_overlay.bin \
		  palette.json screens.bin mni_f1.bin mni_f2.bin \
		  mni_spl.bin sound.json

ep2-assets: $(addprefix $(EP2_ASSETS)/,$(EP2_ASSET_FILES))

$(EP2_ASSETS)/%.bin: ../out/portable_ep2/%.bin
	@mkdir -p $(EP2_ASSETS)
	cp $< $@

$(EP2_ASSETS)/palette.json: ../out/portable_ep2/palette.json
	@mkdir -p $(EP2_ASSETS)
	cp $< $@

$(EP2_ASSETS)/sound.json: ../out/portable/sound.json
	@mkdir -p $(EP2_ASSETS)
	cp $< $@

# Regenerators for the ep2 portable artifacts the copies depend on.
../out/portable_ep2/screens.bin: ../extracted/ep2/CC2-UNLZ.EXE \
	    ../notes/ep2/menu_fixtures.json
	$(PYTHON) ../tools/export_shell_assets.py --episode ep2

../out/portable_ep2/level_overlay.bin: ../notes/ep2/loader_tags.json
	$(PYTHON) ../tools/export_level_overlay.py --episode ep2

# The sound banks are byte-identical between the episodes; export_sound.py is
# Episode-1-only, so the ep2 portable set reuses the same sound.json.
../out/portable_ep2/sound.json: ../out/portable/sound.json
	@mkdir -p ../out/portable_ep2
	cp $< $@

$(EP2_DATA)/startup_data.h: ../notes/ep2/startup.json
	@mkdir -p $(EP2_DATA)
	$(PYTHON) ../tools/make_startup_data.py --episode ep2 --out $@

$(EP2_DATA)/episode_data.h:
	@mkdir -p $(EP2_DATA)
	$(PYTHON) ../tools/make_episode_data.py --episode ep2 --out $@

../notes/ep2/oracle_catalog.json: ../notes/ep2/oracle_objects.json
	$(PYTHON) ../tools/build_oracle_catalog.py --episode ep2

$(EP2_DATA)/objects_data.h: ../notes/ep2/oracle_catalog.json
	@mkdir -p $(EP2_DATA)
	$(PYTHON) ../tools/make_objects_data.py --episode ep2 --out $@

$(EP2_DATA)/cells_data.h: ../notes/ep2/oracle_objects.json
	@mkdir -p $(EP2_DATA)
	$(PYTHON) ../tools/make_cells_data.py --episode ep2 --out $@

# --- Episode 3 build ------------------------------------------------------
EP3_DATA = ep3_data
EP3_OUT  = render_ep3.wasm
EP3_OBJ  = $(addprefix obj_ep3/,$(SRC:.c=.o))

ep3: $(EP3_OUT)

$(EP3_OUT): $(EP3_OBJ)
	@test -x "$(LLVM)/clang" || { echo "clang not found at $(LLVM)/clang"; \
	  echo "Install Homebrew LLVM (brew install llvm) or pass LLVM=/path/to/bin"; exit 1; }
	$(LLVM)/clang $(CFLAGS_EP3) $(LDFLAGS) -o $@ $(EP3_OBJ)

obj_ep3/%.o: %.c
	@mkdir -p $(dir $@)
	$(LLVM)/clang $(CFLAGS_EP3) -MMD -MP -c $< -o $@

-include $(EP3_OBJ:.o=.d)

$(EP3_OBJ): $(EP3_DATA)/startup_data.h $(EP3_DATA)/objects_data.h \
	    $(EP3_DATA)/cells_data.h $(EP3_DATA)/episode_data.h \
	    $(EP3_DATA)/shell_text_data.h $(EP3_DATA)/shell_exact_data.h

$(EP3_DATA)/shell_text_data.h: ../extracted/ep3/CC3-UNLZ.EXE \
	    ../notes/ep3/menu_fixtures.json
	@mkdir -p $(EP3_DATA)
	$(PYTHON) ../tools/export_shell_assets.py --episode ep3

$(EP3_DATA)/shell_exact_data.h: ../notes/ep3/shell_exactness_fixtures.json
	@mkdir -p $(EP3_DATA)
	$(PYTHON) ../tools/make_shell_exact_data.py --episode ep3

$(EP3_DATA)/startup_data.h: ../notes/ep3/startup.json
	@mkdir -p $(EP3_DATA)
	$(PYTHON) ../tools/make_startup_data.py --episode ep3 --out $@

$(EP3_DATA)/episode_data.h:
	@mkdir -p $(EP3_DATA)
	$(PYTHON) ../tools/make_episode_data.py --episode ep3 --out $@

$(EP3_DATA)/objects_data.h: ../notes/ep3/oracle_catalog.json
	@mkdir -p $(EP3_DATA)
	$(PYTHON) ../tools/make_objects_data.py --episode ep3 --out $@

$(EP3_DATA)/cells_data.h: ../notes/ep3/oracle_objects.json
	@mkdir -p $(EP3_DATA)
	$(PYTHON) ../tools/make_cells_data.py --episode ep3 --out $@

# Episode 3 browser asset pack (assets_ep3/).  Copy-only, like ep2; the sound
# banks are byte-identical to Episode 1, so sound.json is reused.
EP3_ASSETS = assets_ep3
EP3_ASSET_FILES = gfx.bin levels.bin cells.bin level_overlay.bin \
		  palette.json screens.bin mni_f1.bin mni_f2.bin \
		  mni_spl.bin sound.json

ep3-assets: $(addprefix $(EP3_ASSETS)/,$(EP3_ASSET_FILES))

$(EP3_ASSETS)/%.bin: ../out/portable_ep3/%.bin
	@mkdir -p $(EP3_ASSETS)
	cp $< $@

$(EP3_ASSETS)/palette.json: ../out/portable_ep3/palette.json
	@mkdir -p $(EP3_ASSETS)
	cp $< $@

$(EP3_ASSETS)/sound.json: ../out/portable/sound.json
	@mkdir -p $(EP3_ASSETS)
	cp $< $@

../out/portable_ep3/screens.bin: ../extracted/ep3/CC3-UNLZ.EXE \
	    ../notes/ep3/menu_fixtures.json
	$(PYTHON) ../tools/export_shell_assets.py --episode ep3

../out/portable_ep3/level_overlay.bin: ../notes/ep3/loader_tags.json
	$(PYTHON) ../tools/export_level_overlay.py --episode ep3

# Sound banks are byte-identical across episodes; reuse Episode 1's export.
../out/portable_ep3/sound.json: ../out/portable/sound.json
	@mkdir -p ../out/portable_ep3
	cp $< $@

assets: assets/gfx.bin assets/levels.bin assets/palette.json assets/cells.bin assets/level_overlay.bin assets/screens.bin assets/mni_f1.bin assets/mni_f2.bin assets/mni_spl.bin assets/sound.json

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
assets/sound.json: ../out/portable/sound.json
	@mkdir -p assets
	cp $< $@

# Episode screenshots for the launch picker (select.html).  Copy-only from
# the committed oracle renders: hub (L02) and cave (L06) per episode.
SHOTS = shots/ep1_hub.png shots/ep1_cave.png \
	shots/ep2_hub.png shots/ep2_cave.png \
	shots/ep3_hub.png shots/ep3_cave.png

shots: $(SHOTS)

shots/ep1_hub.png: ../out/oracle_L02.png
	@mkdir -p shots
	cp $< $@
shots/ep1_cave.png: ../out/oracle_L06.png
	@mkdir -p shots
	cp $< $@
shots/ep2_hub.png: ../out/ep2/oracle_L02.png
	@mkdir -p shots
	cp $< $@
shots/ep2_cave.png: ../out/ep2/oracle_L06.png
	@mkdir -p shots
	cp $< $@
shots/ep3_hub.png: ../out/ep3/oracle_L02.png
	@mkdir -p shots
	cp $< $@
shots/ep3_cave.png: ../out/ep3/oracle_L06.png
	@mkdir -p shots
	cp $< $@

# Complete web deployment bundle: the launch picker, the game pages and all
# three episodes' modules and asset packs — exactly what the production
# image serves.  CI uploads dist/crystal-caves-web.tar.gz on tag builds.
WEB_FILES = select.html game.html game.js keymap.js legend.js persist.js \
	    sound_synth.js audio.js touch.js manifest.webmanifest \
	    favicon.ico favicon.svg favicon-16.png favicon-32.png \
	    favicon-192.png favicon-512.png apple-touch-icon.png \
	    social-card.png

bundle-web: $(OUT) $(EP2_OUT) $(EP3_OUT) ep2-assets ep3-assets shots
	rm -rf dist
	mkdir -p dist/site
	cp $(WEB_FILES) dist/site/
	cp -R game dist/site/game
	cp $(OUT) $(EP2_OUT) $(EP3_OUT) dist/site/
	cp -R assets assets_ep2 assets_ep3 dist/site/
	tar -czf dist/crystal-caves-web.tar.gz -C dist/site .
	@echo "bundle: dist/crystal-caves-web.tar.gz"

# Milo favicon set, decoded straight from the committed GFX asset.
# Only rerun when the sprite source changes; the icons are committed.
favicon:
	$(PYTHON) ../tools/make_favicon.py

# 1200x630 Open Graph card from the original title screen (link previews).
social-card:
	$(PYTHON) ../tools/make_social_card.py

# Regenerate the portable intermediate from the committed extracted assets.
data:
	$(PYTHON) ../tools/make_startup_data.py
	$(PYTHON) ../tools/make_episode_data.py
	$(PYTHON) ../tools/export_assets.py
	$(PYTHON) ../tools/export_shell_assets.py
	$(PYTHON) ../tools/make_shell_exact_data.py
	$(PYTHON) ../tools/export_level_overlay.py
	$(PYTHON) ../tools/export_sound.py

# Recover the exact 0xC3E startup state (Unicorn; writes notes/startup.json).
startup:
	$(PYTHON) ../tools/emulate_startup.py
	$(PYTHON) ../tools/make_startup_data.py

# Emscripten path (produces app.js + app.wasm); same source list, its own
# depfile (app.d) for header dependency tracking.
emcc: assets
	$(EMCC) $(SRC) -O2 -I. -MMD -MP -o app.js -s WASM=1 \
	  -Werror=implicit-function-declaration -Werror=incompatible-pointer-types \
	  -Werror=return-type \
	  -s EXPORTED_FUNCTIONS='[$(foreach s,$(EXPORTS),_$(s)),_malloc,_free]' \
	  -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","HEAPU8"]' \
	  -s ALLOW_MEMORY_GROWTH=1

-include app.d

test: clang
	for l in 0 1 9 13 18; do node test_render.js $$l; done

# Pixel-exact comparison against the checked-in oracle renders (all 19 levels).
test-render-all: clang
	node test_render_all.js

# Episode 2 static levels (skips when absent).
test-render-ep2: clang
	node test_render_all_ep2.js

# --- Episode 3 checks -----------------------------------------------------
test-render-ep3: clang
	CC_EP=ep3 node test_render_all_ep2.js

test-ep3-browser:
	node test_ep3_browser.js

# Transition overlay replay against the ep3 loader fixtures (~13 min Unicorn).
test-ep3-transitions:
	@if [ ! -f ../notes/ep3/loader_fixtures.json ]; then \
	  echo "SKIP test-ep3-transitions: notes/ep3/loader_fixtures.json missing"; \
	else \
	  CC_WASM=render_ep3.wasm CC_CELLS=../out/portable_ep3/cells.bin \
	  CC_OVERLAY=../out/portable_ep3/level_overlay.bin \
	  node test_loader.js ../notes/ep3/loader_fixtures.json; \
	fi

# Episode 3 checks.  Skips cleanly when the data is not present.
test-ep3:
	@if [ ! -f ../extracted/ep3/CC3-UNLZ.EXE ] || \
	    [ ! -f ../notes/ep3/oracle_objects.json ]; then \
	  echo "SKIP test-ep3: Episode 3 data not present"; \
	else \
	  $(MAKE) ep3 ep3-assets && \
	  $(MAKE) test-render-ep3 test-ep3-browser test-ep3-transitions && \
	  CC_EP=ep3 node test_ep2_data.js; \
	fi

# Episode 2 checks.  Skips cleanly when the data is not present.
test-ep2:
	@if [ ! -f ../extracted/ep2/CC2-UNLZ.EXE ] || \
	    [ ! -f ../notes/ep2/oracle_objects.json ]; then \
	  echo "SKIP test-ep2: Episode 2 data not present"; \
	else \
	  $(MAKE) ep2 ep2-assets && \
	  $(MAKE) test-render-ep2 test-ep2-browser test-ep2-transitions && \
	  node test_ep2_data.js; \
	fi

test-ep2-browser:
	node test_ep2_browser.js

# Transition overlay replay against the ep2 loader fixtures.  The fixture is
# generated by tools/loader_fixtures.py --episode ep2 (~13 min, Unicorn) and
# is committed under notes/ep2/; skip when it is absent.
test-ep2-transitions:
	@if [ ! -f ../notes/ep2/loader_fixtures.json ]; then \
	  echo "SKIP test-ep2-transitions: notes/ep2/loader_fixtures.json missing"; \
	else \
	  CC_WASM=render_ep2.wasm CC_CELLS=../out/portable_ep2/cells.bin \
	  CC_OVERLAY=../out/portable_ep2/level_overlay.bin \
	  node test_loader.js ../notes/ep2/loader_fixtures.json; \
	fi

test-data:
	$(PYTHON) ../tools/check_oracle.py

# Phase 4: the built module must expose exactly the Makefile EXPORTS list.
test-exports: clang
	node test_exports.js

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

test-messages: clang
	node test_messages.js

test-text: clang
	node test_text.js

# Menu information screens (ordering/foreign/BBS/about/video/quit-to-DOS).
test-menus: clang
	node test_menus.js

test-shell: clang
	node test_shell.js

# R2: exact title/credits scroll (committed oracle flow fixture).
test-shell-flow: clang
	node test_shell_flow.js

# R3: animated '^' input cursor (F2 glyphs 45..49, 21-tick cadence).
test-cursor: clang
	node test_cursor.js

# Phase 7 persistence: save format/round-trips, high scores, config.
test-persist: clang
	node test_persist.js

test-exactness: clang
	node test_exactness.js
	node test_keymap.js

test-keymap:
	node test_keymap.js

# Phase 6 sound: driver selection, exported trace, JS synth hashes.
test-sound: clang
	node test_sound.js
	node test_sound_shell.js

test-oracle-sound:
	$(PYTHON) ../tools/sound_fixtures.py
	$(PYTHON) ../tools/export_sound.py
	$(MAKE) clang
	node test_sound.js
	node test_sound_shell.js

test-oracle-exactness:
	$(PYTHON) ../tools/shell_exactness.py
	$(PYTHON) ../tools/make_shell_exact_data.py
	$(MAKE) clang
	node test_exactness.js
	node test_keymap.js

test-oracle-shell: clang
	$(PYTHON) ../tools/oracle_shell.py
	node test_hud.js
	node test_text.js

# Regenerate the menu information buffers and the text fixtures that use them.
test-oracle-menus:
	$(PYTHON) ../tools/shell_exactness.py --menus
	$(PYTHON) ../tools/export_shell_assets.py
	$(PYTHON) ../tools/oracle_shell.py
	$(MAKE) clang
	node test_menus.js
	node test_text.js

# R1: regenerate the real title-loop flow fixture, then the fast test.
test-oracle-shell-flow: clang
	$(PYTHON) ../tools/shell_exactness.py --flow
	node test_shell_flow.js

# Characterization golden master (Phase 0): full-state SHA-256 digests over
# scripted gameplay, transitions, shell flow and save round-trips.
test-digest: clang
	node test_refactor_digest.js

# Intentional behavior change: rewrite notes/refactor_digests.json.
test-digest-update: clang
	node test_refactor_digest.js --update

# Phase 2 characterization digests for the shell/text/render surface
# (static/cell renders, view ops, text drawing, screens, credits scroll).
# Goldens live in notes/refactor_render_digests.json; the Phase 0 file is
# never touched here.
test-render-digest: clang
	node test_render_digest.js

render-digest-update: clang
	node test_render_digest.js --update

# Source-size limits (Phase 0).  Warning-only until Phase 1/2 bring the files
# under the limits; `lint-strict` is the flip-the-switch variant for CI.
lint:
	$(PYTHON) ../tools/check_source_limits.py

lint-strict:
	$(PYTHON) ../tools/check_source_limits.py --strict

# Frame-budget / wasm-size measurement (same work as game.html per frame).
perf: clang
	node measure_perf.js

# Headless browser smoke test (Playwright; test-only npm dependency).
# test_browser.js prints SKIP with exit 0 when playwright is not installed.
test-browser: clang
	node test_browser.js

# Regenerate the framebuffer hashes after an intentional rendering change.
test-browser-update: clang
	node test_browser.js --update

# Compatibility aliases: save coverage lives in test_persist.js; the exact
# high-score table/sort/name-entry coverage lives in test_exactness.js.
test-save: test-persist
test-hiscore: test-persist test-exactness

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

test-all: test-data test-exports test-render-all test-view test-objects test-gameplay test-integration test-physics test-combat test-handlers test-loader test-caves test-cells test-digest test-hud test-messages test-text test-menus test-shell test-shell-flow test-cursor test-persist test-exactness test-sound
# NOTE: test-keymap is already run by test-exactness; keep it as its own target.

clean:
	rm -f $(OUT) $(EP2_OUT) app.js app.wasm app.d out_wasm_level*.png oracle_level*.json
	rm -rf obj obj_ep2 ep2_data shots dist

.PHONY: clang emcc ep2 ep2-assets ep3 ep3-assets assets shots bundle-web favicon social-card data startup test test-render-all test-render-ep2 test-ep2 test-ep2-browser test-ep2-transitions test-render-ep3 test-ep3 test-ep3-browser test-ep3-transitions test-data test-exports test-objects test-gameplay test-integration test-view test-physics test-combat test-handlers test-loader test-caves test-cells test-hud test-messages test-text test-menus test-shell test-persist test-exactness test-keymap test-cursor test-sound test-oracle-sound test-oracle-exactness test-oracle-shell test-oracle-shell-flow test-oracle-menus test-save test-hiscore test-oracle-cells test-oracle-physics test-oracle-combat test-oracle-handlers test-oracle-loader test-oracle-caves test-oracle-objects test-browser test-browser-update test-digest test-digest-update test-render-digest render-digest-update lint lint-strict perf test-all clean
