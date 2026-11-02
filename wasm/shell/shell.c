/* Crystal Caves - Phase 5 shell state machine.
 *
 * The original shell uses overlapping Turbo Pascal segments and DOS file I/O;
 * this module reimplements the observable flow with the same text blocks,
 * key letters, screens and save/restore restrictions.  The JS page owns
 * localStorage, canvas presentation and the browser key map.
 *
 * Phase 2 split (mechanical): the public entry points and the internal
 * helpers live in the shell/ modules; the shared state and prototypes are
 * in shell_internal.h.  Module ownership:
 *
 *   state.c     shell state machine, pages/story setup, cursor animation,
 *               cc_shell_init/step/state, page accessors, message + notify_*
 *   input.c     cc_shell_key/cc_shell_key_scan, text paging key, scan-code
 *               compatibility, story/video/quit/instruction entry, warp
 *   render.c    cc_shell_render and the centered-window/prompt draw helpers
 *   hiscore.c   high-score table, name entry buffer, sort/tie insertion
 *   config.c    sound/joystick/video config, dirty flag, action-key tables
 *   persist.c   save/restore request handshake with the JS layer
 *   scroll.c    title/credits attract loop (phase/frame/row/direction)
 *
 * shell.h remains the only public API; the enum values and export names are
 * stable for game.html and the tests.  State-machine redesign is Phase 3.
 */
#include "shell_internal.h"
