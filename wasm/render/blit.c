/* Crystal Caves render core - shape/glyph blitters.
 */
#include "blit.h"

#include "screens.h"

/* 1-based block/shape -> 160-byte planar shape, or 0. */
const unsigned char *shape_ptr(int block, int shape) {
    if (block < 1 || block > BLOCKS || shape < 1 || shape > 50) return 0;
    return g_gfx + (unsigned)(block - 1) * BLK_SIZE + 3
                  + (unsigned)(shape - 1) * SHAPE_BYTES;
}

static void blit_to(unsigned char *buf, int bw, int bh,
                    const unsigned char *shape, int px, int py) {
    if (shape == 0) return;
    /* GFX shape format (confirmed against the original blitters and
     * validated bit-exact against the emulated EGA framebuffer):
     * 160 bytes = 16 rows x 2 groups x 5 bytes [mask][plane0..plane3].
     * A set mask bit = opaque pixel; the colour index gathers the four
     * plane bits (p0=blue .. p3=intensity); MSB = leftmost pixel. */
    for (int y = 0; y < SHAPE_H; y++) {
        int fy = py + y;
        if (fy < 0 || fy >= bh) continue;
        const unsigned char *row = shape + y * (GROUPS * 5);
        for (int g = 0; g < GROUPS; g++) {
            const unsigned char *grp = row + g * 5;
            const unsigned char mask = grp[0];
            for (int bit = 0; bit < 8; bit++) {
                int bx = px + g * 8 + bit;
                if (bx < 0 || bx >= bw) continue;
                const unsigned char m = (unsigned char)(0x80u >> bit);
                if (!(mask & m)) continue;          /* transparent pixel */
                int ci = ((grp[1] & m) ? 1 : 0) | ((grp[2] & m) ? 2 : 0)
                       | ((grp[3] & m) ? 4 : 0) | ((grp[4] & m) ? 8 : 0);
                unsigned int c = g_palette[ci];
                unsigned char *d = &buf[(fy * bw + bx) * 4];
                d[0] = (c >> 16) & 0xFF; d[1] = (c >> 8) & 0xFF;
                d[2] = c & 0xFF; d[3] = 0xFF;
            }
        }
    }
}

/* Blit a planar shape into the 640x400 level canvas (shared with
 * render.c via blit.h). */
void blit(const unsigned char *shape, int px, int py) {
    blit_to(g_rgba, TILE_W, TILE_H, shape, px, py);
}

/* Draw one object sprite (block/shape are 1-based) at screen position (px,py).
 * Transparency follows the shape mask, as for map tiles; the dedicated
 * sprite blitter (0x28B5:0x191F) still needs verifying for object-specific
 * behaviour. */
void cc_draw_object(int block, int shape, int px, int py) {
    const unsigned char *s = shape_ptr(block, shape);
    if (s) blit(s, px, py);
}

void cc_draw_object_view(int block, int shape, int px, int py) {
    const unsigned char *s = shape_ptr(block, shape);
    if (s) blit_to(g_view, SCREEN_W, SCREEN_H, s, px, py);
}

/* 8x8 glyph: 8 rows x 5 bytes [mask,p0..p3]. */
void cc_view_draw_glyph(const unsigned char *glyph, int x, int y) {
    if (!glyph) return;
    for (int row = 0; row < 8; row++) {
        const unsigned char *g = glyph + row * 5;
        unsigned char mask = g[0];
        for (int bit = 0; bit < 8; bit++) {
            unsigned char m = (unsigned char)(0x80u >> bit);
            if (!(mask & m)) continue;
            int ci = ((g[1] & m) ? 1 : 0) | ((g[2] & m) ? 2 : 0)
                   | ((g[3] & m) ? 4 : 0) | ((g[4] & m) ? 8 : 0);
            view_pixel(x + bit, y + row, ci);
        }
    }
}
