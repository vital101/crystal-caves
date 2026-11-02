/* Crystal Caves Episode 1 - wasm render core (original, clean-room).
 *
 * Freestanding: compiles with either `emcc` or `clang --target=wasm32
 * -nostdlib` (linked by wasm-ld).  Consumes the portable intermediate from
 * work/tools/export_assets.py:
 *   gfx.bin     CC1.GFX (23 blocks x 8003 B; block & shape indices are 1-based)
 *   levels.bin  19 levels x 40*25 cells x 6 B [b1,s1,b2,s2,b3,s3]
 *   palette   16 x uint32 0x00RRGGBB (loaded via cc_set_palette)
 *
 * A shape is 16x16, stored as 16 rows x 2 groups x 5 bytes
 * [mask][plane0..plane3]; mask bit set = opaque pixel, colour index =
 * p0 + 2*p1 + 4*p2 + 8*p3, MSB = leftmost.  This only rasterises the baked
 * tile map; gameplay is a later phase. */

#define BLK_SIZE      8003
#define SHAPE_BYTES   160
#define SHAPE_W       16
#define SHAPE_H       16
#define GROUPS        2
#define PALETTE_SIZE  16
#define BLOCKS        23
#define LEVEL_W       40
#define LEVEL_H       25
#define LEVELS        19
#define CELL_BYTES    6
#define TILE_W        640
#define TILE_H        400
#define SCREEN_W      320
#define SCREEN_H      200

/* --- tiny freestanding runtime ------------------------------------------ */
void *memset(void *d, int c, unsigned n) {
    unsigned char *p = (unsigned char *)d;
    while (n--) *p++ = (unsigned char)c;
    return d;
}
void *memcpy(void *d, const void *s, unsigned n) {
    unsigned char *p = (unsigned char *)d;
    const unsigned char *q = (const unsigned char *)s;
    while (n--) *p++ = *q++;
    return d;
}

/* bump allocator over a static arena (JS copies assets in via cc_alloc). */
static unsigned char g_heap[4 << 20];
static unsigned int g_heap_top = 0;

void *cc_alloc(unsigned size) {
    size = (size + 15u) & ~15u;
    if (g_heap_top + size > sizeof(g_heap)) return 0;
    void *p = &g_heap[g_heap_top];
    g_heap_top += size;
    return p;
}

/* --- render state ------------------------------------------------------- */
static const unsigned char *g_gfx = 0;
static const unsigned char *g_levels = 0;
static unsigned int g_palette[PALETTE_SIZE];
static unsigned char g_rgba[TILE_W * TILE_H * 4];
static unsigned char g_view[SCREEN_W * SCREEN_H * 4];

void cc_set_assets(const unsigned char *gfx, const unsigned char *levels) {
    g_gfx = gfx;
    g_levels = levels;
}
void cc_set_palette(const unsigned int *pal32) {
    memcpy(g_palette, pal32, sizeof(g_palette));
}
unsigned char *cc_framebuffer(void) { return g_rgba; }
int cc_tex_width(void)  { return TILE_W; }
int cc_tex_height(void) { return TILE_H; }

