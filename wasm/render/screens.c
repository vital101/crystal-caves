/* Crystal Caves render core - PCX/full-screen and view primitives.
 *
 * The shell and text modules draw into g_view (320x200 RGBA) through
 * these functions so they do not depend on renderer internals.
 */
#include "screens.h"

static const unsigned char *g_screens = 0;

void cc_set_screens(const unsigned char *screens) { g_screens = screens; }

void view_pixel(int x, int y, int color) {
    if (x < 0 || x >= SCREEN_W || y < 0 || y >= SCREEN_H) return;
    unsigned int c = g_palette[color & 15];
    unsigned char *d = &g_view[(y * SCREEN_W + x) * 4];
    d[0] = (c >> 16) & 0xFF;
    d[1] = (c >> 8) & 0xFF;
    d[2] = c & 0xFF;
    d[3] = 0xFF;
}

void cc_view_clear(int color) {
    unsigned int c = g_palette[color & 15];
    unsigned char r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
        g_view[i * 4 + 0] = r;
        g_view[i * 4 + 1] = g;
        g_view[i * 4 + 2] = b;
        g_view[i * 4 + 3] = 0xFF;
    }
}

void cc_view_fill_rect(int x, int y, int w, int h, int color) {
    for (int yy = y; yy < y + h; yy++)
        for (int xx = x; xx < x + w; xx++)
            view_pixel(xx, yy, color);
}

void cc_view_set_pixel(int x, int y, int color) { view_pixel(x, y, color); }

void cc_draw_screen(int idx) {
    if (!g_screens) { cc_view_clear(0); return; }
    if (idx < 0) idx = 0;
    const unsigned char *src = g_screens + (unsigned)idx * SCREEN_W * SCREEN_H;
    for (int y = 0; y < SCREEN_H; y++)
        for (int x = 0; x < SCREEN_W; x++)
            view_pixel(x, y, src[y * SCREEN_W + x]);
}

/* Vertical crop of two stacked 320x200 screens: rows 0..199 come from `top`,
 * rows 200..399 from `bottom`.  `row` is the virtual-screen row shown at the
 * top of the viewport (0..200).  This is the original title/credits bounce
 * (cc1.ttl over cc1.cdt) without modelling the CRTC start register. */
void cc_draw_screen_rows(int top, int bottom, int row) {
    if (!g_screens) { cc_view_clear(0); return; }
    if (top < 0) top = 0;
    if (bottom < 0) bottom = 0;
    if (row < 0) row = 0;
    if (row > SCREEN_H) row = SCREEN_H;
    for (int y = 0; y < SCREEN_H; y++) {
        int sy = row + y;
        const unsigned char *src = (sy < SCREEN_H)
            ? g_screens + (unsigned)top * SCREEN_W * SCREEN_H + sy * SCREEN_W
            : g_screens + (unsigned)bottom * SCREEN_W * SCREEN_H +
                  (sy - SCREEN_H) * SCREEN_W;
        for (int x = 0; x < SCREEN_W; x++)
            view_pixel(x, y, src[x]);
    }
}
