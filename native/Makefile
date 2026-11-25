# Native desktop build (Windows/macOS/Linux) - thin wrapper around CMake.
# The wasm build and its test suite stay in ../wasm; this builds the native
# game (caves) and the golden-master digest harness (cc_digest).

BUILD  ?= build
CMAKE  ?= cmake
GROUPS  = static_render gameplay transitions warp shell save_round_trip
DIGEST_DIR = $(BUILD)/digest
FIXTURE = ../notes/refactor_digests.json

.PHONY: all build test-digest test-smoke test-smoke-ep2 test-smoke-ep3 clean

all: build

build: $(BUILD)/caves $(BUILD)/cc_digest

$(BUILD)/Makefile: CMakeLists.txt ../tools/embed_assets.py
	$(CMAKE) -S . -B $(BUILD) -DCMAKE_BUILD_TYPE=Release

$(BUILD)/caves $(BUILD)/cc_digest: $(BUILD)/Makefile
	$(CMAKE) --build $(BUILD)

$(BUILD)/caves_ep2: $(BUILD)/Makefile
	$(CMAKE) --build $(BUILD) --target caves_ep2

$(BUILD)/caves_ep3: $(BUILD)/Makefile
	$(CMAKE) --build $(BUILD) --target caves_ep3

# Run every digest group in a fresh process (fresh static state, like the JS
# harness's fresh WebAssembly.Instance) and compare against the wasm fixture.
test-digest: $(BUILD)/cc_digest
	@mkdir -p $(DIGEST_DIR)
	@for g in $(GROUPS); do \
	  ./$(BUILD)/cc_digest $$g $(DIGEST_DIR)/$$g.json || exit 1; \
	done
	node compare_digests.js $(FIXTURE) $(foreach g,$(GROUPS),$(DIGEST_DIR)/$(g).json)

# Headless smoke test of the full SDL host path (dummy video/audio drivers).
test-smoke: $(BUILD)/caves
	SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./$(BUILD)/caves --smoke 120

test-smoke-ep2: $(BUILD)/caves_ep2
	SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./$(BUILD)/caves_ep2 --smoke 120

test-smoke-ep3: $(BUILD)/caves_ep3
	SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./$(BUILD)/caves_ep3 --smoke 120

clean:
	rm -rf $(BUILD)
