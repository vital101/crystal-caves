/* Crystal Caves - HUD and text rendering (Phase 5).
 *
 * Fonts are the original ProGraphx MNI banks: 50 glyphs of 40 bytes, each
 * glyph 8 rows x 5 bytes [mask][plane0..plane3], MSB = leftmost pixel.
 * The font pointers passed by the shell point at glyph 0 (the 3-byte MNI
 * header has already been stripped by the asset exporter).
 */
#include "text.h"
#include "render.h"
#include "objects.h"
#include "shell_text_data.h"

#include <stdint.h>

static const unsigned char *g_f1 = 0;
static const unsigned char *g_f2 = 0;
static const unsigned char *g_spl = 0;

#define SCREEN_W 320
#define SCREEN_H 200
#define CELL_W 8
#define CELL_H 8

void cc_text_set_fonts(const unsigned char *f1, const unsigned char *f2,
                       const unsigned char *spl) {
    g_f1 = f1;
    g_f2 = f2;
    g_spl = spl;
}

static const unsigned char *glyph(const unsigned char *bank, int idx) {
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

static const unsigned char *font_for_char(unsigned char ch) {
    int code = code_for_char(ch);
    if (!code) return 0;
    return glyph_for_code(code);
}

/* '^' is a cursor marker, not a printable glyph.  Custom shell prompts still
 * use cc_text_draw_string() and expect to see the original caret shape, so
 * the pixel-oriented helper maps it to the F2 caret glyph.  Window text uses
 * the real renderer rule and skips it. */
static const unsigned char *caret_glyph(void) { return glyph(g_f2, 22); }

static int g_scroll(void) {
    return (int)(int16_t)(cc_global_get(0x2b5c) & 0xFFFF);
}

/* Exact window frame from 0x0FC5:0x0877.  All coordinates are cells. */
static void draw_frame_cells(int wx, int wy, int maxlen, int h, int scroll) {
    int rx = wx + maxlen + 4;
    int by = wy + h + 1;
    int rows = by - wy;
    for (int i = 1; i <= rows; i++)
        for (int x = wx + 1; x < rx; x++)
            cc_view_draw_glyph(glyph(g_f1, 8), x * 8, (wy + i) * 8 + scroll);
    cc_view_draw_glyph(glyph(g_f1, 0), wx * 8, wy * 8 + scroll);
    for (int x = wx + 1; x < rx; x++)
        cc_view_draw_glyph(glyph(g_f1, 1), x * 8, wy * 8 + scroll);
    cc_view_draw_glyph(glyph(g_f1, 2), rx * 8, wy * 8 + scroll);
    for (int i = 1; i <= rows; i++) {
        cc_view_draw_glyph(glyph(g_f1, 5), wx * 8, (wy + i) * 8 + scroll);
        cc_view_draw_glyph(glyph(g_f1, 7), rx * 8, (wy + i) * 8 + scroll);
        cc_view_draw_glyph(glyph(g_f1, 9), (rx + 1) * 8, (wy + i) * 8 + scroll);
    }
    cc_view_draw_glyph(glyph(g_f1, 9), (rx + 1) * 8, (by + 1) * 8 + scroll);
    cc_view_draw_glyph(glyph(g_f1, 4), wx * 8, by * 8 + scroll);
    for (int x = wx + 1; x < rx; x++)
        cc_view_draw_glyph(glyph(g_f1, 6), x * 8, by * 8 + scroll);
    cc_view_draw_glyph(glyph(g_f1, 3), rx * 8, by * 8 + scroll);
    for (int x = wx + 1; x <= rx; x++)
        cc_view_draw_glyph(glyph(g_f1, 9), x * 8, (by + 1) * 8 + scroll);
}

void cc_text_draw_frame(int px, int py, int pw, int ph) {
    if (pw < 8 || ph < 8) return;
    int wx = px / 8, wy = py / 8;
    int maxlen = pw / 8 - 6;
    int h = ph / 8 - 3;
    if (maxlen < 0) maxlen = 0;
    if (h < 0) h = 0;
    draw_frame_cells(wx, wy, maxlen, h, 0);
}

static void draw_window_string(int xcell, int ycell, const char *s, int scroll) {
    if (!s) return;
    for (int i = 0; s[i]; i++) {
        unsigned char ch = (unsigned char)s[i];
        if (ch == '^') continue;
        const unsigned char *g = font_for_char(ch);
        if (g) cc_view_draw_glyph(g, (xcell + i) * 8, ycell * 8 + scroll);
    }
}

void cc_text_draw_string(int x, int y, const char *s) {
    if (!s) return;
    while (*s) {
        unsigned char ch = (unsigned char)*s++;
        const unsigned char *g = (ch == '^') ? caret_glyph() : font_for_char(ch);
        if (g) cc_view_draw_glyph(g, x, y);
        x += 8;
    }
}

int cc_text_block_lines(int block_id) {
    if (block_id < 0 || block_id >= SHELL_TEXT_COUNT) return 0;
    return g_shell_text_blocks[block_id].count;
}

static int text_len(const char *s) {
    int n = 0;
    if (s) while (s[n]) n++;
    return n;
}

/* Exact window layout from the original renderer.  `count` text lines are
 * followed by the implicit "END OF WINDOW" sentinel; the sentinel and its
 * one-row gap make the window height H = count + 2. */
static void draw_window_lines(const char *const *lines, int count,
                              int cam_x, int cam_y, int scroll) {
    if (count <= 0) return;
    int maxlen = 0;
    for (int i = 0; i < count; i++) {
        int n = text_len(lines[i]);
        if (i == 0) maxlen = n - 1;
        if (n - 1 > maxlen) maxlen = n - 1;
    }
    int h = count + 2;  /* text lines + one-slot gap before the sentinel */
    int wx = ((320 - maxlen * 8) / 2 + cam_x) / 8 - 3;
    int wy = ((200 - h * 8) / 2 + cam_y) / 8 - 2;

    draw_frame_cells(wx, wy, maxlen, h, scroll);
    for (int i = 0; i < count; i++)
        draw_window_string(wx + 2, wy + i + 2, lines[i], scroll);
}

/* Draw a text block using the original centered window layout.  Long blocks
 * are paged by the caller; each page is laid out with the same renderer. */
void cc_text_draw_block(int block_id, int start_line, int max_lines) {
    if (block_id < 0 || block_id >= SHELL_TEXT_COUNT) return;
    const ShellTextBlock *b = &g_shell_text_blocks[block_id];
    if (start_line < 0) start_line = 0;
    if (start_line >= b->count) return;
    int end = b->count;
    if (max_lines > 0 && start_line + max_lines < end)
        end = start_line + max_lines;
    int count = end - start_line;
    if (count <= 0) return;

    int cam_x = (int)(int16_t)(cc_global_get(0x512e) & 0xFFFF);
    int cam_y = (int)(int16_t)(cc_global_get(0x5130) & 0xFFFF);
    draw_window_lines(b->lines + start_line, count, cam_x, cam_y, g_scroll());
}

/* ---- HUD ---------------------------------------------------------------- */

static int u32_digits(unsigned int v, char *buf) {
    char tmp[12];
    int n = 0;
    if (v == 0) tmp[n++] = '0';
    while (v) { tmp[n++] = (char)('0' + (v % 10)); v /= 10; }
    for (int i = 0; i < n; i++) buf[i] = tmp[n - 1 - i];
    return n;
}

static void format_u32(unsigned int v, int width, char *buf) {
    char digits[12];
    int n = u32_digits(v, digits);
    if (n > width) {
        for (int i = 0; i < width; i++) buf[i] = digits[i];
    } else {
        int pad = width - n;
        for (int i = 0; i < pad; i++) buf[i] = ' ';
        for (int i = 0; i < n; i++) buf[pad + i] = digits[i];
    }
    buf[width] = 0;
}

static int gs(int off) { return cc_global_get(off); }

/* Original HUD power-shot cadence: called once per game frame, not once per
 * rendered frame.  [0xA0C6] counts 1..0x11 then [0xA0C4] is decremented. */
void cc_hud_tick(void) {
    /* The all-caves HUD branch returns before the power-timer section. */
    if (gs(0xa0fb)) return;
    int sub_t = gs(0xa0c6) & 0xFFFF;
    sub_t++;
    if (sub_t > 0x11) {
        sub_t = 0;
        int pt = (int)(int16_t)(gs(0xa0c4) & 0xFFFF);
        pt--;
        cc_global_set(0xa0c4, pt & 0xFFFF);
    }
    cc_global_set(0xa0c6, sub_t);
}

void cc_hud_render(void) {
    unsigned char sub = (unsigned char)(gs(0x512e) & 7);
    char buf[16];
    int x, y = 1;

    /* Score: icon at cell 0, seven right-aligned digits at cells 1..7. */
    unsigned int score = ((unsigned int)(gs(0x50a7) & 0xFFFF) << 16)
                       | (unsigned int)(gs(0x50a5) & 0xFFFF);
    format_u32(score, 7, buf);
    {
        const unsigned char *g = glyph(g_f1, 14);
        if (g) cc_view_draw_glyph(g, 0 + sub, y);
    }
    x = 1 * CELL_W + sub;
    for (int i = 0; i < 7; i++) {
        if (buf[i] != ' ') {
            const unsigned char *g = glyph(g_spl, buf[i] - '0' + 8);
            if (g) cc_view_draw_glyph(g, x, y);
        }
        x += CELL_W;
    }

    if (gs(0xa0fb)) {
        /* All caves complete: "Crystals: " label + four digits. */
        /* The original label call includes a 7-pixel left margin. */
        x = 9 * CELL_W + sub + 7;
        cc_text_draw_string(x, y, "Crystals: ");
        format_u32((unsigned int)(gs(0x50a2) & 0xFFFF), 4, buf);
        x = 19 * CELL_W + sub;
        for (int i = 0; i < 4; i++) {
            if (buf[i] != ' ') {
                const unsigned char *g = glyph(g_spl, buf[i] - '0' + 8);
                if (g) cc_view_draw_glyph(g, x, y);
            }
            x += CELL_W;
        }
        return;
    }

    /* Ammo: icon at cell 11, three right-aligned digits at cells 12..14. */
    {
        const unsigned char *g = glyph(g_spl, 0);
        if (g) cc_view_draw_glyph(g, 11 * CELL_W + sub, y);
    }
    format_u32((unsigned int)(gs(0x509c) & 0xFFFF), 3, buf);
    x = 12 * CELL_W + sub;
    for (int i = 0; i < 3; i++) {
        if (buf[i] != ' ') {
            const unsigned char *g = glyph(g_spl, buf[i] - '0' + 8);
            if (g) cc_view_draw_glyph(g, x, y);
        }
        x += CELL_W;
    }

    /* Hearts at cells 19.. */
    int health = gs(0x509e) & 0xFFFF;
    if (health > 8) health = 8;
    for (int i = 0; i < health; i++) {
        const unsigned char *g = glyph(g_spl, 4);
        if (g) cc_view_draw_glyph(g, (19 + i) * CELL_W + sub, y);
    }

    /* Status icon (switch/door latch).  Its cell follows the hearts:
     * the original computes ([bp-0x66]+2 + health - 1) * 8. */
    if (gs(0xa0f9)) {
        int cell = 20 + health;
        const unsigned char *g = glyph(g_spl, 5);
        if (g) cc_view_draw_glyph(g, cell * CELL_W + sub, y);
    }

    /* Power-shot timer: *NN*, state advanced by cc_hud_tick() per game frame. */
    int pt = (int)(int16_t)(gs(0xa0c4) & 0xFFFF);
    if (pt > -1) {
        const unsigned char *star = glyph(g_f1, 20);
        if (star) cc_view_draw_glyph(star, 25 * CELL_W + sub, y);
        format_u32((unsigned int)pt, 2, buf);
        x = 26 * CELL_W + sub;
        for (int i = 0; i < 2; i++) {
            if (buf[i] != ' ') {
                const unsigned char *g = glyph(g_spl, buf[i] - '0' + 8);
                if (g) cc_view_draw_glyph(g, x, y);
            }
            x += CELL_W;
        }
        if (star) cc_view_draw_glyph(star, 28 * CELL_W + sub, y);
    }
}
