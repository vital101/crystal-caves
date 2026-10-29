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

static const unsigned char *font_for_char(unsigned char ch) {
    /* The original window text is ASCII, but the font's lowercase glyphs
     * start six codes later than ASCII lowercase (the renderer's F2 range
     * reserves 0x49..0x5c for uppercase and punctuation). */
    if (ch >= 'a' && ch <= 'z') ch = (unsigned char)(ch - 6);
    if (ch >= 0x17 && ch <= 0x48) return glyph(g_f1, ch - 0x16);
    if (ch >= 0x49 && ch <= 0x7a) return glyph(g_f2, ch - 0x48);
    return glyph(g_f1, 0);
}

void cc_text_draw_frame(int px, int py, int pw, int ph) {
    if (pw <= 0 || ph <= 0) return;
    cc_view_fill_rect(px, py, pw, ph, 1);
    cc_view_fill_rect(px, py, pw, 2, 15);
    cc_view_fill_rect(px, py, 2, ph, 15);
    cc_view_fill_rect(px, py + ph - 2, pw, 2, 8);
    cc_view_fill_rect(px + pw - 2, py, 2, ph, 8);
    for (int y = py + 2; y < py + ph - 2; y += 8)
        for (int x = px + 2; x < px + pw - 2; x += 8)
            cc_view_set_pixel(x, y, 0);
}

void cc_text_draw_string(int x, int y, const char *s) {
    if (!s) return;
    while (*s) {
        unsigned char ch = (unsigned char)*s++;
        const unsigned char *g = font_for_char(ch);
        if (g) cc_view_draw_glyph(g, x, y);
        x += 8;
    }
}

int cc_text_block_lines(int block_id) {
    if (block_id < 0 || block_id >= SHELL_TEXT_COUNT) return 0;
    return g_shell_text_blocks[block_id].count;
}

/* Draw a window using the original centered layout: the text block already
 * carries its own leading spaces, so only the window frame and the starting
 * cell are computed here.  Long blocks are paged by the caller. */
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

    int maxlen = 0;
    for (int i = start_line; i < end; i++) {
        int n = 0;
        while (b->lines[i][n]) n++;
        if (n > maxlen) maxlen = n;
    }
    if (maxlen > 36) maxlen = 36;

    int wcells = maxlen + 6;
    int hcells = count + 3;
    if (wcells > 40) wcells = 40;
    if (hcells > 25) hcells = 25;
    int wx = (40 - wcells) / 2;
    int wy = (25 - hcells) / 2;
    if (wx < 0) wx = 0;
    if (wy < 0) wy = 0;
    int px = wx * CELL_W, py = wy * CELL_H;
    int pw = wcells * CELL_W, ph = hcells * CELL_H;

    cc_text_draw_frame(px, py, pw, ph);

    int tx = px + 3 * CELL_W;
    int ty = py + 1 * CELL_H;
    for (int i = start_line; i < end; i++) {
        cc_text_draw_string(tx, ty, b->lines[i]);
        ty += CELL_H;
    }
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

    /* Power-shot timer: *NN*, advancing the original timers on each draw. */
    int pt = (int)(int16_t)(gs(0xa0c4) & 0xFFFF);
    int sub_t = gs(0xa0c6) & 0xFFFF;
    sub_t++;
    if (sub_t > 0x11) {
        sub_t = 0;
        pt--;
        cc_global_set(0xa0c4, pt & 0xFFFF);
    }
    cc_global_set(0xa0c6, sub_t);
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
