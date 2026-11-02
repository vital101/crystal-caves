/* Crystal Caves gameplay module - split from wasm/objects.c. */
#include "game_internal.h"



/* freestanding: no libc */
void *memcpy_(void *d, const void *s, unsigned n) {
    unsigned char *p = (unsigned char *)d;
    const unsigned char *q = (const unsigned char *)s;
    while (n--) *p++ = *q++;
    return d;
}



/* ---------------------------------------------------------------- layout
 *
 * The save payload is a raw struct image (see cc_state_save below), and the
 * object image is addressed with fixed original-binary byte offsets.  Any
 * accidental field move/reorder therefore corrupts saves or gameplay without
 * a compiler error.  These asserts turn that into a build failure.
 *
 * Rules: rename fields freely, but never reorder or resize them.  A genuine
 * layout change must bump the save format version and add a migration.
 */

/* Original object record: 60 slots, stride 0x2D, packed. */
_Static_assert(sizeof(ObjectEntry) == 0x2D, "ObjectEntry record layout changed");
_Static_assert(CC_OFFSETOF(ObjectEntry, state)       == 0x00, "object field moved");
_Static_assert(CC_OFFSETOF(ObjectEntry, x)           == 0x06, "object field moved");
_Static_assert(CC_OFFSETOF(ObjectEntry, block)       == 0x0E, "object field moved");
_Static_assert(CC_OFFSETOF(ObjectEntry, anim_frame)  == 0x10, "object field moved");
_Static_assert(CC_OFFSETOF(ObjectEntry, shape)       == 0x1C, "object field moved");
_Static_assert(CC_OFFSETOF(ObjectEntry, vx)          == 0x1E, "object field moved");
_Static_assert(CC_OFFSETOF(ObjectEntry, kind)        == 0x26, "object field moved");
_Static_assert(CC_OFFSETOF(ObjectEntry, ready)       == 0x28, "object field moved");
_Static_assert(CC_OFFSETOF(ObjectEntry, handler)     == 0x29, "object field moved");
_Static_assert(CC_OFFSETOF(ObjectEntry, handler_seg) == 0x2B, "object field moved");

/* Save payload sections.  Offsets are byte offsets inside cc_state_t. */
_Static_assert(sizeof(cc_state_t) == 23010,
               "cc_state_t changed; bump the save format version");
_Static_assert(sizeof(cc_state_t) <= 0xFFFF, "save payload too large");
_Static_assert(CC_OFFSETOF(cc_state_t, obj)              == 0,     "state section moved");
_Static_assert(CC_OFFSETOF(cc_state_t, obj_cursor)       == 2700,  "state section moved");
_Static_assert(CC_OFFSETOF(cc_state_t, cells)            == 2702,  "state section moved");
_Static_assert(CC_OFFSETOF(cc_state_t, player_x)         == 22704, "state section moved");
_Static_assert(CC_OFFSETOF(cc_state_t, cam_x)            == 22708, "state section moved");
_Static_assert(CC_OFFSETOF(cc_state_t, input_left)       == 22718, "state section moved");
_Static_assert(CC_OFFSETOF(cc_state_t, cur_level)        == 22762, "state section moved");
_Static_assert(CC_OFFSETOF(cc_state_t, g_a0cc)           == 22776, "state section moved");
_Static_assert(CC_OFFSETOF(cc_state_t, cave_flags)       == 22832, "state section moved");
_Static_assert(CC_OFFSETOF(cc_state_t, level_block)      == 22849, "state section moved");
_Static_assert(CC_OFFSETOF(cc_state_t, checkpoint_block) == 22882, "state section moved");
_Static_assert(CC_OFFSETOF(cc_state_t, g_5124)           == 22928, "state section moved");
_Static_assert(CC_OFFSETOF(cc_state_t, score_lo)         == 22938, "state section moved");
_Static_assert(CC_OFFSETOF(cc_state_t, rng_lo)           == 22942, "state section moved");
_Static_assert(CC_OFFSETOF(cc_state_t, checkpoint_50a0)  == 23000, "state section moved");
_Static_assert(sizeof(((cc_state_t *)0)->cells) == 20000, "cell grid changed");

cc_state_t g_state = { 0 };
const unsigned char *g_cells_asset = 0;

/* 16-bit word arithmetic helpers: the 8086 wraps on every operation. */
int16_t wadd(int16_t a, int16_t b) { return (int16_t)((uint16_t)a + (uint16_t)b); }
int16_t wsub(int16_t a, int16_t b) { return (int16_t)((uint16_t)a - (uint16_t)b); }
int16_t wmul(int16_t a, int16_t b) { return (int16_t)((uint16_t)a * (uint16_t)b); }
int16_t wneg(int16_t a) { return (int16_t)(0u - (uint16_t)a); }

/* [0x50a5]/[0x50a7] add/adc pair.  The two words are not contiguous in
 * memory (0x50a6 is unused), so carry is applied explicitly. */
void score_add(uint16_t lo_add, uint16_t hi_add) {
    uint32_t lo = (uint32_t)g_state.score_lo + lo_add;
    uint32_t hi = (uint32_t)g_state.score_hi + hi_add + (lo >> 16);
    g_state.score_lo = (uint16_t)lo;
    g_state.score_hi = (uint16_t)hi;
}

void cc_set_cells(const unsigned char *cells) { g_cells_asset = cells; }

/* Mutable cell record access.  Logical (col,row) maps to the original
 * 0x508C + (col+1)*500 + (row+1)*20 index. */
unsigned char *cell_rec(int col, int row) {
    if (col < 0 || col >= CELL_COLS || row < 0 || row >= CELL_ROWS) return 0;
    return g_state.cells[row][col];
}
int cell_byte_cur(int col, int row, int off) {
    unsigned char *c = cell_rec(col, row);
    return c ? c[off] : 0;
}
void cell_put(int col, int row, int off, int value) {
    unsigned char *c = cell_rec(col, row);
    if (c) c[off] = (unsigned char)value;
}
int cell_word_cur(int col, int row, int off) {
    return cell_byte_cur(col, row, off) |
           (cell_byte_cur(col, row, off + 1) << 8);
}
void cell_word_put(int col, int row, int off, int value) {
    cell_put(col, row, off, value & 0xFF);
    cell_put(col, row, off + 1, (value >> 8) & 0xFF);
}
/* Raw original cell indices already include the +1 border offset. */
int cell_word_raw(int col1, int row1, int off) {
    return cell_word_cur(col1 - 1, row1 - 1, off);
}
void cell_word_raw_put(int col1, int row1, int off, int value) {
    cell_word_put(col1 - 1, row1 - 1, off, value);
}
int cave_flag_get(int idx) {
    if (idx < 0 || idx > 16) return 0;
    return g_state.cave_flags[idx];
}
void cave_flag_set(int idx, int value) {
    if (idx >= 0 && idx <= 16) {
        g_state.cave_flags[idx] = (uint8_t)value;
        g_state.level_block[0x10 + idx] = (uint8_t)value;
    }
}

/* Current-level solid flag: mutable oracle cells when available, otherwise
 * the baked solid bitmap from cells_data.h. */
int cell_solid_cur(int col, int row) {
    if (col < 0 || col >= CELL_COLS || row < 0 || row >= CELL_ROWS) return 1;
    if (g_cells_asset) return g_state.cells[row][col][17] != 0;
    return cc_cell_solid(g_state.cur_level, col, row);
}
