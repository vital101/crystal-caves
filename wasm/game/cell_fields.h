#ifndef CELL_FIELDS_H
#define CELL_FIELDS_H

/* Mutable 20-byte cell record (10 words) addressed at
 * 0x508C + (col+1)*500 + (row+1)*20 (see AGENTS.md / notes/handlers.md).
 * Words are little-endian; the offset enum names are shared by the
 * gameplay helpers in game_internal.h and the renderer's cell_word(), so
 * the two cannot drift apart.
 *
 * Layer pairs as the original cell-event handler (0x0FC5:0xBB26) reads
 * them: layer1 (CELL_W4, CELL_W2), layer2 (CELL_W5, CELL_W3),
 * layer3 (CELL_W6, CELL_W7). */
enum cell_word_offset {
    CELL_W0 = 0x00, CELL_W1 = 0x02, CELL_W2 = 0x04,
    CELL_W3 = 0x06, CELL_W4 = 0x08, CELL_W5 = 0x0A,
    CELL_W6 = 0x0C, CELL_W7 = 0x0E, CELL_W8 = 0x10, CELL_W9 = 0x12
};

/* Flag bytes inside W8 (solid) and W9 (exit).  The high bytes are the
 * ones the collision code tests: 0x11 solid, 0x13 exit. */
#define CELL_FLAG_SOLID     0x11  /* byte 0x11: non-zero blocks the player */
#define CELL_FLAG_SOLID_LO  0x10  /* byte 0x10: cleared with the solid flag */
#define CELL_FLAG_EXIT      0x13  /* byte 0x13: non-zero marks an exit cell */
#define CELL_FLAG_EXIT_LO   0x12  /* byte 0x12: tested as an extra blocker */

#endif
