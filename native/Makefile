# Native desktop build (Windows/macOS/Linux) - thin wrapper around CMake.
# The wasm build and its test suite stay in ../wasm; this builds the native
# game (caves) and the golden-master digest harness (cc_digest).

BUILD  ?= build
CMAKE  ?= cmake
GROUPS  = static_render gameplay transitions warp shell save_round_trip
DIGEST_DIR = $(BUILD)/digest
FIXTURE = ../notes/refactor_digests.json

.PHONY: all build test-digest test-smoke test-smoke-ep2 test-smoke-ep3 test-menu test-persist-isolation clean

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

# Headless picker check: the launcher renders, the scripted Down+Enter lands
# on the second listed entry.  The expected line is derived from which
# sibling binaries exist next to caves (none -> 1 entry, ep2 -> 2, all -> 3).
test-menu: $(BUILD)/caves
	@if [ -x ./$(BUILD)/caves_ep2 ] && [ -x ./$(BUILD)/caves_ep3 ]; then \
	  want="selected 2 of 3"; \
	elif [ -x ./$(BUILD)/caves_ep2 ]; then \
	  want="selected 2 of 2"; \
	elif [ -x ./$(BUILD)/caves_ep3 ]; then \
	  want="selected 3 of 2"; \
	else \
	  want="selected 1 of 1"; \
	fi; \
	SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
	  ./$(BUILD)/caves --menu-smoke | grep -q "MENU OK: $$want" && \
	  echo "PASS: launcher picker"

# Save-file isolation: all three episode variants write config/hiscores/slot
# into ONE shared directory; each may only touch its own cc<N>.* files.
test-persist-isolation: $(BUILD)/persist_test_ep1 $(BUILD)/persist_test_ep2 $(BUILD)/persist_test_ep3
	@dir=$$(mktemp -d); \
	ok=1; \
	for ep in 1 2 3; do \
	  ./$(BUILD)/persist_test_ep$$ep $$dir || ok=0; \
	done; \
	ls $$dir/cc1.* $$dir/cc2.* $$dir/cc3.* >/dev/null || ok=0; \
	rm -rf $$dir; \
	[ $$ok -eq 1 ] && echo "PASS: per-episode save files coexist in one pref dir"

$(BUILD)/persist_test_ep1 $(BUILD)/persist_test_ep2 $(BUILD)/persist_test_ep3: $(BUILD)/Makefile
	$(CMAKE) --build $(BUILD) --target persist_test_ep1 persist_test_ep2 persist_test_ep3

clean:
	rm -rf $(BUILD)
