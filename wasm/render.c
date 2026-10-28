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
static unsigned char g_heap[1 << 20];
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

static void blit(const unsigned char *shape, int px, int py) {
    if (shape == 0) return;
    /* GFX shape format (confirmed against the original blitters and
     * validated bit-exact against the emulated EGA framebuffer):
     * 160 bytes = 16 rows x 2 groups x 5 bytes [mask][plane0..plane3].
     * A set mask bit = opaque pixel; the colour index gathers the four
     * plane bits (p0=blue .. p3=intensity); MSB = leftmost pixel. */
    for (int y = 0; y < SHAPE_H; y++) {
        int fy = py + y;
        if (fy < 0 || fy >= TILE_H) continue;
        const unsigned char *row = shape + y * (GROUPS * 5);
        for (int g = 0; g < GROUPS; g++) {
            const unsigned char *grp = row + g * 5;
            const unsigned char mask = grp[0];
            for (int bit = 0; bit < 8; bit++) {
                int bx = px + g * 8 + bit;
                if (bx < 0 || bx >= TILE_W) continue;
                const unsigned char m = (unsigned char)(0x80u >> bit);
                if (!(mask & m)) continue;          /* transparent pixel */
                int ci = ((grp[1] & m) ? 1 : 0) | ((grp[2] & m) ? 2 : 0)
                       | ((grp[3] & m) ? 4 : 0) | ((grp[4] & m) ? 8 : 0);
                unsigned int c = g_palette[ci];
                unsigned char *d = &g_rgba[(fy * TILE_W + bx) * 4];
                d[0] = (c >> 16) & 0xFF; d[1] = (c >> 8) & 0xFF;
                d[2] = c & 0xFF; d[3] = 0xFF;
            }
        }
    }
}

/* Draw one object sprite (block/shape are 1-based) at screen position (px,py).
 * Transparency follows the shape mask, as for map tiles; the dedicated
 * sprite blitter (0x28B5:0x191F) still needs verifying for object-specific
 * behaviour. */
void cc_draw_object(int block, int shape, int px, int py) {
    const unsigned char *s = shape_ptr(block, shape);
    if (s) blit(s, px, py);
}

/* Render a baked level: layer 1, then 2, then 3 (SetCell helper CS:0x0000;
 * layers 2 and 3 are skipped when their shape is 0). */
void cc_render_level(int level, unsigned char *out_rgba) {
    memset(g_rgba, 0, sizeof(g_rgba));
    if (!g_gfx || !g_levels) { memcpy(out_rgba, g_rgba, sizeof(g_rgba)); return; }
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
    memcpy(out_rgba, g_rgba, sizeof(g_rgba));
}
