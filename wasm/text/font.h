/* Crystal Caves text - font lookup (Phase 2 split).
 *
 * Fonts are the original ProGraphx MNI banks: 50 glyphs of 40 bytes, each
 * glyph 8 rows x 5 bytes [mask][plane0..plane3], MSB = leftmost pixel.
 * The font pointers passed by the shell point at glyph 0 (the 3-byte MNI
 * header has already been stripped by the asset exporter).
 */
#ifndef TEXT_FONT_H
#define TEXT_FONT_H

#include "text.h"

#include <stdint.h>

#define SCREEN_W 320
#define SCREEN_H 200
#define CELL_W 8
#define CELL_H 8

/* Selected glyph banks (cc_text_set_fonts).  Owned by font.c. */
extern const unsigned char *g_f1;
extern const unsigned char *g_f2;
extern const unsigned char *g_spl;

/* bank glyph at 1-based-free index idx (0..49), or 0 when out of range. */
const unsigned char *glyph(const unsigned char *bank, int idx);

/* The F1/F2 glyph for a printable character (0 when it draws nothing). */
const unsigned char *font_for_char(unsigned char ch);

#endif /* TEXT_FONT_H */
