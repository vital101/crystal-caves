# syntax=docker/dockerfile:1
#
# Crystal Caves - browser build + static host (all three episodes).
#
#   builder  freestanding wasm32 modules (Debian clang/lld, no Emscripten):
#            render.wasm (ep1), render_ep2.wasm, render_ep3.wasm
#   runtime  nginx serving select.html (the launch picker), game.html and
#            the three asset bundles
#
# The committed out/portable*/ assets are the only game data needed, so no
# original archive is fetched and the build is fully offline after apt.

FROM debian:trixie-slim AS build

RUN apt-get update \
 && apt-get install -y --no-install-recommends clang lld make \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /src

COPY wasm/ wasm/
COPY out/portable/ out/portable/
COPY out/portable_ep2/ out/portable_ep2/
COPY out/portable_ep3/ out/portable_ep3/

# LLVM must be passed explicitly: the Makefile default is a Homebrew path.
# The committed generated data and portable assets are current, so -o stops
# make from chasing their regeneration prerequisites (notes/, extracted/ —
# original-archive material that is deliberately not in this context).
RUN make -C wasm clang LLVM=/usr/bin \
 && make -C wasm ep2 ep2-assets LLVM=/usr/bin \
      -o ep2_data/startup_data.h -o ep2_data/objects_data.h \
      -o ep2_data/cells_data.h -o ep2_data/shell_text_data.h \
      -o ep2_data/shell_exact_data.h -o ep2_data/episode_data.h \
      -o ../out/portable_ep2/screens.bin -o ../out/portable_ep2/level_overlay.bin \
 && make -C wasm ep3 ep3-assets LLVM=/usr/bin \
      -o ep3_data/startup_data.h -o ep3_data/objects_data.h \
      -o ep3_data/cells_data.h -o ep3_data/shell_text_data.h \
      -o ep3_data/shell_exact_data.h -o ep3_data/episode_data.h \
      -o ../out/portable_ep3/screens.bin -o ../out/portable_ep3/level_overlay.bin \
 && test -s wasm/render.wasm && test -s wasm/render_ep2.wasm \
 && test -s wasm/render_ep3.wasm

FROM nginx:alpine

COPY deploy/nginx.conf /etc/nginx/conf.d/default.conf

COPY wasm/select.html \
     wasm/game.html \
     wasm/game.js \
     wasm/keymap.js \
     wasm/legend.js \
     wasm/persist.js \
     wasm/sound_synth.js \
     wasm/audio.js \
     wasm/touch.js \
     wasm/manifest.webmanifest \
     wasm/favicon.ico \
     wasm/favicon.svg \
     wasm/favicon-16.png \
     wasm/favicon-32.png \
     wasm/favicon-192.png \
     wasm/favicon-512.png \
     wasm/apple-touch-icon.png \
     wasm/social-card.png \
     /srv/
COPY wasm/game/*.js /srv/game/
COPY wasm/shots/ /srv/shots/
COPY --from=build /src/wasm/render.wasm /srv/render.wasm
COPY --from=build /src/wasm/render_ep2.wasm /srv/render_ep2.wasm
COPY --from=build /src/wasm/render_ep3.wasm /srv/render_ep3.wasm
COPY --from=build /src/wasm/assets/ /srv/assets/
COPY --from=build /src/wasm/assets_ep2/ /srv/assets_ep2/
COPY --from=build /src/wasm/assets_ep3/ /srv/assets_ep3/

EXPOSE 80

HEALTHCHECK --interval=30s --timeout=3s --start-period=5s --retries=3 \
  CMD wget -q -O /dev/null http://127.0.0.1/ || exit 1
