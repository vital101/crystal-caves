#!/bin/sh
# Dev-container entrypoint: turn the one environment mistake that is invisible
# on Windows (CRLF makefiles) into a readable message instead of GNU make
# misreading every rule in wasm/Makefile.
# git considers these files clean (the CRLF converts away on add), so a plain
# `git checkout` will not rewrite them: the file has to be deleted first.
if [ -f /src/wasm/Makefile ] && LC_ALL=C grep -q "$(printf '\r')" /src/wasm/Makefile; then
  echo "warning: /src/wasm/Makefile has CRLF line endings; GNU make in this" >&2
  echo "         container will misread it.  Repair it on the host with:" >&2
  echo "             rm wasm/Makefile && git checkout -- wasm/Makefile" >&2
  echo "         (.gitattributes pins makefiles and shell scripts to LF.)" >&2
fi
exec "$@"
