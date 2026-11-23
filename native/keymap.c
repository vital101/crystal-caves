/* Native port of wasm/keymap.js (KeyboardEvent.code -> DOS set-1 scan code),
 * driven by SDL scancodes instead.  SDL letter scancodes are contiguous in
 * alphabet order, so the same LETTER_SCAN table as keymap.js applies. */
#include <string.h>
#include <SDL3/SDL.h>
#include "keymap.h"

static unsigned char s_sdl_to_set1[SDL_SCANCODE_COUNT];

static const unsigned char LETTER_SCAN[26] = {
    0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23,
    0x17, 0x24, 0x25, 0x26, 0x32, 0x31, 0x18, 0x19,
    0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D,
    0x15, 0x2C
};

void cc_keymap_init(void) {
    memset(s_sdl_to_set1, 0, sizeof(s_sdl_to_set1));
    s_sdl_to_set1[SDL_SCANCODE_ESCAPE]    = 0x01;
    for (int i = 0; i < 9; i++)
        s_sdl_to_set1[SDL_SCANCODE_1 + i] = 0x02 + i;
    s_sdl_to_set1[SDL_SCANCODE_0]         = 0x0B;
    s_sdl_to_set1[SDL_SCANCODE_MINUS]     = 0x0C;
    s_sdl_to_set1[SDL_SCANCODE_EQUALS]    = 0x0D;
    s_sdl_to_set1[SDL_SCANCODE_BACKSPACE] = 0x0E;
    s_sdl_to_set1[SDL_SCANCODE_TAB]       = 0x0F;
    for (int i = 0; i < 26; i++)
        s_sdl_to_set1[SDL_SCANCODE_A + i] = LETTER_SCAN[i];
    s_sdl_to_set1[SDL_SCANCODE_LEFTBRACKET]  = 0x1A;
    s_sdl_to_set1[SDL_SCANCODE_RIGHTBRACKET] = 0x1B;
    s_sdl_to_set1[SDL_SCANCODE_RETURN]       = 0x1C;
    s_sdl_to_set1[SDL_SCANCODE_LCTRL]        = 0x1D;
    s_sdl_to_set1[SDL_SCANCODE_RCTRL]        = 0x1D;
    s_sdl_to_set1[SDL_SCANCODE_SEMICOLON]    = 0x27;
    s_sdl_to_set1[SDL_SCANCODE_APOSTROPHE]   = 0x28;
    s_sdl_to_set1[SDL_SCANCODE_GRAVE]        = 0x29;
    s_sdl_to_set1[SDL_SCANCODE_LSHIFT]       = 0x2A;
    s_sdl_to_set1[SDL_SCANCODE_BACKSLASH]    = 0x2B;
    s_sdl_to_set1[SDL_SCANCODE_COMMA]        = 0x33;
    s_sdl_to_set1[SDL_SCANCODE_PERIOD]       = 0x34;
    s_sdl_to_set1[SDL_SCANCODE_SLASH]        = 0x35;
    s_sdl_to_set1[SDL_SCANCODE_RSHIFT]       = 0x36;
    s_sdl_to_set1[SDL_SCANCODE_LALT]         = 0x38;
    s_sdl_to_set1[SDL_SCANCODE_RALT]         = 0x38;
    s_sdl_to_set1[SDL_SCANCODE_SPACE]        = 0x39;
    for (int i = 0; i < 10; i++)
        s_sdl_to_set1[SDL_SCANCODE_F1 + i] = 0x3B + i;
    s_sdl_to_set1[SDL_SCANCODE_HOME]     = 0x47;
    s_sdl_to_set1[SDL_SCANCODE_UP]       = 0x48;
    s_sdl_to_set1[SDL_SCANCODE_PAGEUP]   = 0x49;
    s_sdl_to_set1[SDL_SCANCODE_LEFT]     = 0x4B;
    s_sdl_to_set1[SDL_SCANCODE_RIGHT]    = 0x4D;
    s_sdl_to_set1[SDL_SCANCODE_END]      = 0x4F;
    s_sdl_to_set1[SDL_SCANCODE_DOWN]     = 0x50;
    s_sdl_to_set1[SDL_SCANCODE_PAGEDOWN] = 0x51;
    s_sdl_to_set1[SDL_SCANCODE_INSERT]   = 0x52;
    s_sdl_to_set1[SDL_SCANCODE_DELETE]   = 0x53;
}

int cc_scan_from_sdl(int sdl_scancode) {
    if (sdl_scancode <= 0 || sdl_scancode >= SDL_SCANCODE_COUNT) return 0;
    return s_sdl_to_set1[sdl_scancode];
}

/* Shell key value for SDL_Keycode, mirroring ev.key.charCodeAt(0) in the
 * browser: printable ASCII (with Shift uppercasing letters, like ev.key),
 * plus the control keys the shell understands. */
int cc_keychar_from_sdl(uint32_t sdl_key, uint16_t mod) {
    if (sdl_key == SDLK_RETURN)    return 13;
    if (sdl_key == SDLK_ESCAPE)    return 27;
    if (sdl_key == SDLK_BACKSPACE) return 8;
    if (sdl_key == SDLK_SPACE)     return 32;
    if (sdl_key >= 32 && sdl_key <= 126) {
        if ((mod & SDL_KMOD_SHIFT) && sdl_key >= 'a' && sdl_key <= 'z')
            return (int)sdl_key - 32;
        return (int)sdl_key;
    }
    return 0;
}
