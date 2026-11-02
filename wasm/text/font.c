/* Crystal Caves text - glyph lookup and character table.
 */
#include "font.h"

const unsigned char *g_f1 = 0;
const unsigned char *g_f2 = 0;
const unsigned char *g_spl = 0;

void cc_text_set_fonts(const unsigned char *f1, const unsigned char *f2,
                       const unsigned char *spl) {
    g_f1 = f1;
    g_f2 = f2;
    g_spl = spl;
}

const unsigned char *glyph(const unsigned char *bank, int idx) {
    if (!bank || idx < 0 || idx >= 50) return 0;
    return bank + idx * 40;
}

/* The original text printer looks each character up in a 117-byte table
 * (CS:0x64C): 32 leading '|' entries, ASCII 0x20..0x5a, then ASCII
 * 0x61..0x7a.  The resulting 1-based code selects F1 for codes <= 0x48 and
 * F2 for codes >= 0x49, using the same "code - base" glyph indices as the
 * real routine.  Codes for characters outside the table (notably
 * 0x5b..0x60) draw nothing. */
static int code_for_char(unsigned char ch) {
    if (ch >= 0x20 && ch <= 0x5a) return (int)ch + 1;
    if (ch >= 0x61 && ch <= 0x7a) return (int)ch - 5;
    return 0;
}

static const unsigned char *glyph_for_code(int code) {
    if (code <= 0x48) return glyph(g_f1, code - 0x17);
    return glyph(g_f2, code - 0x49);
}

const unsigned char *font_for_char(unsigned char ch) {
    int code = code_for_char(ch);
    if (!code) return 0;
    return glyph_for_code(code);
}

/* '^' is the input cursor marker, not a printable glyph.  The original
 * printer has no table entry for 0x5E, so it draws nothing; the renderer
 * locates the marker and the animation loop paints F2 glyphs 45..49 at
 * that cell every 21 PIT ticks.  See cc_text_cursor_draw(). */
