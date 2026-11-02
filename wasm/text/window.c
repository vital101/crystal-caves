/* Crystal Caves text - window renderer.
 */
#include "window.h"

#include "render.h"
#include "objects.h"
#include "shell_text_data.h"

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
        const unsigned char *g = font_for_char(ch);
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
static void window_layout(const char *const *lines, int count,
                          int cam_x, int cam_y, int *wx, int *wy) {
    int maxlen = 0;
    for (int i = 0; i < count; i++) {
        int n = text_len(lines[i]);
        if (i == 0) maxlen = n - 1;
        if (n - 1 > maxlen) maxlen = n - 1;
    }
    int h = count + 2;  /* text lines + one-slot gap before the sentinel */
    *wx = ((320 - maxlen * 8) / 2 + cam_x) / 8 - 3;
    *wy = ((200 - h * 8) / 2 + cam_y) / 8 - 2;
}

static void draw_window_lines(const char *const *lines, int count,
                              int cam_x, int cam_y, int scroll) {
    if (count <= 0) return;
    int wx, wy;
    window_layout(lines, count, cam_x, cam_y, &wx, &wy);
    int maxlen = 0;
    for (int i = 0; i < count; i++) {
        int n = text_len(lines[i]);
        if (i == 0) maxlen = n - 1;
        if (n - 1 > maxlen) maxlen = n - 1;
    }
    int h = count + 2;
    draw_frame_cells(wx, wy, maxlen, h, scroll);
    for (int i = 0; i < count; i++)
        draw_window_string(wx + 2, wy + i + 2, lines[i], scroll);
}

/* Locate the '^' marker in the page and return its screen cell. */
static int window_cursor_cell(const char *const *lines, int count,
                              int cam_x, int cam_y, int *xcell, int *ycell) {
    int line = -1, col = -1;
    for (int i = 0; i < count && line < 0; i++) {
        for (int j = 0; lines[i] && lines[i][j]; j++) {
            if (lines[i][j] == '^') { line = i; col = j; break; }
        }
    }
    if (line < 0) return 0;
    int wx, wy;
    window_layout(lines, count, cam_x, cam_y, &wx, &wy);
    *xcell = wx + 2 + col;
    *ycell = wy + 2 + line;
    return 1;
}

/* Original 0x0FC5:0x0A18/0x0A37 cursor animation: repaint the cell with the
 * window interior (F1 glyph 8) and then draw F2 glyph 45..49. */
void cc_text_cursor_draw(int xcell, int ycell, int frame) {
    if (frame < 1) frame = 1;
    if (frame > 5) frame = 5;
    int scroll = g_scroll();
    cc_view_draw_glyph(glyph(g_f1, 8), xcell * 8, ycell * 8 + scroll);
    cc_view_draw_glyph(glyph(g_f2, 44 + frame), xcell * 8, ycell * 8 + scroll);
}

int cc_text_draw_block_cursor(int block_id, int start_line, int max_lines,
                              int frame) {
    if (block_id < 0 || block_id >= SHELL_TEXT_COUNT) return 0;
    const ShellTextBlock *b = &g_shell_text_blocks[block_id];
    if (start_line < 0) start_line = 0;
    if (start_line >= b->count) return 0;
    int end = b->count;
    if (max_lines > 0 && start_line + max_lines < end)
        end = start_line + max_lines;
    int count = end - start_line;
    if (count <= 0) return 0;
    int cam_x = (int)(int16_t)(cc_global_get(0x512e) & 0xFFFF);
    int cam_y = (int)(int16_t)(cc_global_get(0x5130) & 0xFFFF);
    const char *const *lines = b->lines + start_line;
    draw_window_lines(lines, count, cam_x, cam_y, g_scroll());
    int x, y;
    if (frame > 0 && window_cursor_cell(lines, count, cam_x, cam_y, &x, &y))
        cc_text_cursor_draw(x, y, frame);
    return 1;
}

int cc_text_draw_lines_cursor(const char *const *lines, int count, int frame) {
    if (!lines || count <= 0) return 0;
    int cam_x = (int)(int16_t)(cc_global_get(0x512e) & 0xFFFF);
    int cam_y = (int)(int16_t)(cc_global_get(0x5130) & 0xFFFF);
    draw_window_lines(lines, count, cam_x, cam_y, g_scroll());
    int x, y;
    if (frame > 0 && window_cursor_cell(lines, count, cam_x, cam_y, &x, &y))
        cc_text_cursor_draw(x, y, frame);
    return 1;
}

/* Draw a text block using the original centered window layout.  Long blocks
 * are paged by the caller; each page is laid out with the same renderer. */
void cc_text_draw_block(int block_id, int start_line, int max_lines) {
    cc_text_draw_block_cursor(block_id, start_line, max_lines, 0);
}

/* Draw an arbitrary, generated line array with the same exact window
 * renderer.  Used by the high-score screen and the captured story pages. */
void cc_text_draw_lines(const char *const *lines, int count) {
    cc_text_draw_lines_cursor(lines, count, 0);
}
