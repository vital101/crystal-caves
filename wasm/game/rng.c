/* Crystal Caves gameplay module - split from wasm/objects.c. */
#include "game_internal.h"

/* ---------------------------------------------------------------- RNG
 * 0x2BB5:0x980 / generator at 0x2C51F.  The high word is computed by the
 * original's byte-wise multiply sequence, not a plain 32-bit multiply; the
 * two differ from the second draw onward.  Reproduce the register operations
 * exactly.  Returns (high word) % n; n == 0 returns 0. */
uint16_t rng_next_hi(void) {
    uint16_t ax = g_state.rng_lo;
    uint16_t bx = g_state.rng_hi;
    uint16_t cx = (uint16_t)(ax << 3);
    uint32_t prod = (uint32_t)ax * 0x8405u;
    uint16_t dx = (uint16_t)(prod >> 16);
    uint8_t dh, ch;
    ax = (uint16_t)prod;

    ch = (uint8_t)((cx >> 8) + (cx & 0xFF));
    cx = (uint16_t)((ch << 8) | (cx & 0xFF));
    dx = (uint16_t)(dx + cx);
    dx = (uint16_t)(dx + bx);

    bx = (uint16_t)(bx << 2);
    dx = (uint16_t)(dx + bx);
    dh = (uint8_t)((dx >> 8) + (bx & 0xFF));
    dx = (uint16_t)((dh << 8) | (dx & 0xFF));

    bx = (uint16_t)(bx << 5);
    dh = (uint8_t)((dx >> 8) + (bx & 0xFF));
    dx = (uint16_t)((dh << 8) | (dx & 0xFF));

    ax = (uint16_t)(ax + 1);
    if (ax == 0) dx = (uint16_t)(dx + 1);

    g_state.rng_lo = ax;
    g_state.rng_hi = dx;
    return dx;
}
uint16_t cc_rng_next(uint16_t n) {
    uint16_t hi = rng_next_hi();
    return n ? (uint16_t)(hi % n) : 0;
}
void cc_rng_seed(uint16_t lo, uint16_t hi) {
    g_state.rng_lo = lo;
    g_state.rng_hi = hi;
}
uint16_t cc_rng_lo(void) { return g_state.rng_lo; }
uint16_t cc_rng_hi(void) { return g_state.rng_hi; }
