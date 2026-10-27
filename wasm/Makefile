# Crystal Caves Episode 1 - wasm build (clean-room original)
#
# Two back-ends:
#   make clang   freestanding module via Homebrew LLVM (default; no Emscripten)
#   make emcc    Emscripten build (needs emcc on PATH)
#
# Portable assets come from ../tools/export_assets.py into ../out/portable,
# hence copied into assets/ for the browser loader.

LLVM   ?= /opt/homebrew/opt/llvm/bin
EMCC   ?= emcc
OUT    ?= render.wasm

EXPORTS = cc_alloc cc_set_assets cc_set_palette cc_render_level cc_framebuffer cc_tex_width cc_tex_height \
	  cc_objects_init cc_objects_step cc_obj_count cc_obj_state cc_obj_x cc_obj_y \
	  cc_obj_kind cc_obj_shape cc_obj_tile cc_obj_p10 cc_obj_p18 cc_obj_ready cc_obj_vx cc_obj_cursor \
	  cc_input_set cc_player_x cc_player_y cc_cam_x cc_cam_y cc_move_dir cc_collide_player

clang: assets $(OUT)

$(OUT): render.c objects.c objects.h
	$(LLVM)/clang --target=wasm32 -O2 -nostdlib -ffreestanding -fno-builtin \
	  -Wl,--no-entry -Wl,--no-gc-sections -Wl,--stack-first -Wl,-z,stack-size=65536 \
	  -Wl,--export-memory -Wl,--initial-memory=16777216 -Wl,-z,stack-size=65536 \
	  $(foreach s,$(EXPORTS),-Wl,--export=$(s)) -o $(OUT) render.c objects.c

assets: assets/gfx.bin assets/levels.bin assets/palette.json

assets/gfx.bin: ../out/portable/gfx.bin
	cp $< $@
assets/levels.bin: ../out/portable/levels.bin
	cp $< $@
assets/palette.json: ../out/portable/palette.json
	cp $< $@

# Emscripten path (produces app.js + app.wasm)
emcc: assets
	$(EMCC) render.c objects.c -O2 -o app.js -s WASM=1 \
	  -s EXPORTED_FUNCTIONS='[$(foreach s,$(EXPORTS),_$(s)),_malloc,_free]' \
	  -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","HEAPU8"]' \
	  -s ALLOW_MEMORY_GROWTH=1

test: clang
	for l in 0 1 9 13 18; do node test_render.js $$l; done

test-objects: clang
	node test_objects.js

clean:
	rm -f $(OUT) app.js app.wasm

.PHONY: clang emcc assets test test-objects clean
