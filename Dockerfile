# syntax=docker/dockerfile:1
#
# Crystal Caves Episode 1 - browser build + static host.
#
#   builder  freestanding wasm32 module (Debian clang/lld, no Emscripten)
#   runtime  nginx serving game.html, its JS adapters and the asset bundle
#
# The committed out/portable/ assets are the only game data needed, so no
# original archive is fetched and the build is fully offline after apt.

FROM debian:trixie-slim AS build

RUN apt-get update \
 && apt-get install -y --no-install-recommends clang lld make \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /src

COPY wasm/ wasm/
COPY out/portable/ out/portable/

# LLVM must be passed explicitly: the Makefile default is a Homebrew path.
RUN make -C wasm clang LLVM=/usr/bin \
 && test -s wasm/render.wasm

FROM nginx:alpine

COPY deploy/nginx.conf /etc/nginx/conf.d/default.conf

COPY wasm/game.html \
     wasm/keymap.js \
     wasm/legend.js \
     wasm/persist.js \
     wasm/sound_synth.js \
     wasm/audio.js \
     wasm/favicon.ico \
     wasm/favicon.svg \
     wasm/favicon-16.png \
     wasm/favicon-32.png \
     wasm/apple-touch-icon.png \
     /srv/
COPY --from=build /src/wasm/render.wasm /srv/render.wasm
COPY --from=build /src/wasm/assets/ /srv/assets/

EXPOSE 80

HEALTHCHECK --interval=30s --timeout=3s --start-period=5s --retries=3 \
  CMD wget -q -O /dev/null http://127.0.0.1/ || exit 1