/* 1-based block/shape -> 160-byte planar shape, or 0. */
static const unsigned char *shape_ptr(int block, int shape) {
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

static void blit(const unsigned char *shape, int px, int py) {
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

unsigned char *cc_view_framebuffer(void) { return g_view; }
int cc_view_width(void)  { return SCREEN_W; }
int cc_view_height(void) { return SCREEN_H; }

/* Render a baked level: layer 1, then 2, then 3 (SetCell helper CS:0x0000;
 * layers 2 and 3 are skipped when their shape is 0). */
static void render_level_buf(int level) {
    memset(g_rgba, 0, sizeof(g_rgba));
    if (!g_gfx || !g_levels) return;
    if (level < 0 || level >= LEVELS) level = 0;

    const unsigned char *cells =
        g_levels + (unsigned)level * LEVEL_W * LEVEL_H * CELL_BYTES;
    for (int r = 0; r < LEVEL_H; r++) {
        for (int c = 0; c < LEVEL_W; c++) {
            const unsigned char *cell = cells + (unsigned)(r * LEVEL_W + c) * CELL_BYTES;
            /* cell layout: [b1,s1,b2,s2,b3,s3]; draw order 1, 2, 3. */
            int b1 = cell[0], s1 = cell[1], b2 = cell[2], s2 = cell[3];
            int b3 = cell[4], s3 = cell[5];
            int px = c * SHAPE_W, py = r * SHAPE_H;
            const unsigned char *s;
            if ((s = shape_ptr(b1, s1))) blit(s, px, py);
            if ((s = shape_ptr(b2, s2))) blit(s, px, py);
            if ((s = shape_ptr(b3, s3))) blit(s, px, py);
        }
    }
}

/* Dynamic renderer: draw the mutable 20-byte cell records owned by objects.c.
 * Mapping confirmed by tools/build_cells.py:
 *   layer1 = (w4,w2), layer2 = (w5,w3) if shape, layer3 = (w6,w7) if shape. */
extern int cc_cell_byte(int col, int row, int off);

static int cell_word(int col, int row, int off) {
    return cc_cell_byte(col, row, off) |
           (cc_cell_byte(col, row, off + 1) << 8);
}

static void render_cells_buf(int level) {
    (void)level;
    memset(g_rgba, 0, sizeof(g_rgba));
    if (!g_gfx) return;
    for (int r = 0; r < LEVEL_H; r++) {
        for (int c = 0; c < LEVEL_W; c++) {
            int b[3], sh[3];
            b[0] = cell_word(c, r, 0x08); sh[0] = cell_word(c, r, 0x04);
            b[1] = cell_word(c, r, 0x0A); sh[1] = cell_word(c, r, 0x06);
            b[2] = cell_word(c, r, 0x0C); sh[2] = cell_word(c, r, 0x0E);
            for (int l = 0; l < 3; l++) {
                int blk = b[l];
                if (l > 0 && sh[l] == 0) continue;
                if (blk == 0 && sh[l] != 0) blk = 20;
                const unsigned char *s = shape_ptr(blk, sh[l]);
                if (s) blit(s, c * SHAPE_W, r * SHAPE_H);
            }
        }
    }
}

void cc_render_cells(int level, unsigned char *out_rgba) {
    render_cells_buf(level);
    memcpy(out_rgba, g_rgba, sizeof(g_rgba));
}

/* Viewport from the current mutable cell records (used by the game shell). */
void cc_render_view_cells(int level, int cam_x, int cam_y) {
    int x0 = cam_x, y0 = cam_y;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x0 > TILE_W - SCREEN_W) x0 = TILE_W - SCREEN_W;
    if (y0 > TILE_H - SCREEN_H) y0 = TILE_H - SCREEN_H;
    render_cells_buf(level);
    for (int y = 0; y < SCREEN_H; y++) {
        memcpy(&g_view[y * SCREEN_W * 4],
               &g_rgba[((y + y0) * TILE_W + x0) * 4],
               SCREEN_W * 4);
    }
}

void cc_render_level(int level, unsigned char *out_rgba) {
    render_level_buf(level);
    memcpy(out_rgba, g_rgba, sizeof(g_rgba));
}

/* Render the 320x200 camera viewport from a baked level. */
void cc_render_view(int level, int cam_x, int cam_y) {
    int x0 = cam_x, y0 = cam_y;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x0 > TILE_W - SCREEN_W) x0 = TILE_W - SCREEN_W;
    if (y0 > TILE_H - SCREEN_H) y0 = TILE_H - SCREEN_H;
    render_level_buf(level);
    for (int y = 0; y < SCREEN_H; y++) {
        memcpy(&g_view[y * SCREEN_W * 4],
               &g_rgba[((y + y0) * TILE_W + x0) * 4],
               SCREEN_W * 4);
    }
}

/* ---------------------------------------------------------------- shell
 * PCX/full-screen and text/HUD drawing helpers.  The shell and text modules
 * draw into g_view (320x200 RGBA) through these functions so they do not
 * depend on renderer internals. */
static const unsigned char *g_screens = 0;

void cc_set_screens(const unsigned char *screens) { g_screens = screens; }

static void view_pixel(int x, int y, int color) {
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
