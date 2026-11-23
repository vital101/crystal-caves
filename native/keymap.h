#ifndef CC_KEYMAP_H
#define CC_KEYMAP_H

#include <stdint.h>

/* Native port of wasm/keymap.js: physical key -> IBM PC set-1 scan code.
 * Uses SDL scancodes (physical, layout-independent) exactly like the browser
 * build uses KeyboardEvent.code. */

void cc_keymap_init(void);
int  cc_scan_from_sdl(int sdl_scancode);        /* set-1 make code or 0 */
int  cc_keychar_from_sdl(uint32_t sdl_key, uint16_t mod); /* shell key value or 0 */

#endif
